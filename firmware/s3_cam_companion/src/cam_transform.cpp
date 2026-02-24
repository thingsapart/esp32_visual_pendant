// cam_transform.cpp — Perspective transform via PSRAM-resident LUT
//
// Enhanced processing (CAM_ENHANCED_PROCESSING=1):
//   - 12.4 fixed-point sub-pixel coordinates for bilinear interpolation
//   - Per-pixel sample count for adaptive area averaging in minification zones
//   - Direct capture→output remapping (no same-size requirement)

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

// Sentinel for out-of-bounds LUT entries (both legacy and enhanced).
#define LUT_OOB  0xFFFFFFFFU

// ---------------------------------------------------------------------------
// Fixed-point format for enhanced bilinear interpolation (12.4)
// 12 integer bits per axis = max 4095 pixels,  4 fractional bits = 1/16 sub-pixel
// Packed into 32 bits: [y_fp12.4 : 16 bits] [x_fp12.4 : 16 bits]
// ---------------------------------------------------------------------------
#if CAM_ENH_BILINEAR || CAM_ENH_AREA_AVERAGE
#define FP_FRAC_BITS  4
#define FP_ONE        (1 << FP_FRAC_BITS)          // 16
#define FP_MASK       (FP_ONE - 1)                  // 0x0F
#define FP_FROM_FLOAT(f) ((int16_t)((f) * FP_ONE + 0.5f))
#endif

typedef struct cam_transform_ctx {
    uint16_t src_w, src_h;
    uint16_t dst_w, dst_h;
    bool     identity;

    // LUT: dst_w × dst_h entries, stored in PSRAM.
    // Legacy (nearest-neighbour): (src_y << 16) | src_x  (integer coords)
    // Enhanced (bilinear): (src_y_fp12.4 << 16) | src_x_fp12.4
    uint32_t *lut;

#if CAM_ENH_AREA_AVERAGE
    // Per-pixel sample count (1, 4, or 9 = 1×1, 2×2, or 3×3 box).
    // Determined at LUT build time from the local Jacobian magnitude.
    // Stored in PSRAM alongside the LUT.
    uint8_t *sample_count;
#endif
} cam_transform_ctx_t;

