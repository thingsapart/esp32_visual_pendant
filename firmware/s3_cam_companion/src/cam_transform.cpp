// Minimal cam_transform.cpp — nearest-neighbour LUT implementation
//
// This file intentionally removes the previous "enhanced processing"
// implementation (bilinear fixed-point interpolation, area averaging, etc.)
// while keeping the public API and configuration/defines untouched. The
// implementation below builds a simple inverse-homography LUT that maps each
// output pixel to an integer source coordinate (nearest neighbour).

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

// Sentinel for out-of-bounds LUT entries.
#define LUT_OOB  0xFFFFFFFFU

typedef struct cam_transform_ctx {
    uint16_t src_w, src_h;
    uint16_t dst_w, dst_h;
    bool     identity;
    uint32_t *lut; // nearest-neighbour entries: (src_y << 16) | src_x
} cam_transform_ctx_t;

// Invert a 3×3 matrix; returns false if singular
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

    // Detect identity
    static const float I[9] = {1,0,0, 0,1,0, 0,0,1};
    ctx->identity = true;
    for (int i = 0; i < 9; i++) if (fabsf(H[i] - I[i]) > 1e-5f) { ctx->identity = false; break; }

    if (ctx->identity && src_w == dst_w && src_h == dst_h) {
        ESP_LOGI(TAG, "Identity transform — LUT skipped");
        ctx->lut = NULL;
        return ctx;
    }

    size_t lut_entries = (size_t)dst_w * dst_h;
    ctx->lut = (uint32_t *)heap_caps_malloc(lut_entries * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!ctx->lut) {
        ESP_LOGE(TAG, "Failed to alloc LUT (%zu bytes)", lut_entries * sizeof(uint32_t));
        heap_caps_free(ctx);
        return NULL;
    }

    // If caller provided identity but dims differ, substitute scale homography
    float H_eff[9];
    if (ctx->identity && (src_w != dst_w || src_h != dst_h)) {
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
            if (x < src_w && y < src_h) ctx->lut[i] = ((uint32_t)y << 16) | x;
            else ctx->lut[i] = LUT_OOB;
        }
        return ctx;
    }

    // Build LUT entries.  Two modes:
    //  - Nearest-neighbour: store integer src coords in upper/lower 16 bits.
    //  - Bilinear (fixed 12.4): store sub-pixel coords in 12.4 fixed point
    //    packed as (y_fixed << 16) | x_fixed.  Fractional bits = 4.
    uint32_t oob_count = 0;
    const int FP_SHIFT = 4; // fractional bits for bilinear
    const int FP_SCALE = (1 << FP_SHIFT);
#if CAM_ENH_BILINEAR
    for (uint16_t y = 0; y < dst_h; y++) {
        for (uint16_t x = 0; x < dst_w; x++) {
            float xf = (float)x;
            float yf = (float)y;
            float w = Hi[6]*xf + Hi[7]*yf + Hi[8];
            size_t idx = (size_t)y * dst_w + x;
            if (fabsf(w) < 1e-10f) { ctx->lut[idx] = LUT_OOB; oob_count++; continue; }
            float inv_w = 1.0f / w;
            float sx = (Hi[0]*xf + Hi[1]*yf + Hi[2]) * inv_w;
            float sy = (Hi[3]*xf + Hi[4]*yf + Hi[5]) * inv_w;
            // Convert to 12.4 fixed point and clamp to 0..(src-1).15
            int x_fixed = (int)floorf(sx * FP_SCALE);
            int y_fixed = (int)floorf(sy * FP_SCALE);
            // integer part clamp
            int ix = x_fixed >> FP_SHIFT;
            int iy = y_fixed >> FP_SHIFT;
            if (ix >= 0 && ix < (int)src_w && iy >= 0 && iy < (int)src_h) {
                ctx->lut[idx] = ((uint32_t)(uint16_t)y_fixed << 16) | (uint16_t)x_fixed;
            } else { ctx->lut[idx] = LUT_OOB; oob_count++; }
        }
    }
#else
    for (uint16_t y = 0; y < dst_h; y++) {
        for (uint16_t x = 0; x < dst_w; x++) {
            float xf = (float)x;
            float yf = (float)y;
            float w = Hi[6]*xf + Hi[7]*yf + Hi[8];
            size_t idx = (size_t)y * dst_w + x;
            if (fabsf(w) < 1e-10f) { ctx->lut[idx] = LUT_OOB; oob_count++; continue; }
            float inv_w = 1.0f / w;
            float sx = (Hi[0]*xf + Hi[1]*yf + Hi[2]) * inv_w;
            float sy = (Hi[3]*xf + Hi[4]*yf + Hi[5]) * inv_w;
            int ix = (int) (sx + 0.5f);
            int iy = (int) (sy + 0.5f);
            if (ix >= 0 && ix < (int)src_w && iy >= 0 && iy < (int)src_h) ctx->lut[idx] = ((uint32_t)(uint16_t)iy << 16) | (uint16_t)ix;
            else { ctx->lut[idx] = LUT_OOB; oob_count++; }
        }
    }
