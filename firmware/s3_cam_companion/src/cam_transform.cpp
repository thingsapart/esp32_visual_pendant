// cam_transform.cpp — Perspective transform via PSRAM-resident LUT

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

// Each LUT entry stores the source pixel coordinate (packed).
// If the source coordinate is out-of-bounds, we store 0xFFFF.
#define LUT_OOB  0xFFFFFFFFU

typedef struct cam_transform_ctx {
    uint16_t src_w, src_h;
    uint16_t dst_w, dst_h;
    bool     identity;
    // LUT: dst_w × dst_h entries.
    // Each entry is a 32-bit value: (src_y << 16) | src_x
    // Stored in PSRAM.
    uint32_t *lut;
} cam_transform_ctx_t;

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

    // Compute inverse homography: H_inv maps dst → src.
    // We invert the 3×3 matrix.
    float det = H[0] * (H[4]*H[8] - H[5]*H[7])
              - H[1] * (H[3]*H[8] - H[5]*H[6])
              + H[2] * (H[3]*H[7] - H[4]*H[6]);

    if (fabsf(det) < 1e-10f) {
        ESP_LOGW(TAG, "Singular homography — using identity");
        for (size_t i = 0; i < lut_entries; i++) {
            uint16_t y = (uint16_t)(i / dst_w);
            uint16_t x = (uint16_t)(i % dst_w);
            if (x < src_w && y < src_h)
                ctx->lut[i] = ((uint32_t)y << 16) | x;
            else
                ctx->lut[i] = LUT_OOB;
        }
        return ctx;
    }

    float inv_det = 1.0f / det;
    float Hi[9];
    Hi[0] = (H[4]*H[8] - H[5]*H[7]) * inv_det;
    Hi[1] = (H[2]*H[7] - H[1]*H[8]) * inv_det;
    Hi[2] = (H[1]*H[5] - H[2]*H[4]) * inv_det;
    Hi[3] = (H[5]*H[6] - H[3]*H[8]) * inv_det;
    Hi[4] = (H[0]*H[8] - H[2]*H[6]) * inv_det;
    Hi[5] = (H[2]*H[3] - H[0]*H[5]) * inv_det;
    Hi[6] = (H[3]*H[7] - H[4]*H[6]) * inv_det;
    Hi[7] = (H[1]*H[6] - H[0]*H[7]) * inv_det;
    Hi[8] = (H[0]*H[4] - H[1]*H[3]) * inv_det;

    // Pre-compute for every output pixel.
    uint32_t oob_count = 0;
    for (uint16_t y = 0; y < dst_h; y++) {
        for (uint16_t x = 0; x < dst_w; x++) {
            float xf = (float)x;
            float yf = (float)y;
            float w  = Hi[6]*xf + Hi[7]*yf + Hi[8];
            if (fabsf(w) < 1e-10f) {
                ctx->lut[(size_t)y * dst_w + x] = LUT_OOB;
                oob_count++;
                continue;
            }
            float inv_w = 1.0f / w;
            float sx = (Hi[0]*xf + Hi[1]*yf + Hi[2]) * inv_w;
            float sy = (Hi[3]*xf + Hi[4]*yf + Hi[5]) * inv_w;

            // Nearest-neighbour sampling
            int ix = (int)(sx + 0.5f);
            int iy = (int)(sy + 0.5f);

            if (ix >= 0 && ix < (int)src_w && iy >= 0 && iy < (int)src_h) {
                ctx->lut[(size_t)y * dst_w + x] =
                    ((uint32_t)(uint16_t)iy << 16) | (uint16_t)ix;
            } else {
                ctx->lut[(size_t)y * dst_w + x] = LUT_OOB;
                oob_count++;
            }
        }
    }

    ESP_LOGI(TAG, "LUT built: %ux%u → %ux%u, OOB pixels: %lu",
             src_w, src_h, dst_w, dst_h, (unsigned long)oob_count);
    return ctx;
}

// ---------------------------------------------------------------------------
// Destroy
// ---------------------------------------------------------------------------
void cam_transform_destroy(cam_transform_t ctx) {
    if (!ctx) return;
    if (ctx->lut) heap_caps_free(ctx->lut);
    heap_caps_free(ctx);
}

// ---------------------------------------------------------------------------
// Apply the LUT-based warp
// ---------------------------------------------------------------------------
void cam_transform_apply(cam_transform_t ctx,
                         const uint16_t *src_rgb565,
                         uint16_t *dst_rgb565) {
    if (!ctx) return;

    // Identity fast path — just memcpy.
    if (ctx->identity && !ctx->lut) {
        size_t bytes = (size_t)ctx->dst_w * ctx->dst_h * 2;
        memcpy(dst_rgb565, src_rgb565, bytes);
        return;
    }

    const uint32_t *lut = ctx->lut;
    const size_t n = (size_t)ctx->dst_w * ctx->dst_h;
    const uint16_t sw = ctx->src_w;

    for (size_t i = 0; i < n; i++) {
        uint32_t entry = lut[i];
        if (entry == LUT_OOB) {
            dst_rgb565[i] = 0x0000;  // Black for out-of-bounds
        } else {
            uint16_t sy = (uint16_t)(entry >> 16);
            uint16_t sx = (uint16_t)(entry & 0xFFFF);
            dst_rgb565[i] = src_rgb565[(size_t)sy * sw + sx];
        }
    }
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

bool cam_transform_is_identity(cam_transform_t ctx) {
    return ctx ? ctx->identity : true;
}
