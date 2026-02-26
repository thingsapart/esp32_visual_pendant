// cam_transform.h — Perspective (homography) transform via pre-computed LUT
//
// The LUT maps each output pixel (x_out, y_out) → (x_src, y_src) using a
// pre-computed inverse homography.  At runtime, the transform is just an
// array lookup per pixel — no floating-point math in the hot path.
//
// With CAM_ENHANCED_PROCESSING enabled:
//   - LUT entries use 12.4 fixed-point for sub-pixel bilinear interpolation
//   - Per-pixel sample-count map enables adaptive area averaging where the
//     warp compresses source pixels (minification regions)
//   - src and dst dimensions may differ (capture at high res, output at lower)

#ifndef CAM_TRANSFORM_H
#define CAM_TRANSFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "cam_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to a transform context (holds the LUT in PSRAM).
typedef struct cam_transform_ctx *cam_transform_t;

// Build (or rebuild) the LUT from a 3×3 homography matrix.
// src_w/src_h: input (camera capture) image dimensions.
// dst_w/dst_h: output (warped) image dimensions — may differ from src.
// homography:  3×3 row-major float matrix.
// Returns NULL on allocation failure.
cam_transform_t cam_transform_create(uint16_t src_w, uint16_t src_h,
                                     uint16_t dst_w, uint16_t dst_h,
                                     const float homography[9]);

// Free the LUT memory.
void cam_transform_destroy(cam_transform_t ctx);

// Apply the LUT: warp src_rgb565 → dst_rgb565.
// src buffer must be at least (src_w × src_h × 2) bytes.
// dst buffer must be at least (dst_w × dst_h × 2) bytes.
// Pixels that map outside the source image are set to black (0x0000).
void cam_transform_apply(cam_transform_t ctx,
                         const uint16_t *src_rgb565,
                         uint16_t *dst_rgb565);

// Get the output dimensions of the transform.
void cam_transform_get_size(cam_transform_t ctx,
                            uint16_t *dst_w, uint16_t *dst_h);

// Get the source (capture) dimensions of the transform.
void cam_transform_get_src_size(cam_transform_t ctx,
                                uint16_t *src_w, uint16_t *src_h);

// Check if the transform is an identity (no-op).
bool cam_transform_is_identity(cam_transform_t ctx);

// Inform the transform about the byte order of source pixels.
// When swap_bytes is true, source RGB565 pixels are byte-swapped relative to
// the standard layout (RRRRRGGG_GGGBBBBB).  This only matters for bilinear
// interpolation which must extract individual R/G/B channels; nearest-neighbour
// simply copies raw pixel values and is byte-order agnostic.
void cam_transform_set_swap_bytes(cam_transform_t ctx, bool swap);

#ifdef __cplusplus
}
#endif

#endif // CAM_TRANSFORM_H
