// cam_transform.cpp — Perspective (homography) transform via pre-computed LUT
//
// Two modes are compiled, selected by the CAM_ENH_BILINEAR feature gate:
//
//   Nearest-neighbour (CAM_ENH_BILINEAR == 0)
//     LUT entry: (src_y << 16) | src_x  — integer coordinates.
//     apply() copies one source pixel per output pixel.
//
//   Bilinear (CAM_ENH_BILINEAR == 1)
//     LUT entry: (y_12_4 << 16) | x_12_4  — 12.4 fixed-point coordinates.
//     apply() samples a 2×2 neighbourhood and interpolates each RGB565
//     channel (R5, G6, B5) independently at its NATIVE bit depth.
//
//     This avoids the colour bias that plagues RGB565 → RGB888 → RGB565
//     round-trips: because R and B have 5-bit precision while G has 6-bit,
//     any expansion to a uniform wider type (e.g. <<3 / <<2) creates an
//     asymmetric "white point" (248, 252, 248 for the naive shift).
//     Interpolating in that skewed space and truncating back introduces a
//     systematic green tint.  By never leaving the native 5/6/5 domain we
//     eliminate the asymmetry entirely.
//
//     Weight sum is always exactly 256 (4-bit fractions → 16×16 grid),
//     so the final division is an exact >>8 with +128 rounding bias.

#include "cam_transform.h"
#include "app_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#undef ESP_LOGV
#define ESP_LOGE(tag, fmt, ...) APP_LOGE(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) APP_LOGW(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) APP_LOGI(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) APP_LOGD(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) APP_LOGV(tag, fmt, ##__VA_ARGS__)

static const char *TAG = "cam_transform";

// Sentinel for out-of-bounds LUT entries (safe: max valid 12.4 value for
// UXGA 1600×1200 is ~25600, well below 0xFFFF in each half-word).
#define LUT_OOB  0xFFFFFFFFU

typedef struct cam_transform_ctx {
    uint16_t src_w, src_h;
    uint16_t dst_w, dst_h;
    bool     identity;
    bool     swap_bytes;  // source pixels need byte-swap for channel extraction
    uint32_t *lut;
    // When CAM_ENH_BILINEAR: entries are 12.4 fixed-point (y_fp4 << 16) | x_fp4
    // Otherwise:             entries are integer       (src_y  << 16) | src_x
} cam_transform_ctx_t;

// ---------------------------------------------------------------------------
// Invert a 3×3 matrix; returns false if singular.
// ---------------------------------------------------------------------------
static bool invert3x3(const float H[9], float Hi[9]) {
    float det = H[0] * (H[4]*H[8] - H[5]*H[7])
              - H[1] * (H[3]*H[8] - H[5]*H[6])
              + H[2] * (H[3]*H[7] - H[4]*H[6]);
    if (fabsf(det) < 1e-10f) return false;
    float inv = 1.0f / det;
    Hi[0] = (H[4]*H[8] - H[5]*H[7]) * inv;
    Hi[1] = (H[2]*H[7] - H[1]*H[8]) * inv;
    Hi[2] = (H[1]*H[5] - H[2]*H[4]) * inv;
    Hi[3] = (H[5]*H[6] - H[3]*H[8]) * inv;
    Hi[4] = (H[0]*H[8] - H[2]*H[6]) * inv;
    Hi[5] = (H[2]*H[3] - H[0]*H[5]) * inv;
    Hi[6] = (H[3]*H[7] - H[4]*H[6]) * inv;
    Hi[7] = (H[1]*H[6] - H[0]*H[7]) * inv;
    Hi[8] = (H[0]*H[4] - H[1]*H[3]) * inv;
    return true;
}

