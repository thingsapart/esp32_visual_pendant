// cam_diff.cpp — Block-wise diff engine + per-tile JPEG encoding

#include "cam_diff.h"
#include "app_log.h"
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include "img_converters.h"  // from esp32-camera: fmt2jpg()

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

static const char *TAG = "cam_diff";

struct cam_diff_ctx {
    uint16_t img_w, img_h;
    uint8_t  tiles_x, tiles_y;
    uint16_t tile_w, tile_h;

    // Previous warped frame (PSRAM, RGB565).
    uint16_t *prev_frame;
    bool      has_prev;

    // Temporary tile pixel buffer for JPEG encoding (internal RAM for speed).
    uint16_t *tile_buf;

    cam_diff_result_t result;
};

// ---------------------------------------------------------------------------
// Create / Destroy
// ---------------------------------------------------------------------------
cam_diff_t cam_diff_create(uint16_t img_w, uint16_t img_h,
                           uint8_t tiles_x, uint8_t tiles_y) {
    cam_diff_ctx *ctx = (cam_diff_ctx *)
        heap_caps_calloc(1, sizeof(cam_diff_ctx), MALLOC_CAP_DEFAULT);
    if (!ctx) return NULL;

    ctx->img_w   = img_w;
    ctx->img_h   = img_h;
    ctx->tiles_x = tiles_x;
    ctx->tiles_y = tiles_y;
    ctx->tile_w  = img_w / tiles_x;
    ctx->tile_h  = img_h / tiles_y;
    ctx->has_prev = false;

    // Allocate prev frame in PSRAM.
    size_t frame_bytes = (size_t)img_w * img_h * sizeof(uint16_t);
    ctx->prev_frame = (uint16_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM);
    if (!ctx->prev_frame) {
        ESP_LOGE(TAG, "Failed to alloc prev_frame (%zu bytes)", frame_bytes);
        heap_caps_free(ctx);
        return NULL;
    }
    memset(ctx->prev_frame, 0, frame_bytes);

    // Tile buffer for extracting tile pixels (internal RAM preferred for speed).
    size_t tile_bytes = (size_t)ctx->tile_w * ctx->tile_h * sizeof(uint16_t);
    ctx->tile_buf = (uint16_t *)heap_caps_malloc(tile_bytes,
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ctx->tile_buf) {
        // Fallback to PSRAM.
        ctx->tile_buf = (uint16_t *)heap_caps_malloc(tile_bytes, MALLOC_CAP_SPIRAM);
    }
    if (!ctx->tile_buf) {
        ESP_LOGE(TAG, "Failed to alloc tile_buf");
        heap_caps_free(ctx->prev_frame);
        heap_caps_free(ctx);
        return NULL;
    }

    memset(&ctx->result, 0, sizeof(ctx->result));

    ESP_LOGI(TAG, "Diff engine: %ux%u, %dx%d tiles of %ux%u",
             img_w, img_h, tiles_x, tiles_y, ctx->tile_w, ctx->tile_h);
    return ctx;
}

void cam_diff_destroy(cam_diff_t ctx) {
    if (!ctx) return;
    cam_diff_free_tiles(ctx);
    if (ctx->prev_frame) heap_caps_free(ctx->prev_frame);
    if (ctx->tile_buf) heap_caps_free(ctx->tile_buf);
    heap_caps_free(ctx);
}

// ---------------------------------------------------------------------------
// Compare a single tile region for changes.
//
// RGB565 pixel: RRRRRGGG GGGBBBBB
// We compare per-channel with a threshold.  If enough pixels differ,
// the tile is marked as changed.
// ---------------------------------------------------------------------------
static inline int abs_diff_r(uint16_t a, uint16_t b) {
    return abs((int)((a >> 11) & 0x1F) - (int)((b >> 11) & 0x1F));
}
static inline int abs_diff_g(uint16_t a, uint16_t b) {
    return abs((int)((a >> 5) & 0x3F) - (int)((b >> 5) & 0x3F));
}
static inline int abs_diff_b(uint16_t a, uint16_t b) {
    return abs((int)(a & 0x1F) - (int)(b & 0x1F));
}

static bool tile_changed(const uint16_t *cur, const uint16_t *prev,
                         uint16_t img_w,
                         uint16_t tx, uint16_t ty,
                         uint16_t tw, uint16_t th,
                         uint8_t threshold) {
    // 5-bit R/B threshold, 6-bit G threshold (scaled).
    int thr_rb = threshold >> 3;  // Map 0–255 → 0–31
    int thr_g  = threshold >> 2;  // Map 0–255 → 0–63
    if (thr_rb < 1) thr_rb = 1;
    if (thr_g  < 1) thr_g  = 1;

    // Count changed pixels.  If >5% differ, tile is changed.
    int total   = (int)tw * th;
    int changed = 0;
    int max_unchanged = total - (total / 20);  // Early exit at 5%

    for (uint16_t y = 0; y < th; y++) {
        const uint16_t *cr = &cur[(ty + y) * img_w + tx];
        const uint16_t *pr = &prev[(ty + y) * img_w + tx];
        for (uint16_t x = 0; x < tw; x++) {
            if (abs_diff_r(cr[x], pr[x]) > thr_rb ||
                abs_diff_g(cr[x], pr[x]) > thr_g  ||
                abs_diff_b(cr[x], pr[x]) > thr_rb) {
                changed++;
                if (changed > total - max_unchanged) return true;
            }
        }
    }
    return changed > (total / 20);
}

