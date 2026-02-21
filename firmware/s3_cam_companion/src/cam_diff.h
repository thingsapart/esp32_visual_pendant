// cam_diff.h — Block-wise frame diff and tile JPEG encoding
//
// Compares the current warped RGB565 frame against the previous one at the
// tile level.  Tiles are independently JPEG-encoded so the pendant can
// decode and composite them individually.

#ifndef CAM_DIFF_H
#define CAM_DIFF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum tiles in each dimension.
#define CAM_MAX_TILES_X  8
#define CAM_MAX_TILES_Y  8
#define CAM_MAX_TILES    (CAM_MAX_TILES_X * CAM_MAX_TILES_Y)

// Per-tile encoded data.
typedef struct {
    uint8_t *jpeg_buf;    // JPEG data (heap-allocated)
    size_t   jpeg_len;    // JPEG byte count
} cam_tile_data_t;

// Result of a diff operation.
typedef struct {
    uint16_t img_w, img_h;
    uint8_t  tiles_x, tiles_y;
    uint16_t tile_w, tile_h;
    bool     is_keyframe;

    // Which tiles changed (bit N = tile N).
    uint8_t  changed_bitmap[CAM_MAX_TILES / 8];
    uint8_t  num_changed;

    // JPEG data for each changed tile.
    cam_tile_data_t tiles[CAM_MAX_TILES];
} cam_diff_result_t;

// Opaque diff context (holds previous frame + tile buffers in PSRAM).
typedef struct cam_diff_ctx *cam_diff_t;

// Create the diff context.
// img_w, img_h: warped image dimensions.
// tiles_x, tiles_y: tile grid.
cam_diff_t cam_diff_create(uint16_t img_w, uint16_t img_h,
                           uint8_t tiles_x, uint8_t tiles_y);

void cam_diff_destroy(cam_diff_t ctx);

// Process a new warped frame.
// - Compares against the previous frame to find changed tiles.
// - JPEG-encodes each changed tile.
// - If force_keyframe is true, all tiles are treated as changed.
//
// After this call, cam_diff_get_result() returns the result.
// The JPEG buffers in the result are valid until the next process() call.
void cam_diff_process(cam_diff_t ctx,
                      const uint16_t *warped_frame,
                      uint16_t img_w, uint16_t img_h,
                      uint8_t jpeg_quality,
                      uint8_t diff_threshold,
                      bool force_keyframe);

// Get the result of the last diff.
const cam_diff_result_t *cam_diff_get_result(cam_diff_t ctx);

// Free the JPEG tile buffers from the last result.
void cam_diff_free_tiles(cam_diff_t ctx);

#ifdef __cplusplus
}
#endif

#endif // CAM_DIFF_H
