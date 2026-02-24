// cam_capture.h — OV3660 DVP camera capture via esp32-camera

#ifndef CAM_CAPTURE_H
#define CAM_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>
#include "cam_protocol.h"
#include "cam_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the camera hardware.  Returns true on success.
// Must be called once during setup().
bool cam_capture_init(const cam_settings_t *settings);

// True when camera driver is initialized and ready to capture.
bool cam_capture_is_ready(void);

// Last camera init/capture error (ESP_OK if none).
int cam_capture_last_error(void);

// Human-readable name of last camera error.
const char *cam_capture_last_error_name(void);

// Reconfigure resolution (requires brief camera reinit).
bool cam_capture_set_resolution(cam_resolution_t res);

// Apply sensor settings (brightness, contrast, AEC/AGC, etc.).
void cam_capture_apply_sensor(const cam_settings_t *settings);

// ---------------------------------------------------------------------------
// AEC/AGC periodic re-lock
// ---------------------------------------------------------------------------
// When AEC/AGC is enabled the sensor adjusts gain/shutter every frame,
// causing a uniform brightness shift that looks like scene change to the diff.
//
// Strategy: lock AEC/AGC immediately after capturing each frame so the next
// frame is taken under the same exposure.  Every `interval` frames, unlock
// for exactly one capture so the sensor can re-sample ambient light, then
// re-lock again.  The caller should force a diff keyframe on those transitions
// to avoid the brightness step triggering partial tile updates.
//
// interval = 0  → feature disabled (AEC/AGC left as-is from cam_settings).
// interval = 30 → re-sample ambient light every 30 captured frames.
void cam_capture_set_ae_lock_interval(uint8_t interval);

// Call once per captured RGB565 frame (after cam_capture_rgb565 returns).
// Returns true on the frame where AEC/AGC was unlocked for re-sampling
// (i.e. this frame should be sent as a keyframe and curr→prev should be
// skipped, because the next frame will have a different exposure).
bool cam_capture_tick_ae(void);

// Capture a single frame as RGB565 into a PSRAM buffer.
// On success, sets *buf to the pixel data and *len to byte count.
// The buffer is owned by the camera driver — call cam_capture_release()
// when done.  Returns true on success.
bool cam_capture_rgb565(uint8_t **buf, size_t *len,
                        uint16_t *width, uint16_t *height);

// Capture a single frame as JPEG (for web server snapshots).
// Returns true on success.  Caller must call cam_capture_release().
bool cam_capture_jpeg(uint8_t **buf, size_t *len);

// Release the frame buffer returned by cam_capture_rgb565/jpeg.
void cam_capture_release(void);

// Get current capture resolution in pixels.
void cam_capture_get_resolution(uint16_t *w, uint16_t *h);

#ifdef __cplusplus
}
#endif

#endif // CAM_CAPTURE_H