// ---------------------------------------------------------------------------
// Extract tile pixels into a contiguous buffer for JPEG encoding.
// ---------------------------------------------------------------------------
static void extract_tile(const uint16_t *frame, uint16_t img_w,
                         uint16_t tx, uint16_t ty,
                         uint16_t tw, uint16_t th,
                         uint16_t *out) {
    for (uint16_t y = 0; y < th; y++) {
        memcpy(&out[y * tw],
               &frame[(ty + y) * img_w + tx],
               tw * sizeof(uint16_t));
    }
}

// ---------------------------------------------------------------------------
// Process: diff + encode
// ---------------------------------------------------------------------------
void cam_diff_process(cam_diff_t ctx,
                      const uint16_t *warped_frame,
                      uint16_t img_w, uint16_t img_h,
                      uint8_t jpeg_quality,
                      uint8_t diff_threshold,
                      bool force_keyframe) {
    // Free any previous tile JPEG buffers.
    cam_diff_free_tiles(ctx);

    cam_diff_result_t *r = &ctx->result;
    memset(r, 0, sizeof(*r));
    r->img_w   = img_w;
    r->img_h   = img_h;
    r->tiles_x = ctx->tiles_x;
    r->tiles_y = ctx->tiles_y;
    r->tile_w  = ctx->tile_w;
    r->tile_h  = ctx->tile_h;

    bool is_keyframe = force_keyframe || !ctx->has_prev;
    r->is_keyframe = is_keyframe;

    // Determine which tiles changed.
    for (uint8_t ty = 0; ty < ctx->tiles_y; ty++) {
        for (uint8_t tx = 0; tx < ctx->tiles_x; tx++) {
            uint8_t tile_idx = ty * ctx->tiles_x + tx;
            uint16_t px = tx * ctx->tile_w;
            uint16_t py = ty * ctx->tile_h;

            bool changed = is_keyframe ||
                           tile_changed(warped_frame, ctx->prev_frame,
                                        img_w, px, py,
                                        ctx->tile_w, ctx->tile_h,
                                        diff_threshold);
            if (changed) {
                r->changed_bitmap[tile_idx / 8] |= (1 << (tile_idx % 8));
                r->num_changed++;

                // Extract tile pixels.
                extract_tile(warped_frame, img_w, px, py,
                             ctx->tile_w, ctx->tile_h, ctx->tile_buf);

                // JPEG-encode the tile.
                uint8_t *jpeg_out = NULL;
                size_t   jpeg_len = 0;
                bool ok = fmt2jpg((uint8_t *)ctx->tile_buf,
                                  ctx->tile_w * ctx->tile_h * 2,
                                  ctx->tile_w, ctx->tile_h,
                                  PIXFORMAT_RGB565,
                                  jpeg_quality,
                                  &jpeg_out, &jpeg_len);
                if (ok && jpeg_out) {
                    r->tiles[tile_idx].jpeg_buf = jpeg_out;
                    r->tiles[tile_idx].jpeg_len = jpeg_len;
                } else {
                    ESP_LOGW(TAG, "JPEG encode failed for tile %d", tile_idx);
                    r->changed_bitmap[tile_idx / 8] &= ~(1 << (tile_idx % 8));
                    r->num_changed--;
                }
            }
        }
    }

    // Save current frame as previous.
    size_t frame_bytes = (size_t)img_w * img_h * sizeof(uint16_t);
    memcpy(ctx->prev_frame, warped_frame, frame_bytes);
    ctx->has_prev = true;

    ESP_LOGD(TAG, "Diff: %s, %d/%d tiles changed",
             is_keyframe ? "KEYFRAME" : "DIFF",
             r->num_changed, ctx->tiles_x * ctx->tiles_y);
}

// ---------------------------------------------------------------------------
// Access result
// ---------------------------------------------------------------------------
const cam_diff_result_t *cam_diff_get_result(cam_diff_t ctx) {
    return ctx ? &ctx->result : NULL;
}

// ---------------------------------------------------------------------------
// Free tile JPEG buffers
// ---------------------------------------------------------------------------
void cam_diff_free_tiles(cam_diff_t ctx) {
    if (!ctx) return;
    for (int i = 0; i < CAM_MAX_TILES; i++) {
        if (ctx->result.tiles[i].jpeg_buf) {
            free(ctx->result.tiles[i].jpeg_buf);
            ctx->result.tiles[i].jpeg_buf = NULL;
            ctx->result.tiles[i].jpeg_len = 0;
        }
    }
}