// ---------------------------------------------------------------------------
// Invert a 3×3 matrix; returns false if singular
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
// Build the inverse-homography LUT
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

    // Check if H is (approximately) the identity matrix.
    static const float I[9] = {1,0,0, 0,1,0, 0,0,1};
    ctx->identity = true;
    for (int i = 0; i < 9; i++) {
        if (fabsf(H[i] - I[i]) > 1e-5f) { ctx->identity = false; break; }
    }

    if (ctx->identity && src_w == dst_w && src_h == dst_h) {
        ESP_LOGI(TAG, "Identity transform — LUT skipped");
        ctx->lut = NULL;
#if CAM_ENH_AREA_AVERAGE
        ctx->sample_count = NULL;
#endif
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

#if CAM_ENH_AREA_AVERAGE
    ctx->sample_count = (uint8_t *)heap_caps_malloc(lut_entries, MALLOC_CAP_SPIRAM);
    if (!ctx->sample_count) {
        ESP_LOGW(TAG, "Failed to alloc sample_count map — area averaging disabled");
    }
#endif

    // When the caller passed the identity matrix but capture and output
    // dimensions differ we must use a proper scale homography — otherwise the
    // inverted identity produces a 1:1 crop instead of a full-frame resample.
    // H_eff maps src→dst; its inverse (used in the LUT) maps dst→src.
    float H_eff[9];
    if (ctx->identity && (src_w != dst_w || src_h != dst_h)) {
        ESP_LOGI(TAG, "Identity H with mismatched dims (%ux%u→%ux%u) — "
                 "substituting scale homography", src_w, src_h, dst_w, dst_h);
        H_eff[0] = (float)dst_w / src_w; H_eff[1] = 0.0f;                H_eff[2] = 0.0f;
        H_eff[3] = 0.0f;                 H_eff[4] = (float)dst_h / src_h; H_eff[5] = 0.0f;
        H_eff[6] = 0.0f;                 H_eff[7] = 0.0f;                 H_eff[8] = 1.0f;
        ctx->identity = false;  // LUT is now non-trivial
    } else {
        memcpy(H_eff, H, sizeof(H_eff));
    }

    // Compute inverse homography: H_inv maps dst → src.
    float Hi[9];
    if (!invert3x3(H_eff, Hi)) {
        ESP_LOGW(TAG, "Singular homography — using identity");
        for (size_t i = 0; i < lut_entries; i++) {
            uint16_t y = (uint16_t)(i / dst_w);
            uint16_t x = (uint16_t)(i % dst_w);
            if (x < src_w && y < src_h) {
#if CAM_ENH_BILINEAR
                ctx->lut[i] = ((uint32_t)(y << FP_FRAC_BITS) << 16) | (uint16_t)(x << FP_FRAC_BITS);
#else
                ctx->lut[i] = ((uint32_t)y << 16) | x;
#endif
            } else {
                ctx->lut[i] = LUT_OOB;
            }
#if CAM_ENH_AREA_AVERAGE
            if (ctx->sample_count) ctx->sample_count[i] = 1;
#endif
        }
        return ctx;
    }

    // Pre-compute for every output pixel.
    uint32_t oob_count = 0;

    for (uint16_t y = 0; y < dst_h; y++) {
        for (uint16_t x = 0; x < dst_w; x++) {
            float xf = (float)x;
            float yf = (float)y;
            float w  = Hi[6]*xf + Hi[7]*yf + Hi[8];
            size_t idx = (size_t)y * dst_w + x;

            if (fabsf(w) < 1e-10f) {
                ctx->lut[idx] = LUT_OOB;
#if CAM_ENH_AREA_AVERAGE
                if (ctx->sample_count) ctx->sample_count[idx] = 1;
#endif
                oob_count++;
                continue;
            }
            float inv_w = 1.0f / w;
            float sx = (Hi[0]*xf + Hi[1]*yf + Hi[2]) * inv_w;
            float sy = (Hi[3]*xf + Hi[4]*yf + Hi[5]) * inv_w;

#if CAM_ENH_BILINEAR
            // Store as 12.4 fixed-point for sub-pixel bilinear interpolation.
            // Clamp to valid range with 1px margin for bilinear tap.
            if (sx >= -0.5f && sx < (float)(src_w - 1) + 0.5f &&
                sy >= -0.5f && sy < (float)(src_h - 1) + 0.5f) {
                // Clamp to [0, src_w-1] × [0, src_h-1] for the integer part
                if (sx < 0.0f) sx = 0.0f;
                if (sy < 0.0f) sy = 0.0f;
                if (sx > (float)(src_w - 1)) sx = (float)(src_w - 1);
                if (sy > (float)(src_h - 1)) sy = (float)(src_h - 1);

                uint16_t sx_fp = (uint16_t)(sx * FP_ONE + 0.5f);
                uint16_t sy_fp = (uint16_t)(sy * FP_ONE + 0.5f);
                ctx->lut[idx] = ((uint32_t)sy_fp << 16) | sx_fp;
            } else {
                ctx->lut[idx] = LUT_OOB;
                oob_count++;
            }
#else
            // Legacy: nearest-neighbour (integer coords)
            int ix = (int)(sx + 0.5f);
            int iy = (int)(sy + 0.5f);
            if (ix >= 0 && ix < (int)src_w && iy >= 0 && iy < (int)src_h) {
                ctx->lut[idx] = ((uint32_t)(uint16_t)iy << 16) | (uint16_t)ix;
            } else {
                ctx->lut[idx] = LUT_OOB;
                oob_count++;
            }
#endif

#if CAM_ENH_AREA_AVERAGE
            // Compute local Jacobian magnitude to determine sample count.
            // J = max(|d(sx)/dx|, |d(sy)/dx|, |d(sx)/dy|, |d(sy)/dy|)
            // approximated via finite differences in the homography.
            if (ctx->sample_count) {
                uint8_t sc = 1;
                if (ctx->lut[idx] != LUT_OOB) {
                    // Partial derivatives via finite differences on the inverse map
                    float w_xp = Hi[6]*(xf+1.0f) + Hi[7]*yf + Hi[8];
                    float w_yp = Hi[6]*xf + Hi[7]*(yf+1.0f) + Hi[8];

                    float jac_max = 1.0f;
                    if (fabsf(w_xp) > 1e-10f) {
                        float sx_xp = (Hi[0]*(xf+1.0f) + Hi[1]*yf + Hi[2]) / w_xp;
                        float sy_xp = (Hi[3]*(xf+1.0f) + Hi[4]*yf + Hi[5]) / w_xp;
                        float dx_sx = fabsf(sx_xp - sx);
                        float dx_sy = fabsf(sy_xp - sy);
                        float j = dx_sx > dx_sy ? dx_sx : dx_sy;
                        if (j > jac_max) jac_max = j;
                    }
                    if (fabsf(w_yp) > 1e-10f) {
                        float sx_yp = (Hi[0]*xf + Hi[1]*(yf+1.0f) + Hi[2]) / w_yp;
                        float sy_yp = (Hi[3]*xf + Hi[4]*(yf+1.0f) + Hi[5]) / w_yp;
                        float dy_sx = fabsf(sx_yp - sx);
                        float dy_sy = fabsf(sy_yp - sy);
                        float j = dy_sx > dy_sy ? dy_sx : dy_sy;
                        if (j > jac_max) jac_max = j;
                    }

                    // jac_max ~1.0 → 1:1 mapping, use 1 sample
                    // jac_max >1.5 → minifying by >1.5x, use 2×2
                    // jac_max >2.5 → use 3×3
                    if (jac_max > 2.5f) sc = 9;       // 3×3 box
                    else if (jac_max > 1.5f) sc = 4;   // 2×2 box
                    else sc = 1;
                }
                ctx->sample_count[idx] = sc;
            }
#endif
        }
    }

    ESP_LOGI(TAG, "LUT built: %ux%u → %ux%u, OOB pixels: %lu"
#if CAM_ENH_BILINEAR
             " [bilinear FP12.4]"
#endif
#if CAM_ENH_AREA_AVERAGE
             " [area-avg]"
#endif
             ,
             src_w, src_h, dst_w, dst_h, (unsigned long)oob_count);
    return ctx;
}

