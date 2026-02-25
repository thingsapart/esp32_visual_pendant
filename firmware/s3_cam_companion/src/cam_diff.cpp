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

    // Swap R↔B in tile pixels before JPEG encoding (for BGR565 displays).
    bool      swap_rb;
    // Swap bytes of each 16-bit pixel before encoding (for LV_COLOR_16_SWAP)
    bool      swap_bytes;
    // Invert all pixel values (XOR 0xFFFF on each RGB565 word).
    bool      invert_colors;

    // Temporary tile pixel buffer for JPEG encoding (internal RAM for speed).
    // Stored as BGR888 (3 bytes/pixel) so that fmt2jpg(RGB888) receives
    // properly bit-replicated channels — avoids the chroma asymmetry caused
    // by the simple left-shift truncation in fmt2jpg(RGB565) that makes
    // near-white highlights encode as (248,252,248) instead of (255,255,255).
    uint8_t *tile_buf;

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
    // 3 bytes per pixel: BGR888 for fmt2jpg(PIXFORMAT_RGB888).
    size_t tile_bytes = (size_t)ctx->tile_w * ctx->tile_h * 3;
    ctx->tile_buf = (uint8_t *)heap_caps_malloc(tile_bytes,
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ctx->tile_buf) {
        // Fallback to PSRAM.
        ctx->tile_buf = (uint8_t *)heap_caps_malloc(tile_bytes, MALLOC_CAP_SPIRAM);
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
// Block-averaged SAD for a tile region.
//
// The tile is divided into DIFF_BLOCK_SIZE×DIFF_BLOCK_SIZE pixel blocks.
// For each block the average R, G, B values are computed from the current
// and previous frames separately.  The 8-bit-normalised max-channel delta
// between block averages is noise-gated (excess above DIFF_NOISE_FLOOR
// accumulated) and the total is divided by the number of blocks to give
// the mean gated-block-SAD for the tile.
//
// Why this beats per-pixel approaches for camera noise:
//   Per-pixel noise is zero-mean and statistically independent across pixels.
//   Averaging N² pixels reduces σ by 1/N.  With BLOCK_SIZE=4 and OV3660
//   per-pixel 5-bit noise σ ≈ 3–4 LSBs (= 24–32 on the 8-bit scale), the
//   block-average noise σ drops to ~7 for a single frame.
//
//   The diff compares two independent frames, so the delta noise is:
//     σ_delta = √2 × σ_block ≈ 10 on 8-bit scale (R/B channels).
//   DIFF_NOISE_FLOOR must exceed this (≥ 2σ) to reject noise reliably.
//
//   Real scene changes are spatially coherent: a moving object shifts all
//   pixels in the block in the same direction, so the block average shifts
//   just as much as any individual pixel.
//
// BLOCK_SIZE must divide tw and th evenly (ensured by tile grid config).
// ---------------------------------------------------------------------------
static inline uint16_t swap_rb565(uint16_t px) {
    // RGB565: [R4..R0 G5..G3] [G2..G0 B4..B0]
    uint16_t r = (px >> 11) & 0x1F;
    uint16_t g = (px >>  5) & 0x3F;
    uint16_t b = (px      ) & 0x1F;
    return (uint16_t)((b << 11) | (g << 5) | r);  // B in R slot, R in B slot
}

static inline uint16_t bswap16(uint16_t v) {
    return (uint16_t)(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
}

static uint32_t tile_mean_sad(const uint16_t *cur, const uint16_t *prev,
                               uint16_t img_w,
                               uint16_t tx, uint16_t ty,
                               uint16_t tw, uint16_t th)
{
    const uint16_t bs     = DIFF_BLOCK_SIZE;
    const uint32_t bsq    = (uint32_t)bs * bs;         // pixels per block
    const uint32_t num_bx = tw / bs;                   // blocks in X
    const uint32_t num_by = th / bs;                   // blocks in Y
    const uint32_t num_b  = num_bx * num_by;           // total blocks in tile

    uint32_t sad = 0;

    for (uint32_t by = 0; by < num_by; by++) {
        for (uint32_t bx = 0; bx < num_bx; bx++) {
            // Top-left corner of this block in the full image.
            const uint16_t ox = (uint16_t)(tx + bx * bs);
            const uint16_t oy = (uint16_t)(ty + by * bs);

            // Accumulate R, G, B sums (raw channel values) for cur and prev.
            uint32_t sr_c=0, sg_c=0, sb_c=0;
            uint32_t sr_p=0, sg_p=0, sb_p=0;

            for (uint16_t y = 0; y < bs; y++) {
                const uint16_t *cr = &cur [(oy + y) * img_w + ox];
                const uint16_t *pr = &prev[(oy + y) * img_w + ox];
                for (uint16_t x = 0; x < bs; x++) {
                    uint16_t c = cr[x], p = pr[x];
                    sr_c += (c >> 11) & 0x1F;   sg_c += (c >> 5) & 0x3F;   sb_c += c & 0x1F;
                    sr_p += (p >> 11) & 0x1F;   sg_p += (p >> 5) & 0x3F;   sb_p += p & 0x1F;
                }
            }

            // Block-average channel values (still in raw 5/6-bit domain).
            const uint32_t avg_r_c = sr_c / bsq,  avg_r_p = sr_p / bsq;
            const uint32_t avg_g_c = sg_c / bsq,  avg_g_p = sg_p / bsq;
            const uint32_t avg_b_c = sb_c / bsq,  avg_b_p = sb_p / bsq;

            // 8-bit normalise (R/B ×8, G ×4) and take max-channel delta.
            uint32_t dr = (uint32_t)abs((int)avg_r_c - (int)avg_r_p) << 3;
            uint32_t dg = (uint32_t)abs((int)avg_g_c - (int)avg_g_p) << 2;
            uint32_t db = (uint32_t)abs((int)avg_b_c - (int)avg_b_p) << 3;
            uint32_t dm = dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);

            // Noise gate: only accumulate excess above the floor.
            if (dm > DIFF_NOISE_FLOOR) {
                sad += (dm - DIFF_NOISE_FLOOR);
            }
        }
    }

    return sad / num_b;
}

// ---------------------------------------------------------------------------
// Extract tile pixels into a contiguous BGR888 buffer for JPEG encoding.
//
// Converts big-endian RGB565 (camera DMA byte order) to BGR888 using the
// SAME left-shift expansion that fmt2jpg(PIXFORMAT_RGB565) uses:
//   R8 = R5 << 3   (0-padded to 8 bits)
//   G8 = G6 << 2   (0-padded to 8 bits)
//   B8 = B5 << 3   (0-padded to 8 bits)
//
// This is intentional.  The alternative (bit-replication) breaks the
// natural colour neutrality of the sensor output: for a neutral highlight
// where the sensor outputs R5=31, G6=62, B5=31 (the natural "equal"
// relationship R5<<3 == G6<<2 == 248) bit-replication would give
// R8=B8=255 vs G8=251, introducing a purple tinge.  Left-shifting
// preserves R8 == G8 == B8 == 248 — neutral and consistent with what
// fmt2jpg(RGB565) and the non-enhanced direct-copy path produce.
//
// fmt2jpg(PIXFORMAT_RGB888) reverses the byte triplet internally (BGR→RGB)
// before passing to the JPEG encoder, so we write in BGR order here.
//
// If swap_rb is true the R and B slots are exchanged (for BGR-order displays).
// If invert_colors is true each channel byte is XOR-inverted.
// swap_bytes is not applicable to the RGB888 path.
// ---------------------------------------------------------------------------
static void extract_tile(const uint16_t *frame, uint16_t img_w,
                         uint16_t tx, uint16_t ty,
                         uint16_t tw, uint16_t th,
                         uint8_t *out, bool swap_rb, bool invert_colors) {
    for (uint16_t y = 0; y < th; y++) {
        const uint16_t *src = &frame[(ty + y) * img_w + tx];
        uint8_t        *dst = &out[(size_t)y * tw * 3];
        for (uint16_t x = 0; x < tw; x++) {
            // Camera frame is big-endian RGB565; bswap16 gives standard layout.
            uint16_t px = bswap16(src[x]);
            uint8_t r5 = (px >> 11) & 0x1Fu;
            uint8_t g6 = (px >>  5) & 0x3Fu;
            uint8_t b5 = (px      ) & 0x1Fu;

            // Left-shift expansion (matches fmt2jpg(PIXFORMAT_RGB565) behaviour).
            // Preserves R5<<3 == G6<<2 == B5<<3 channel neutrality for grays.
            uint8_t r8 = (uint8_t)(r5 << 3);
            uint8_t g8 = (uint8_t)(g6 << 2);
            uint8_t b8 = (uint8_t)(b5 << 3);

            if (invert_colors) { r8 ^= 0xFFu; g8 ^= 0xFFu; b8 ^= 0xFFu; }

            // fmt2jpg(PIXFORMAT_RGB888) expects BGR byte order.
            if (!swap_rb) {
                dst[x * 3    ] = b8;
                dst[x * 3 + 1] = g8;
                dst[x * 3 + 2] = r8;
            } else {
                // Swap R↔B so the JPEG encodes R in the B channel and vice-versa,
                // matching BGR-order displays after decode.
                dst[x * 3    ] = r8;
                dst[x * 3 + 1] = g8;
                dst[x * 3 + 2] = b8;
            }
        }
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
    // Track min/max mean-SAD across the grid so we can log a single
    // representative diagnostic line rather than one per tile.
    uint32_t min_sad = UINT32_MAX, max_sad = 0;

    for (uint8_t ty = 0; ty < ctx->tiles_y; ty++) {
        for (uint8_t tx = 0; tx < ctx->tiles_x; tx++) {
            uint8_t tile_idx = ty * ctx->tiles_x + tx;
            uint16_t px = tx * ctx->tile_w;
            uint16_t py = ty * ctx->tile_h;

            bool changed;
            if (is_keyframe) {
                changed = true;
            } else {
                uint32_t sad = tile_mean_sad(warped_frame, ctx->prev_frame,
                                             img_w, px, py,
                                             ctx->tile_w, ctx->tile_h);
                if (sad < min_sad) min_sad = sad;
                if (sad > max_sad) max_sad = sad;
                changed = sad > DIFF_TILE_MEAN_SAD_THRESH;
            }
            if (changed) {
                r->changed_bitmap[tile_idx / 8] |= (1 << (tile_idx % 8));
                r->num_changed++;

                // Extract tile pixels as BGR888 with proper bit-replication.
                extract_tile(warped_frame, img_w, px, py,
                             ctx->tile_w, ctx->tile_h, ctx->tile_buf,
                             ctx->swap_rb, ctx->invert_colors);

                // JPEG-encode the tile.
                uint8_t *jpeg_out = NULL;
                size_t   jpeg_len = 0;
                bool ok = fmt2jpg(ctx->tile_buf,
                                  ctx->tile_w * ctx->tile_h * 3,
                                  ctx->tile_w, ctx->tile_h,
                                  PIXFORMAT_RGB888,
                                  jpeg_quality,
                                  &jpeg_out, &jpeg_len);
                if (ok && jpeg_out) {
                    // Move the JPEG buffer from internal RAM to PSRAM to avoid
                    // exhausting the limited ~120 KB internal heap when all 48
                    // tiles are encoded in a keyframe.
                    uint8_t *psram_buf = (uint8_t *)heap_caps_malloc(
                        jpeg_len, MALLOC_CAP_SPIRAM);
                    if (psram_buf) {
                        memcpy(psram_buf, jpeg_out, jpeg_len);
                        free(jpeg_out);
                        jpeg_out = psram_buf;
                    }
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

    // Save the post-transform, pre-JPEG RGB565 frame as the previous reference.
    // This ensures both the current and previous buffers are in the same
    // colour space (raw sensor → perspective-warped RGB565) so the diff is
    // not confused by JPEG codec rounding in the transmitted tiles.
    size_t frame_bytes = (size_t)img_w * img_h * sizeof(uint16_t);
    memcpy(ctx->prev_frame, warped_frame, frame_bytes);
    ctx->has_prev = true;

    if (is_keyframe) {
        ESP_LOGD(TAG, "Diff: KEYFRAME, %d/%d tiles",
                 r->num_changed, ctx->tiles_x * ctx->tiles_y);
    } else {
        ESP_LOGD(TAG, "Diff: DIFF %d/%d tiles changed | gated-SAD min=%lu max=%lu floor=%d thresh=%d",
                 r->num_changed, ctx->tiles_x * ctx->tiles_y,
                 (unsigned long)min_sad, (unsigned long)max_sad,
                 DIFF_NOISE_FLOOR, DIFF_TILE_MEAN_SAD_THRESH);
    }
}

// ---------------------------------------------------------------------------
// Access result
// ---------------------------------------------------------------------------
const cam_diff_result_t *cam_diff_get_result(cam_diff_t ctx) {
    return ctx ? &ctx->result : NULL;
}

// ---------------------------------------------------------------------------
// Runtime control
// ---------------------------------------------------------------------------
void cam_diff_set_swap_rb(cam_diff_t ctx, bool swap_rb) {
    if (ctx) ctx->swap_rb = swap_rb;
}

void cam_diff_set_swap_bytes(cam_diff_t ctx, bool swap_bytes) {
    if (ctx) ctx->swap_bytes = swap_bytes;
}

void cam_diff_set_invert_colors(cam_diff_t ctx, bool invert_colors) {
    if (ctx) ctx->invert_colors = invert_colors;
}

// ---------------------------------------------------------------------------
// Free tile JPEG buffers
// ---------------------------------------------------------------------------
void cam_diff_free_tiles(cam_diff_t ctx) {
    if (!ctx) return;
    for (int i = 0; i < CAM_MAX_TILES; i++) {
        if (ctx->result.tiles[i].jpeg_buf) {
            heap_caps_free(ctx->result.tiles[i].jpeg_buf);
            ctx->result.tiles[i].jpeg_buf = NULL;
            ctx->result.tiles[i].jpeg_len = 0;
        }
    }
}