#endif

    // Diagnostic: log mapping for centre output pixel to help debug scaling.
    {
        int cx = dst_w / 2;
        int cy = dst_h / 2;
        float xf = (float)cx;
        float yf = (float)cy;
        float w = Hi[6]*xf + Hi[7]*yf + Hi[8];
        if (fabsf(w) >= 1e-10f) {
            float inv_w = 1.0f / w;
            float sx = (Hi[0]*xf + Hi[1]*yf + Hi[2]) * inv_w;
            float sy = (Hi[3]*xf + Hi[4]*yf + Hi[5]) * inv_w;
            ESP_LOGI(TAG, "LUT built: %ux%u → %ux%u, OOB pixels: %lu, centre dst(%d,%d) -> src(%.3f,%.3f)",
                     src_w, src_h, dst_w, dst_h, (unsigned long)oob_count, cx, cy, sx, sy);
        } else {
            ESP_LOGI(TAG, "LUT built: %ux%u → %ux%u, OOB pixels: %lu", src_w, src_h, dst_w, dst_h, (unsigned long)oob_count);
        }
    }
    return ctx;
}

void cam_transform_destroy(cam_transform_t ctx) {
    if (!ctx) return;
    if (ctx->lut) heap_caps_free(ctx->lut);
    heap_caps_free(ctx);
}

void cam_transform_apply(cam_transform_t ctx,
                         const uint16_t *src_rgb565,
                         uint16_t *dst_rgb565) {
    if (!ctx) return;
    if (ctx->identity && !ctx->lut) {
        size_t bytes = (size_t)ctx->dst_w * ctx->dst_h * 2;
        memcpy(dst_rgb565, src_rgb565, bytes);
        return;
    }
    const uint32_t *lut = ctx->lut;
    const size_t n = (size_t)ctx->dst_w * ctx->dst_h;
    const uint16_t sw = ctx->src_w;
#if CAM_ENH_BILINEAR
    // Bilinear sampling using 12.4 fixed-point LUT entries packed as
    // (y_fixed << 16) | x_fixed where low 4 bits are fractional.
    for (size_t i = 0; i < n; i++) {
        uint32_t entry = lut[i];
        if (entry == LUT_OOB) { dst_rgb565[i] = 0x0000; continue; }
        uint16_t xfix = (uint16_t)(entry & 0xFFFF);
        uint16_t yfix = (uint16_t)(entry >> 16);
        int ix = xfix >> 4;
        int iy = yfix >> 4;
        int fx = xfix & 0xF;
        int fy = yfix & 0xF;
        // Neighbour coordinates
        int ix1 = (ix + 1 < (int)ctx->src_w) ? ix + 1 : ix;
        int iy1 = (iy + 1 < (int)ctx->src_h) ? iy + 1 : iy;
        // Load four sample pixels (camera provides big-endian RGB565; swap
        // to native order for component extraction)
        uint16_t p00 = __builtin_bswap16(src_rgb565[(size_t)iy * sw + ix]);
        uint16_t p01 = __builtin_bswap16(src_rgb565[(size_t)iy * sw + ix1]);
        uint16_t p10 = __builtin_bswap16(src_rgb565[(size_t)iy1 * sw + ix]);
        uint16_t p11 = __builtin_bswap16(src_rgb565[(size_t)iy1 * sw + ix1]);
        // Extract raw components in native bit depth (R:5, G:6, B:5)
        int r00 = (p00 >> 11) & 0x1F; int g00 = (p00 >> 5) & 0x3F; int b00 = p00 & 0x1F;
        int r01 = (p01 >> 11) & 0x1F; int g01 = (p01 >> 5) & 0x3F; int b01 = p01 & 0x1F;
        int r10 = (p10 >> 11) & 0x1F; int g10 = (p10 >> 5) & 0x3F; int b10 = p10 & 0x1F;
        int r11 = (p11 >> 11) & 0x1F; int g11 = (p11 >> 5) & 0x3F; int b11 = p11 & 0x1F;

        // Weights (0..16). Final divisor is 256 (16*16).
        int wx0 = 16 - fx; int wx1 = fx;
        int wy0 = 16 - fy; int wy1 = fy;
        int wr00 = wx0 * wy0;
        int wr01 = wx1 * wy0;
        int wr10 = wx0 * wy1;
        int wr11 = wx1 * wy1;

        // Compute interpolated channels in native ranges using integer math.
        int r = (r00 * wr00 + r01 * wr01 + r10 * wr10 + r11 * wr11 + 128) >> 8; // 0..31
        int g = (g00 * wr00 + g01 * wr01 + g10 * wr10 + g11 * wr11 + 128) >> 8; // 0..63
        int b = (b00 * wr00 + b01 * wr01 + b10 * wr10 + b11 * wr11 + 128) >> 8; // 0..31

        // Pack back to RGB565 (native bit depths) and convert to big-endian
        uint16_t outpix = (uint16_t)(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
        dst_rgb565[i] = __builtin_bswap16(outpix);
    }
#else
    for (size_t i = 0; i < n; i++) {
        uint32_t entry = lut[i];
        if (entry == LUT_OOB) dst_rgb565[i] = 0x0000;
        else {
            uint16_t sy = (uint16_t)(entry >> 16);
            uint16_t sx = (uint16_t)(entry & 0xFFFF);
            dst_rgb565[i] = src_rgb565[(size_t)sy * sw + sx];
        }
    }
#endif
}

void cam_transform_get_size(cam_transform_t ctx, uint16_t *dst_w, uint16_t *dst_h) {
    if (ctx) { *dst_w = ctx->dst_w; *dst_h = ctx->dst_h; }
}

void cam_transform_get_src_size(cam_transform_t ctx, uint16_t *src_w, uint16_t *src_h) {
    if (ctx) { *src_w = ctx->src_w; *src_h = ctx->src_h; }
}

bool cam_transform_is_identity(cam_transform_t ctx) {
    return ctx ? ctx->identity : true;
}