// ---------------------------------------------------------------------------
// Destroy
// ---------------------------------------------------------------------------
void cam_transform_destroy(cam_transform_t ctx) {
    if (!ctx) return;
    if (ctx->lut) heap_caps_free(ctx->lut);
#if CAM_ENH_AREA_AVERAGE
    if (ctx->sample_count) heap_caps_free(ctx->sample_count);
#endif
    heap_caps_free(ctx);
}

// ---------------------------------------------------------------------------
// RGB565 pixel helpers for bilinear blending
// ---------------------------------------------------------------------------

// The OV3660 outputs big-endian RGB565.  On the little-endian ESP32 the bytes
// are swapped when read as uint16_t, so channel decomposition (R>>11 etc.)
// would extract garbage without byte-swapping first.  We swap on read,
// decompose/blend in standard RGB565, then swap the result back so the rest
// of the pipeline (diff engine, JPEG encoder) sees the same byte order.
static inline uint16_t cam_bswap16(uint16_t v) {
    return __builtin_bswap16(v);
}

#if CAM_ENH_BILINEAR
static inline uint16_t bilinear_rgb565(const uint16_t *src, uint16_t stride,
                                       uint16_t sx_fp, uint16_t sy_fp,
                                       uint16_t src_w, uint16_t src_h) {
    uint16_t ix = sx_fp >> FP_FRAC_BITS;
    uint16_t iy = sy_fp >> FP_FRAC_BITS;
    uint8_t  fx = sx_fp & FP_MASK;   // 0..15
    uint8_t  fy = sy_fp & FP_MASK;

    // Fast path: no fractional part → nearest-neighbour (no decomposition needed)
    if (fx == 0 && fy == 0) {
        return src[(size_t)iy * stride + ix];
    }

    // Clamp neighbouring pixel coordinates
    uint16_t ix1 = (ix + 1 < src_w) ? ix + 1 : ix;
    uint16_t iy1 = (iy + 1 < src_h) ? iy + 1 : iy;

    // Fetch 2×2 neighbourhood — byte-swap to standard RGB565 for decomposition
    uint16_t p00 = cam_bswap16(src[(size_t)iy  * stride + ix ]);
    uint16_t p10 = cam_bswap16(src[(size_t)iy  * stride + ix1]);
    uint16_t p01 = cam_bswap16(src[(size_t)iy1 * stride + ix ]);
    uint16_t p11 = cam_bswap16(src[(size_t)iy1 * stride + ix1]);

    // Decompose RGB565 channels and blend with 4-bit fractional weights.
    // Weight products: w00=(16-fx)*(16-fy), w10=fx*(16-fy), etc. — max 256.
    uint16_t w00 = (uint16_t)(FP_ONE - fx) * (FP_ONE - fy);
    uint16_t w10 = (uint16_t)fx * (FP_ONE - fy);
    uint16_t w01 = (uint16_t)(FP_ONE - fx) * fy;
    uint16_t w11 = (uint16_t)fx * fy;

    // Extract 5-bit R, 6-bit G, 5-bit B from each pixel
    uint32_t r = ((p00 >> 11) & 0x1F) * w00 + ((p10 >> 11) & 0x1F) * w10 +
                 ((p01 >> 11) & 0x1F) * w01 + ((p11 >> 11) & 0x1F) * w11;
    uint32_t g = ((p00 >>  5) & 0x3F) * w00 + ((p10 >>  5) & 0x3F) * w10 +
                 ((p01 >>  5) & 0x3F) * w01 + ((p11 >>  5) & 0x3F) * w11;
    uint32_t b = ((p00      ) & 0x1F) * w00 + ((p10      ) & 0x1F) * w10 +
                 ((p01      ) & 0x1F) * w01 + ((p11      ) & 0x1F) * w11;

    // Divide by 256 (= FP_ONE² = 16×16).
    r = (r + 128) >> 8;
    g = (g + 128) >> 8;
    b = (b + 128) >> 8;

    // Reconstruct and swap back to original byte order
    return cam_bswap16((uint16_t)((r << 11) | (g << 5) | b));
}
#endif

