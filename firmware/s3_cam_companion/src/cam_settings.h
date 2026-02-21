// cam_settings.h — Persistent configuration stored in NVS
//
// Uses Arduino Preferences (NVS) for non-volatile storage.
// All settings can be changed via the web UI or ESP-NOW config command.

#ifndef CAM_SETTINGS_H
#define CAM_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "cam_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Runtime settings struct — loaded from NVS on boot, modifiable at runtime.
// ---------------------------------------------------------------------------
typedef struct {
    // Image
    uint8_t  resolution;          // cam_resolution_t
    uint8_t  jpeg_quality;        // 1–63
    uint8_t  tiles_x;
    uint8_t  tiles_y;

    // Diff engine
    uint8_t  diff_threshold;      // Per-pixel channel diff to count as changed
    uint8_t  keyframe_interval;   // Send full keyframe every N frames

    // Camera sensor
    int8_t   brightness;          // -2..+2
    int8_t   contrast;            // -2..+2
    bool     aec_enable;          // Auto-exposure control
    bool     agc_enable;          // Auto-gain control
    int16_t  aec_value;           // Manual exposure (when AEC off)
    uint8_t  agc_gain;            // Manual gain ceiling (when AGC off)

    // Homography (3×3 row-major, identity = no transform)
    float    homography[9];

    // Calibration corners — source points in camera image (normalised 0..1)
    float    cal_src[4][2];       // 4 corners: TL, TR, BR, BL
    bool     calibrated;          // True if user has set corners

    // Network
    uint8_t  wifi_channel;
    char     ap_ssid[33];         // AP SSID for config mode (default: CamCompanion)
    char     ap_pass[65];         // AP password (empty = open)
    uint8_t  hub_mac[6];          // Learned hub MAC (all-zero = broadcast)

    // Timing
    uint8_t  send_interval_ms;    // Delay between ESP-NOW chunk sends
} cam_settings_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Initialise NVS and load settings (or defaults if first boot).
void cam_settings_init(cam_settings_t *s);

// Persist current settings to NVS.
void cam_settings_save(const cam_settings_t *s);

// Reset all settings to factory defaults (does NOT save — call save after).
void cam_settings_defaults(cam_settings_t *s);

// Apply an incoming cam_config_cmd_t to the live settings (and persist).
void cam_settings_apply_config(cam_settings_t *s, const cam_config_cmd_t *cfg);

// Recalculate the homography matrix from the 4 calibration corners.
// Destination corners are assumed to be the full output rectangle.
void cam_settings_compute_homography(cam_settings_t *s,
                                     uint16_t img_w, uint16_t img_h);

#ifdef __cplusplus
}
#endif

#endif // CAM_SETTINGS_H