// ---------------------------------------------------------------------------
// Create the transform (build LUT).
// ---------------------------------------------------------------------------
cam_transform_t cam_transform_create(uint16_t src_w, uint16_t src_h,
                                     uint16_t dst_w, uint16_t dst_h,
                                     const float H[9]) {
    cam_transform_ctx_t *ctx = (cam_transform_ctx_t *)
        heap_caps_calloc(1, sizeof(cam_transform_ctx_t), MALLOC_CAP_DEFAULT);
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to alloc transform context");
        return NULL;
    }

    ctx->src_w = src_w;
    ctx->src_h = src_h;
    ctx->dst_w = dst_w;
    ctx->dst_h = dst_h;
    ctx->swap_bytes = false;

    // Detect identity homography.
    static const float I[9] = {1,0,0, 0,1,0, 0,0,1};
    ctx->identity = true;
    for (int i = 0; i < 9; i++) {
        if (fabsf(H[i] - I[i]) > 1e-5f) { ctx->identity = false; break; }
    }

    // Pure identity with matching dimensions → no LUT needed (memcpy path).
    if (ctx->identity && src_w == dst_w && src_h == dst_h) {
        ESP_LOGI(TAG, "Identity transform — LUT skipped");
        ctx->lut = NULL;
        return ctx;
    }

    size_t lut_entries = (size_t)dst_w * dst_h;
    ctx->lut = (uint32_t *)heap_caps_malloc(lut_entries * sizeof(uint32_t),
                                            MALLOC_CAP_SPIRAM);
    if (!ctx->lut) {
        ESP_LOGE(TAG, "Failed to alloc LUT (%zu bytes)",
                 lut_entries * sizeof(uint32_t));
        heap_caps_free(ctx);
        return NULL;
    }

    // If caller provided identity but dims differ, substitute a scale matrix
    // so the inverse-homography LUT maps output pixels to source pixels with
    // the correct scale factor.
    float H_eff[9];
    if (ctx->identity && (src_w != dst_w || src_h != dst_h)) {
        // H maps src → dst as a pure scale.  Inverse maps dst → src.
        H_eff[0] = (float)dst_w / src_w; H_eff[1] = 0.0f;                H_eff[2] = 0.0f;
        H_eff[3] = 0.0f;                 H_eff[4] = (float)dst_h / src_h; H_eff[5] = 0.0f;
        H_eff[6] = 0.0f;                 H_eff[7] = 0.0f;                 H_eff[8] = 1.0f;
        ctx->identity = false;
    } else {
        memcpy(H_eff, H, sizeof(H_eff));
    }

    float Hi[9];
    if (!invert3x3(H_eff, Hi)) {
        ESP_LOGW(TAG, "Singular homography — using identity mapping");
        for (size_t i = 0; i < lut_entries; i++) {
            uint16_t y = (uint16_t)(i / dst_w);
            uint16_t x = (uint16_t)(i % dst_w);
#if CAM_ENH_BILINEAR
            if (x < src_w && y < src_h)
                ctx->lut[i] = ((uint32_t)((uint16_t)y << 4) << 16)
                             | (uint32_t)((uint16_t)x << 4);
#else
            if (x < src_w && y < src_h)
                ctx->lut[i] = ((uint32_t)y << 16) | x;
#endif
            else ctx->lut[i] = LUT_OOB;
        }
        return ctx;
    }

    // ----- Build the LUT: for each output pixel, compute the source coord -----
    uint32_t oob_count = 0;

    for (uint16_t y = 0; y < dst_h; y++) {
        for (uint16_t x = 0; x < dst_w; x++) {
            float xf = (float)x;
            float yf = (float)y;
            float w  = Hi[6]*xf + Hi[7]*yf + Hi[8];
            size_t idx = (size_t)y * dst_w + x;

            if (fabsf(w) < 1e-10f) {
                ctx->lut[idx] = LUT_OOB;
                oob_count++;
                continue;
            }

            float inv_w = 1.0f / w;
            float sx = (Hi[0]*xf + Hi[1]*yf + Hi[2]) * inv_w;
            float sy = (Hi[3]*xf + Hi[4]*yf + Hi[5]) * inv_w;

#if CAM_ENH_BILINEAR
            // ---- Bilinear 12.4 fixed-point LUT ----
            // Source coord must be in [0, src_dim) for the base pixel.
            // The bilinear neighbourhood extends to ix+1 / iy+1, which is
            // handled by clamping in the apply() function.
            if (sx < 0.0f || sx >= (float)src_w ||
                sy < 0.0f || sy >= (float)src_h) {
                ctx->lut[idx] = LUT_OOB;
                oob_count++;
                continue;
            }

            // Clamp to [0, src_dim-1] for encoding (the fractional part at
            // the last pixel column/row will blend with itself via clamping).
            float sx_c = fminf(sx, (float)(src_w - 1));
            float sy_c = fminf(sy, (float)(src_h - 1));

            // Truncate (floor) to 12.4 fixed-point.
            uint16_t x_fp4 = (uint16_t)(sx_c * 16.0f);
            uint16_t y_fp4 = (uint16_t)(sy_c * 16.0f);

            // Safety: ensure integer part ≤ src_dim-1 after float→int truncation.
            uint16_t max_x_fp4 = (uint16_t)(((src_w - 1) << 4) | 0xF);
            uint16_t max_y_fp4 = (uint16_t)(((src_h - 1) << 4) | 0xF);
            if (x_fp4 > max_x_fp4) x_fp4 = max_x_fp4;
            if (y_fp4 > max_y_fp4) y_fp4 = max_y_fp4;

            ctx->lut[idx] = ((uint32_t)y_fp4 << 16) | x_fp4;
#else
            // ---- Nearest-neighbour integer LUT ----
            int ix = (int)(sx + 0.5f);
            int iy = (int)(sy + 0.5f);
            if (ix >= 0 && ix < (int)src_w && iy >= 0 && iy < (int)src_h)
                ctx->lut[idx] = ((uint32_t)(uint16_t)iy << 16) | (uint16_t)ix;
            else {
                ctx->lut[idx] = LUT_OOB;
                oob_count++;
            }
#endif
        }
    }

    ESP_LOGI(TAG, "LUT built: %ux%u → %ux%u (%s), OOB pixels: %lu",
             src_w, src_h, dst_w, dst_h,
#if CAM_ENH_BILINEAR
             "bilinear 12.4fp",
#else
             "nearest-neighbour",
#endif
             (unsigned long)oob_count);
    return ctx;
}