// ---------------------------------------------------------------------------
// Area-averaged sample (box filter)
// ---------------------------------------------------------------------------
#if CAM_ENH_AREA_AVERAGE
static inline uint16_t area_sample_rgb565(const uint16_t *src, uint16_t stride,
                                          float center_sx, float center_sy,
                                          uint8_t count,
                                          uint16_t src_w, uint16_t src_h) {
    // count: 1 = single bilinear sample, 4 = 2×2 box, 9 = 3×3 box
    // The box is centred on (center_sx, center_sy) with spacing of 0.5 src pixels.
    uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
    uint8_t n = 0;

    float start = (count == 9) ? -0.5f : (count == 4) ? -0.25f : 0.0f;
    float step  = (count == 9) ?  0.5f : (count == 4) ?  0.5f  : 0.0f;
    int side    = (count == 9) ?  3    : (count == 4) ?  2     : 1;

    for (int dy = 0; dy < side; dy++) {
        for (int dx = 0; dx < side; dx++) {
            float ssx = center_sx + start + dx * step;
            float ssy = center_sy + start + dy * step;

            // Clamp
            if (ssx < 0.0f) ssx = 0.0f;
            if (ssy < 0.0f) ssy = 0.0f;
            if (ssx >= (float)(src_w - 1)) ssx = (float)(src_w - 1);
            if (ssy >= (float)(src_h - 1)) ssy = (float)(src_h - 1);

            int ix = (int)(ssx + 0.5f);
            int iy = (int)(ssy + 0.5f);
            if (ix < 0) ix = 0;
            if (iy < 0) iy = 0;
            if (ix >= src_w) ix = src_w - 1;
            if (iy >= src_h) iy = src_h - 1;

            // Byte-swap to standard RGB565 for correct channel extraction
            uint16_t px = cam_bswap16(src[(size_t)iy * stride + ix]);
            r_sum += (px >> 11) & 0x1F;
            g_sum += (px >>  5) & 0x3F;
            b_sum += (px      ) & 0x1F;
            n++;
        }
    }

    uint16_t r = (uint16_t)((r_sum + n/2) / n);
    uint16_t g = (uint16_t)((g_sum + n/2) / n);
    uint16_t b = (uint16_t)((b_sum + n/2) / n);
    // Swap back to original byte order
    return cam_bswap16((uint16_t)((r << 11) | (g << 5) | b));
}
#endif

