// cam_settings.cpp — NVS persistence for camera settings

#include "cam_settings.h"
#include <Preferences.h>
#include <string.h>
#include <math.h>
#include <esp_log.h>

static const char *TAG = "cam_settings";
static Preferences prefs;

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------
void cam_settings_defaults(cam_settings_t *s) {
    memset(s, 0, sizeof(*s));

    s->resolution        = CAM_DEFAULT_RESOLUTION;
    s->jpeg_quality      = CAM_DEFAULT_JPEG_QUALITY;
    s->tiles_x           = CAM_DEFAULT_TILES_X;
    s->tiles_y           = CAM_DEFAULT_TILES_Y;
    s->output_width      = CAM_DEFAULT_OUTPUT_WIDTH;
    s->output_height     = CAM_DEFAULT_OUTPUT_HEIGHT;
    s->diff_threshold    = CAM_DEFAULT_DIFF_THRESHOLD;
    s->keyframe_interval = CAM_DEFAULT_KEYFRAME_INTERVAL;
    s->brightness        = 0;
    s->contrast          = 0;
    s->aec_enable        = true;
    s->agc_enable        = true;
    s->aec_value         = 300;
    s->agc_gain          = 0;

    // Extended sensor defaults
    s->ae_level          = CAM_DEFAULT_AE_LEVEL;
    s->gainceiling       = CAM_DEFAULT_GAINCEILING;
    s->bpc               = CAM_DEFAULT_BPC;
    s->wpc               = CAM_DEFAULT_WPC;
    s->raw_gma           = CAM_DEFAULT_RAW_GMA;
    s->lenc              = CAM_DEFAULT_LENC;
    s->hmirror           = CAM_DEFAULT_HMIRROR;
    s->vflip             = CAM_DEFAULT_VFLIP;
    s->dcw               = CAM_DEFAULT_DCW;
    s->saturation        = CAM_DEFAULT_SATURATION;
    s->sharpness         = CAM_DEFAULT_SHARPNESS;
    s->denoise           = CAM_DEFAULT_DENOISE;
    s->aec2              = CAM_DEFAULT_AEC2;
    s->wb_mode           = CAM_DEFAULT_WB_MODE;

    s->send_interval_ms  = CAM_DEFAULT_SEND_INTERVAL_MS;
    s->swap_rb           = false;
    s->swap_bytes        = false;
    s->invert_colors     = false;
    s->wifi_channel      = CAM_WIFI_CHANNEL;
    s->calibrated        = false;

    // Grid defaults
    s->grid_calibrated = false;
    s->grid_auto_generated = false;
    s->grid_minx = s->grid_miny = 0.0f;
    s->grid_maxx = s->grid_maxy = 0.0f;
    s->grid_dx = s->grid_dy = 0.0f;
    s->grid_nx = s->grid_ny = 0;
    s->grid_inset_left = 0;
    s->grid_inset_top = 0;
    s->grid_inset_right = 0;
    s->grid_inset_bottom = 0;
    s->grid_points_count = 0;
    for (int i = 0; i < CAM_SETTINGS_MAX_GRID_POINTS; i++) { s->grid_points[i][0] = 0.0f; s->grid_points[i][1] = 0.0f; }
    s->surface_width = 100.0f;
    s->surface_height = 100.0f;

    // Identity homography (no transform)
    s->homography[0] = 1.0f; s->homography[1] = 0.0f; s->homography[2] = 0.0f;
    s->homography[3] = 0.0f; s->homography[4] = 1.0f; s->homography[5] = 0.0f;
    s->homography[6] = 0.0f; s->homography[7] = 0.0f; s->homography[8] = 1.0f;

    // Default calibration corners (full frame, normalised)
    s->cal_src[0][0] = 0.0f; s->cal_src[0][1] = 0.0f;  // TL
    s->cal_src[1][0] = 1.0f; s->cal_src[1][1] = 0.0f;  // TR
    s->cal_src[2][0] = 1.0f; s->cal_src[2][1] = 1.0f;  // BR
    s->cal_src[3][0] = 0.0f; s->cal_src[3][1] = 1.0f;  // BL

    strncpy(s->ap_ssid, "CamCompanion", sizeof(s->ap_ssid) - 1);
    s->ap_pass[0] = '\0';  // Open network by default

    memset(s->hub_mac, 0, 6);
}

