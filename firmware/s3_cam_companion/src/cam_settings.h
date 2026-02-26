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

    // Output resolution after transform (0 = same as capture)
    uint16_t output_width;
    uint16_t output_height;

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
    uint8_t  ae_lock_interval;    // AEC/AGC re-sample interval in frames (0=disabled)

    // Extended sensor settings
    int8_t   ae_level;            // AEC brightness bias (-3..+3)
    uint8_t  gainceiling;         // Max analog gain: 0=2x .. 6=128x
    uint8_t  bpc;                 // Black pixel correction
    uint8_t  wpc;                 // White pixel correction
    uint8_t  raw_gma;             // Gamma correction
    uint8_t  lenc;                // Lens correction
    uint8_t  hmirror;             // Horizontal mirror
    uint8_t  vflip;               // Vertical flip
    uint8_t  dcw;                 // Downsize enable
    int8_t   saturation;          // Colour saturation (-2..+2)
    int8_t   sharpness;           // Sharpness (-2..+2) — OV3660
    int8_t   denoise;             // Denoise level (0..10), 0=auto
    uint8_t  aec2;                // Anti-banding / AEC night mode
    uint8_t  wb_mode;             // WB mode: 0=auto,1=sunny,2=cloudy,3=office,4=home

    // Homography (3×3 row-major, identity = no transform)
    float    homography[9];

    // Calibration corners — source points in camera image (normalised 0..1)
    float    cal_src[4][2];       // 4 corners: TL, TR, BR, BL
    bool     calibrated;          // True if user has set corners

    // Grid calibration: mapping from a regular real-world grid to image points.
    // grid_points_count = nx * ny (interior/exterior as provided). Each point
    // is stored as normalized image coords (0..1). Max entries limited.
    bool     grid_calibrated;
    bool     grid_auto_generated;  // true = auto from dimensions, false = wizard
    float    grid_minx; // real units
    float    grid_maxx;
    float    grid_miny;
    float    grid_maxy;
    float    grid_dx;
    float    grid_dy;
    uint16_t grid_nx;
    uint16_t grid_ny;
    // Image margins in output pixels — how far from the output image edge the
    // selected work-area corner points land after transformation.  The
    // homography maps the calibration corners to
    //   (margin_left, margin_top) → (w-1-margin_right, h-1-margin_bottom)
    // so the output image captures margin_* extra pixels of raw image around
    // the work area.  The grid spans this same inset region, aligning exactly
    // with the corner points.
    uint16_t image_margin_left;
    uint16_t image_margin_top;
    uint16_t image_margin_right;
    uint16_t image_margin_bottom;
#define CAM_SETTINGS_MAX_GRID_POINTS 256
    float    grid_points[CAM_SETTINGS_MAX_GRID_POINTS][2];
    uint16_t grid_points_count;
    // Project surface physical dimensions (real-world units, e.g., mm)
    float    surface_width;
    float    surface_height;

    // Network
    uint8_t  wifi_channel;
    char     ap_ssid[33];         // AP SSID for config mode (default: CamCompanion)
    char     ap_pass[65];         // AP password (empty = open)
    uint8_t  hub_mac[6];          // Learned hub MAC (all-zero = broadcast)

    // Timing
    uint8_t  send_interval_ms;    // Delay between ESP-NOW chunk sends

    // Color channel order for the pendant LCD.
    bool     swap_rb;             // false = RGB565 (default), true = BGR565
    // Whether the pendant's display expects 16-bit colour words with bytes
    // swapped (LV_COLOR_16_SWAP).  If true the companion will byte-swap
    // each RGB565 pixel before JPEG-encoding tiles.
    bool     swap_bytes;
    // Invert all pixel colours (XOR 0xFFFF on each RGB565 word).
    bool     invert_colors;
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

// Erase the settings NVS namespace.  Call cam_settings_defaults() +
// cam_settings_save() afterwards, then reboot.
void cam_settings_erase(void);

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