// ---------------------------------------------------------------------------
// Apply the LUT-based warp
// ---------------------------------------------------------------------------
void cam_transform_apply(cam_transform_t ctx,
                         const uint16_t *src_rgb565,
                         uint16_t *dst_rgb565) {
    if (!ctx) return;

    // Identity fast path — just memcpy (only valid when src==dst dimensions).
    if (ctx->identity && !ctx->lut) {
        size_t bytes = (size_t)ctx->dst_w * ctx->dst_h * 2;
        memcpy(dst_rgb565, src_rgb565, bytes);
        return;
    }

    const uint32_t *lut = ctx->lut;
    const size_t n = (size_t)ctx->dst_w * ctx->dst_h;
    const uint16_t sw = ctx->src_w;
    const uint16_t sh = ctx->src_h;

#if CAM_ENH_BILINEAR && CAM_ENH_AREA_AVERAGE
    const uint8_t *sc = ctx->sample_count;
    for (size_t i = 0; i < n; i++) {
        uint32_t entry = lut[i];
        if (entry == LUT_OOB) {
            dst_rgb565[i] = 0x0000;
            continue;
        }
        uint16_t sy_fp = (uint16_t)(entry >> 16);
        uint16_t sx_fp = (uint16_t)(entry & 0xFFFF);

        if (sc && sc[i] > 1) {
            // Area averaging: use multiple samples with the box filter.
            // Convert FP back to float for the centre position.
            float csx = (float)sx_fp / (float)FP_ONE;
            float csy = (float)sy_fp / (float)FP_ONE;
            dst_rgb565[i] = area_sample_rgb565(src_rgb565, sw, csx, csy,
                                               sc[i], sw, sh);
        } else {
            dst_rgb565[i] = bilinear_rgb565(src_rgb565, sw, sx_fp, sy_fp, sw, sh);
        }
    }

#elif CAM_ENH_BILINEAR
    for (size_t i = 0; i < n; i++) {
        uint32_t entry = lut[i];
        if (entry == LUT_OOB) {
            dst_rgb565[i] = 0x0000;
        } else {
            uint16_t sy_fp = (uint16_t)(entry >> 16);
            uint16_t sx_fp = (uint16_t)(entry & 0xFFFF);
            dst_rgb565[i] = bilinear_rgb565(src_rgb565, sw, sx_fp, sy_fp, sw, sh);
        }
    }

#else
    // Legacy nearest-neighbour path
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
#endif
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------
void cam_transform_get_size(cam_transform_t ctx, uint16_t *dst_w, uint16_t *dst_h) {
    if (ctx) {
        *dst_w = ctx->dst_w;
        *dst_h = ctx->dst_h;
    }
}

void cam_transform_get_src_size(cam_transform_t ctx, uint16_t *src_w, uint16_t *src_h) {
    if (ctx) {
        *src_w = ctx->src_w;
        *src_h = ctx->src_h;
    }
}

bool cam_transform_is_identity(cam_transform_t ctx) {
    return ctx ? ctx->identity : true;
}
