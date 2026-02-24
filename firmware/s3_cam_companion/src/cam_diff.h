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

// ---------------------------------------------------------------------------
// Diff sensitivity thresholds
// ---------------------------------------------------------------------------
// Block-averaged SAD model:
//
//   The image tile is divided into DIFF_BLOCK_SIZE × DIFF_BLOCK_SIZE pixel
//   blocks.  R, G, B are averaged across each block separately; the block-
//   average colours are then compared and their max-channel delta (8-bit
//   normalised) is accumulated with a noise gate.
//
// Why block-averaging works:
//   Per-pixel sensor noise is zero-mean and independent.  Averaging N² pixels
//   reduces the noise standard-deviation by 1/N.  With BLOCK_SIZE=4 and
//   OV3660 noise of ±3–4 LSBs in 5-bit channels (= ±24–32 on 8-bit scale),
//   the block-average noise drops to ±6–8 on the 8-bit scale.
//
//   IMPORTANT: the diff computes the *delta* between two independent frames.
//   The inter-frame delta has √2× the noise of a single frame, because both
//   the current and previous frame contribute independent noise:
//     σ_delta = √(σ_cur² + σ_prev²) = √2 × σ_block
//   For R/B channels (5-bit, ×8 to 8-bit): σ_delta ≈ √2 × 7 ≈ 10 on 8-bit.
//   For G channel  (6-bit, ×4 to 8-bit): σ_delta ≈ √2 × 3.5 ≈ 5 on 8-bit.
//
//   The max-of-3-channels metric pushes the effective noise even higher:
//   with σ_R/B ≈ 10, the floor must be ≥ 2σ (≈ 20) to reject noise reliably.
//
//   Real motion is spatially coherent: a moving object shifts all pixels in
//   a block the same direction, so the block average shifts just as much as
//   any individual pixel.
//
// DIFF_BLOCK_SIZE: side length of averaging block in pixels.
//   Powers of 2 allow the division to be replaced by a right-shift.
//   4 → 16 pixels/block, noise ÷4.   8 → 64 pixels/block, noise ÷8 (less
//   spatial resolution — may miss fine tool-tip movement).
#define DIFF_BLOCK_SIZE          4    // must divide tile_w and tile_h evenly

// DIFF_NOISE_FLOOR: block-average max-channel deltas at or below this value
//   are treated as zero.  The floor must exceed the inter-frame delta noise
//   (σ_delta ≈ 10 on 8-bit scale for R/B channels with BLOCK_SIZE=4).
//   A floor of 24 provides ~2.4σ margin, rejecting ~98% of pure-noise
//   blocks.  Only the excess above the floor is accumulated.
//
//   Previous value of 12 was based on single-frame noise (σ ≈ 7) and did
//   not account for the √2 factor in inter-frame deltas, causing ~49% of
//   blocks to exceed the floor on static scenes — hence the observed
//   gated-SAD of 3–5 even with no scene change.
#define DIFF_NOISE_FLOOR         24

// DIFF_TILE_MEAN_SAD_THRESH: a tile is "changed" when the mean of all
//   noise-gated block-deltas exceeds this value.
//   With the corrected floor of 24:
//     Static scene:              gated-block mean ≈ 0
//     Subtle change (slow tool): gated-block mean ≈ 3–10
//     Clear motion:              gated-block mean > 15
#define DIFF_TILE_MEAN_SAD_THRESH  20

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

// Set R↔B channel swap for tile extraction.
// Call after cam_diff_create() and whenever cam_settings.swap_rb changes.
void cam_diff_set_swap_rb(cam_diff_t ctx, bool swap_rb);

// Set byte-swap flag for 16-bit colour words (LV_COLOR_16_SWAP).
// When true the companion swaps the two bytes of each RGB565 pixel before
// JPEG-encoding tiles.
void cam_diff_set_swap_bytes(cam_diff_t ctx, bool swap_bytes);

// Set colour-inversion flag.  When true, each 16-bit RGB565 pixel is
// bitwise-inverted (XOR 0xFFFF) before JPEG-encoding tiles.
void cam_diff_set_invert_colors(cam_diff_t ctx, bool invert_colors);

#ifdef __cplusplus
}
#endif

#endif // CAM_DIFF_H