// ---------------------------------------------------------------------------
// NVS load / save — stored as a raw blob for simplicity
// ---------------------------------------------------------------------------
#define NVS_NS   "cam"
#define NVS_KEY  "cfg"
#define NVS_VER_KEY "ver"
// Bump whenever the cam_settings_t struct layout changes (field insertion,
// reorder, type change).  A version mismatch forces a full reset to defaults
// so stale NVS blobs don't corrupt field values.
// v5: forces re-load of defaults so CAM_DEFAULT_JPEG_QUALITY (now 20) takes effect.
#define NVS_VERSION 5

void cam_settings_init(cam_settings_t *s) {
    cam_settings_defaults(s);

    prefs.begin(NVS_NS, true);  // read-only
    uint8_t ver = prefs.getUChar(NVS_VER_KEY, 0);
    if (ver == NVS_VERSION) {
        size_t len = prefs.getBytesLength(NVS_KEY);
        if (len > 0 && len <= sizeof(cam_settings_t)) {
            // Partial-load: handles struct growth across firmware updates.
            // Zero-fill (already done by cam_settings_defaults above) then
            // overwrite only the bytes that exist in the stored blob.
            // New fields appended to the struct will retain their defaults.
            prefs.getBytes(NVS_KEY, s, len);
            if (len < sizeof(cam_settings_t)) {
                ESP_LOGW(TAG, "NVS blob smaller than struct (%zu < %zu), "
                         "new fields left at defaults", len, sizeof(cam_settings_t));
            } else {
                ESP_LOGI(TAG, "Settings loaded from NVS (ver %d)", ver);
            }
        } else if (len > sizeof(cam_settings_t)) {
            // Stored blob is larger — load only what we understand.
            prefs.getBytes(NVS_KEY, s, sizeof(*s));
            ESP_LOGW(TAG, "NVS blob larger than struct (%zu > %zu), truncated",
                     len, sizeof(cam_settings_t));
        } else {
            ESP_LOGW(TAG, "Empty NVS blob, using defaults");
        }
    } else {
        ESP_LOGI(TAG, "No saved settings (ver %d), using defaults", ver);
    }
    prefs.end();
}

void cam_settings_save(const cam_settings_t *s) {
    prefs.begin(NVS_NS, false);  // read-write
    prefs.putUChar(NVS_VER_KEY, NVS_VERSION);
    prefs.putBytes(NVS_KEY, s, sizeof(*s));
    prefs.end();
    ESP_LOGI(TAG, "Settings saved to NVS");
}

// ---------------------------------------------------------------------------
// Apply incoming config command
// ---------------------------------------------------------------------------
void cam_settings_apply_config(cam_settings_t *s, const cam_config_cmd_t *cfg) {
    // NOTE: resolution, output_width, output_height are companion-local
    // settings configured exclusively via the web UI.  The pendant's config
    // command must NOT overwrite them — they control capture hardware and the
    // output transform pipeline, which the companion manages locally.
    // s->resolution is left untouched.
    // s->output_width / output_height are left untouched.

    s->jpeg_quality      = cfg->jpeg_quality;
    s->tiles_x           = cfg->tiles_x;
    s->tiles_y           = cfg->tiles_y;
    s->diff_threshold    = cfg->diff_threshold;
    s->keyframe_interval = cfg->keyframe_interval;
    s->brightness        = cfg->brightness;
    s->contrast          = cfg->contrast;
    s->aec_enable        = cfg->aec_enable;
    s->agc_enable        = cfg->agc_enable;
    s->aec_value         = cfg->aec_value;
    s->agc_gain          = cfg->agc_gain;
    s->swap_rb           = cfg->swap_rb ? true : false;
    s->swap_bytes        = cfg->swap_bytes ? true : false;
    s->invert_colors     = cfg->invert_colors ? true : false;

    // Extended sensor settings — only apply if the sender included them
    // (backward-compatible: old senders send a shorter SET_CONFIG).
    s->ae_level          = cfg->ae_level;
    s->gainceiling       = cfg->gainceiling;
    s->bpc               = cfg->bpc;
    s->wpc               = cfg->wpc;
    s->raw_gma           = cfg->raw_gma;
    s->lenc              = cfg->lenc;
    s->hmirror           = cfg->hmirror;
    s->vflip             = cfg->vflip;
    s->dcw               = cfg->dcw;
    s->saturation        = cfg->saturation;
    s->sharpness         = cfg->sharpness;
    s->denoise           = cfg->denoise;
    s->aec2              = cfg->aec2;
    s->wb_mode           = cfg->wb_mode;

    // Copy homography if non-zero
    bool all_zero = true;
    for (int i = 0; i < 9; i++) {
        if (cfg->homography[i] != 0.0f) { all_zero = false; break; }
    }
    if (!all_zero) {
        memcpy(s->homography, cfg->homography, sizeof(s->homography));
    }

    cam_settings_save(s);
    ESP_LOGI(TAG, "Config applied: res=%d q=%d tiles=%dx%d",
             s->resolution, s->jpeg_quality, s->tiles_x, s->tiles_y);
}

