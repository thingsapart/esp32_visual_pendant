#ifndef TOUCH_CALIB_H
#define TOUCH_CALIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// Check whether a calibration matrix is stored
bool touch_calib_has(void);

// Load saved 3x3 homography (row-major).
// Returns true on success.
bool touch_calib_load();

// Apply to H as non-permanent touch-calib to use, without saving.
// Apply to H as non-permanent touch-calib to use, without saving.
bool touch_calib_apply(const float H[9]);

// Save a 3x3 homography matrix. Returns true on success.
bool touch_calib_save(const float H[9]);

// Clear stored calibration
void touch_calib_clear(void);

// Apply stored homography to point (in/out). If no calibration, point remains unchanged.
void touch_calib_apply_inplace(float *x, float *y);

// Compute homography from 4 source points to 4 destination points.
// src and dst are arrays of 4 pairs: {{x0,y0},{x1,y1}...}
// H_out must be length 9.
// Returns true on success.
bool touch_calib_compute_homography(const float src[4][2], const float dst[4][2], float H_out[9]);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_CALIB_H