// ---------------------------------------------------------------------------
void cam_transform_destroy(cam_transform_t ctx) {
    if (!ctx) return;
    if (ctx->lut) heap_caps_free(ctx->lut);
    heap_caps_free(ctx);
}

// ---------------------------------------------------------------------------
// Apply the transform.
// ---------------------------------------------------------------------------
void cam_transform_apply(cam_transform_t ctx,
                         const uint16_t *src_rgb565,
                         uint16_t *dst_rgb565) {
    if (!ctx) return;

    // Identity fast-path (no LUT, matching dimensions).
    if (ctx->identity && !ctx->lut) {
        memcpy(dst_rgb565, src_rgb565,
               (size_t)ctx->dst_w * ctx->dst_h * sizeof(uint16_t));
        return;
    }

    const uint32_t *lut = ctx->lut;
    const size_t    n   = (size_t)ctx->dst_w * ctx->dst_h;
    const uint16_t  sw  = ctx->src_w;

#if CAM_ENH_BILINEAR
    // -------------------------------------------------------------------
    // Bilinear interpolation in NATIVE RGB565 component space.
    //
    // BYTE-LEVEL channel extraction (endianness-proof)
    //
    // The camera (OV3660 DVP) stores RGB565 as two bytes per pixel in
    // big-endian order:
    //     byte[0] = RRRRRGGG    (R[4:0] in bits 7–3, G[5:3] in bits 2–0)
    //     byte[1] = GGGBBBBB    (G[2:0] in bits 7–5, B[4:0] in bits 4–0)
    //
    // The JPEG encoder (fmt2jpg / convert_line_format) reads these bytes
    // directly — it does NOT interpret them as uint16_t.  On a little-
    // endian ESP32, reading the same two bytes as a uint16_t gives a
    // byte-swapped value whose bit positions do NOT match the standard
    // RGB565 mask layout (R[15:11], G[10:5], B[4:0]).
    //
    // Previous implementations extracted channels via uint16_t masks,
    // which cross-contaminated the R/G/B values and produced a purple
    // tint after blending (the garbled output bytes no longer matched
    // what fmt2jpg expects).
    //
    // By reading bytes directly — exactly as fmt2jpg does — we guarantee
    // correct channel extraction regardless of CPU endianness, and the
    // composed output bytes are in the format fmt2jpg expects.
    //
    // Each channel (R:5-bit, G:6-bit, B:5-bit) is interpolated at its
    // native precision.  Weight sum is always exactly 256 (4-bit
    // fractions → 16×16 grid), so >>8 with +128 rounding is exact.
    // No channel expansion → no asymmetric white point → no color bias.
    // -------------------------------------------------------------------
    const uint16_t  sh    = ctx->src_h;
    const uint8_t  *src_b = (const uint8_t *)src_rgb565;
    uint8_t        *dst_b = (uint8_t *)dst_rgb565;

    for (size_t i = 0; i < n; i++) {
        uint32_t entry = lut[i];
        if (entry == LUT_OOB) {
            dst_b[i * 2]     = 0;
            dst_b[i * 2 + 1] = 0;
            continue;
        }

        uint16_t x_fp4 = (uint16_t)(entry & 0xFFFF);
        uint16_t y_fp4 = (uint16_t)(entry >> 16);
        uint16_t ix    = x_fp4 >> 4;
        uint16_t iy    = y_fp4 >> 4;
        uint8_t  fx    = (uint8_t)(x_fp4 & 0xF);   // [0..15]
        uint8_t  fy    = (uint8_t)(y_fp4 & 0xF);   // [0..15]

        // Fast path: exact integer coordinate → copy two bytes verbatim.
        if (fx == 0 && fy == 0) {
            size_t off = ((size_t)iy * sw + ix) * 2;
            dst_b[i * 2]     = src_b[off];
            dst_b[i * 2 + 1] = src_b[off + 1];
            continue;
        }

        // 2×2 neighbourhood with boundary clamping.
        uint16_t ix1 = (uint16_t)((ix + 1 < sw) ? ix + 1 : ix);
        uint16_t iy1 = (uint16_t)((iy + 1 < sh) ? iy + 1 : iy);

        // Byte offsets of the four source pixels.
        size_t off00 = ((size_t)iy  * sw + ix ) * 2;
        size_t off10 = ((size_t)iy  * sw + ix1) * 2;
        size_t off01 = ((size_t)iy1 * sw + ix ) * 2;
        size_t off11 = ((size_t)iy1 * sw + ix1) * 2;

        // Extract R(5), G(6), B(5) from the camera's byte layout:
        //   byte[0] = RRRRRGGG,  byte[1] = GGGBBBBB
        // This matches the exact byte-level reading in fmt2jpg's
        // convert_line_format() for PIXFORMAT_RGB565.
        uint8_t r00 = src_b[off00] >> 3;
        uint8_t g00 = (uint8_t)(((src_b[off00] & 0x07) << 3) | (src_b[off00 + 1] >> 5));
        uint8_t b00 = src_b[off00 + 1] & 0x1F;

        uint8_t r10 = src_b[off10] >> 3;
        uint8_t g10 = (uint8_t)(((src_b[off10] & 0x07) << 3) | (src_b[off10 + 1] >> 5));
        uint8_t b10 = src_b[off10 + 1] & 0x1F;

        uint8_t r01 = src_b[off01] >> 3;
        uint8_t g01 = (uint8_t)(((src_b[off01] & 0x07) << 3) | (src_b[off01 + 1] >> 5));
        uint8_t b01 = src_b[off01 + 1] & 0x1F;

        uint8_t r11 = src_b[off11] >> 3;
        uint8_t g11 = (uint8_t)(((src_b[off11] & 0x07) << 3) | (src_b[off11 + 1] >> 5));
        uint8_t b11 = src_b[off11 + 1] & 0x1F;

        // Bilinear weights.  fx, fy ∈ [0,15]; total = 16×16 = 256.
        uint16_t w00 = (uint16_t)(16 - fx) * (uint16_t)(16 - fy);
        uint16_t w10 = (uint16_t)fx        * (uint16_t)(16 - fy);
        uint16_t w01 = (uint16_t)(16 - fx) * (uint16_t)fy;
        uint16_t w11 = (uint16_t)fx        * (uint16_t)fy;
        // w00 + w10 + w01 + w11 == 256  always.

        // Weighted average per channel — stays in native 5/6/5 precision.
        //   max(R,B) × 256 = 31 × 256 =  7 936  → fits uint16_t
        //   max(G)   × 256 = 63 × 256 = 16 128  → fits uint16_t
        uint8_t r = (uint8_t)((r00*w00 + r10*w10 + r01*w01 + r11*w11 + 128) >> 8);
        uint8_t g = (uint8_t)((g00*w00 + g10*w10 + g01*w01 + g11*w11 + 128) >> 8);
        uint8_t b = (uint8_t)((b00*w00 + b10*w10 + b01*w01 + b11*w11 + 128) >> 8);

        // Compose back to the camera's byte layout: [RRRRRGGG][GGGBBBBB]
        dst_b[i * 2]     = (uint8_t)((r << 3) | (g >> 3));
        dst_b[i * 2 + 1] = (uint8_t)(((g & 0x07) << 5) | b);
    }

#else  // !CAM_ENH_BILINEAR
    // -------------------------------------------------------------------
    // Nearest-neighbour: one LUT lookup, one pixel copy.
    // -------------------------------------------------------------------
    for (size_t i = 0; i < n; i++) {
        uint32_t entry = lut[i];
        if (entry == LUT_OOB) {
            dst_rgb565[i] = 0x0000;
        } else {
            uint16_t sy = (uint16_t)(entry >> 16);
            uint16_t sx = (uint16_t)(entry & 0xFFFF);
            dst_rgb565[i] = src_rgb565[(size_t)sy * sw + sx];
        }
    }
#endif // CAM_ENH_BILINEAR
}

// ---------------------------------------------------------------------------
void cam_transform_get_size(cam_transform_t ctx,
                            uint16_t *dst_w, uint16_t *dst_h) {
    if (ctx) { *dst_w = ctx->dst_w; *dst_h = ctx->dst_h; }
}

void cam_transform_get_src_size(cam_transform_t ctx,
                                uint16_t *src_w, uint16_t *src_h) {
    if (ctx) { *src_w = ctx->src_w; *src_h = ctx->src_h; }
}

bool cam_transform_is_identity(cam_transform_t ctx) {
    return ctx ? ctx->identity : true;
}

void cam_transform_set_swap_bytes(cam_transform_t ctx, bool swap) {
    if (ctx) ctx->swap_bytes = swap;
}