// ---------------------------------------------------------------------------
// Compute homography from 4 calibration corners
//
// Uses the Direct Linear Transform (DLT) to find H such that:
//   dst = H * src   (in homogeneous coordinates)
//
// Source corners are the user-selected points in the camera image.
// Destination corners are the output rectangle:
//   (0,0)  (w,0)  (w,h)  (0,h)
//
// We solve the 8-equation system A*h = 0 using a simplified approach
// (Gaussian elimination on the 8×9 augmented matrix, last element = 1).
// ---------------------------------------------------------------------------
void cam_settings_compute_homography(cam_settings_t *s,
                                     uint16_t img_w, uint16_t img_h) {
    if (img_w < 2 || img_h < 2) {
        ESP_LOGW(TAG, "Homography skipped: invalid image size %ux%u", img_w, img_h);
        return;
    }

    // Use pixel coordinates in [0..w-1], [0..h-1].
    const float max_x = (float)(img_w - 1);
    const float max_y = (float)(img_h - 1);

    // Source points (pixel coordinates from normalized calibration corners).
    float sx[4], sy[4];
    for (int i = 0; i < 4; i++) {
        sx[i] = s->cal_src[i][0] * max_x;
        sy[i] = s->cal_src[i][1] * max_y;
    }

    // Destination points (output rectangle corners).
    const float dx[4] = {0.0f, max_x, max_x, 0.0f};
    const float dy[4] = {0.0f, 0.0f, max_y, max_y};

    // Solve A * h = b for h=[h0..h7], with h8 fixed to 1.
    // Equations:
    // h0*sx + h1*sy + h2 - dx*h6*sx - dx*h7*sy = dx
    // h3*sx + h4*sy + h5 - dy*h6*sx - dy*h7*sy = dy
    float M[8][9]; // 8x8 + RHS
    memset(M, 0, sizeof(M));
    for (int i = 0; i < 4; i++) {
        int r = i * 2;

        M[r][0] = sx[i];
        M[r][1] = sy[i];
        M[r][2] = 1.0f;
        M[r][6] = -dx[i] * sx[i];
        M[r][7] = -dx[i] * sy[i];
        M[r][8] = dx[i];

        M[r + 1][3] = sx[i];
        M[r + 1][4] = sy[i];
        M[r + 1][5] = 1.0f;
        M[r + 1][6] = -dy[i] * sx[i];
        M[r + 1][7] = -dy[i] * sy[i];
        M[r + 1][8] = dy[i];
    }

    // Gaussian elimination with partial pivoting.
    for (int col = 0; col < 8; col++) {
        int pivot = col;
        float max_val = fabsf(M[col][col]);
        for (int r = col + 1; r < 8; r++) {
            float v = fabsf(M[r][col]);
            if (v > max_val) {
                max_val = v;
                pivot = r;
            }
        }

        if (max_val < 1e-10f) {
            ESP_LOGW(TAG, "Homography: singular matrix at col %d", col);
            return;
        }

        if (pivot != col) {
            for (int c = col; c < 9; c++) {
                float t = M[col][c];
                M[col][c] = M[pivot][c];
                M[pivot][c] = t;
            }
        }

        float inv_pivot = 1.0f / M[col][col];
        for (int c = col; c < 9; c++) {
            M[col][c] *= inv_pivot;
        }

        for (int r = 0; r < 8; r++) {
            if (r == col) continue;
            float factor = M[r][col];
            if (fabsf(factor) < 1e-12f) continue;
            for (int c = col; c < 9; c++) {
                M[r][c] -= factor * M[col][c];
            }
        }
    }

    float h[9];
    h[0] = M[0][8];
    h[1] = M[1][8];
    h[2] = M[2][8];
    h[3] = M[3][8];
    h[4] = M[4][8];
    h[5] = M[5][8];
    h[6] = M[6][8];
    h[7] = M[7][8];
    h[8] = 1.0f;

    memcpy(s->homography, h, sizeof(s->homography));
    s->calibrated = true;

    ESP_LOGI(TAG, "Homography computed: [%.5f %.5f %.5f; %.5f %.5f %.5f; %.5f %.5f %.5f]",
             h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8]);
}
