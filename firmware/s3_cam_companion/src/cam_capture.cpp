// cam_capture.cpp — OV3660 DVP camera driver using esp32-camera

#include "cam_capture.h"
#include "cam_pins.h"
#include "app_log.h"
#include "esp_camera.h"
#include <esp_log.h>
#include <esp_heap_caps.h>

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

static const char *TAG = "cam_capture";

static pixformat_t s_current_fmt  = PIXFORMAT_RGB565;
static framesize_t s_current_size = FRAMESIZE_VGA;
static camera_fb_t *s_last_fb     = NULL;
static bool s_camera_ready         = false;
static esp_err_t s_last_err        = ESP_OK;

// AEC/AGC periodic re-lock state.
static uint8_t  s_ae_lock_interval = 0;   // 0 = disabled
static uint8_t  s_ae_frame_counter = 0;
static bool     s_ae_locked        = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static framesize_t res_to_framesize(cam_resolution_t res) {
    switch (res) {
        case CAM_RES_QVGA: return FRAMESIZE_QVGA;
        case CAM_RES_SVGA: return FRAMESIZE_SVGA;
        case CAM_RES_VGA:
        default:           return FRAMESIZE_VGA;
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
bool cam_capture_init(const cam_settings_t *settings) {
    if (!settings) {
        s_last_err = ESP_ERR_INVALID_ARG;
        ESP_LOGE(TAG, "Camera init failed: settings is null");
        return false;
    }

    if (s_camera_ready) {
        ESP_LOGW(TAG, "Camera already initialized; reinitializing");
        cam_capture_release();
        esp_camera_deinit();
        s_camera_ready = false;
    }

    camera_config_t cfg = {};

    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.pin_d0       = CAM_PIN_Y2;
    cfg.pin_d1       = CAM_PIN_Y3;
    cfg.pin_d2       = CAM_PIN_Y4;
    cfg.pin_d3       = CAM_PIN_Y5;
    cfg.pin_d4       = CAM_PIN_Y6;
    cfg.pin_d5       = CAM_PIN_Y7;
    cfg.pin_d6       = CAM_PIN_Y8;
    cfg.pin_d7       = CAM_PIN_Y9;
    cfg.pin_xclk     = CAM_PIN_XCLK;
    cfg.pin_pclk     = CAM_PIN_PCLK;
    cfg.pin_vsync    = CAM_PIN_VSYNC;
    cfg.pin_href     = CAM_PIN_HREF;
    cfg.pin_sccb_sda = CAM_PIN_SIOD;
    cfg.pin_sccb_scl = CAM_PIN_SIOC;
    cfg.pin_pwdn     = CAM_PIN_PWDN;
    cfg.pin_reset    = CAM_PIN_RESET;

    cfg.xclk_freq_hz = CAM_XCLK_FREQ;
    cfg.pixel_format = PIXFORMAT_RGB565;
    cfg.frame_size   = res_to_framesize((cam_resolution_t)settings->resolution);
    cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode    = CAMERA_GRAB_LATEST;
    cfg.fb_count     = 2;
    cfg.jpeg_quality = settings->jpeg_quality;

    ESP_LOGI(TAG,
             "Camera bring-up: xclk=%dHz, frame=%d, pixfmt=%d, fb_count=%d, fb_loc=%d",
             cfg.xclk_freq_hz, (int)cfg.frame_size, (int)cfg.pixel_format,
             cfg.fb_count, (int)cfg.fb_location);
    ESP_LOGI(TAG,
             "Camera pins: pwdn=%d reset=%d xclk=%d sda=%d scl=%d y2..y9=%d,%d,%d,%d,%d,%d,%d,%d vsync=%d href=%d pclk=%d",
             cfg.pin_pwdn, cfg.pin_reset, cfg.pin_xclk, cfg.pin_sccb_sda, cfg.pin_sccb_scl,
             cfg.pin_d0, cfg.pin_d1, cfg.pin_d2, cfg.pin_d3,
             cfg.pin_d4, cfg.pin_d5, cfg.pin_d6, cfg.pin_d7,
             cfg.pin_vsync, cfg.pin_href, cfg.pin_pclk);
    ESP_LOGI(TAG, "PSRAM free=%u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    s_current_size = cfg.frame_size;
    s_current_fmt  = cfg.pixel_format;

    s_last_err = esp_camera_init(&cfg);
    if (s_last_err != ESP_OK) {
        s_camera_ready = false;
        ESP_LOGE(TAG, "Camera init failed: 0x%x (%s)", (unsigned)s_last_err, esp_err_to_name(s_last_err));
        return false;
    }

    s_camera_ready = true;
    s_last_err = ESP_OK;

    ESP_LOGI(TAG, "Camera initialised (OV3660 DVP, res=%d)", settings->resolution);
    cam_capture_apply_sensor(settings);

    // Probe one frame right after init so failures surface in logs immediately.
    camera_fb_t *probe = esp_camera_fb_get();
    if (!probe) {
        s_last_err = ESP_FAIL;
        ESP_LOGE(TAG, "Camera probe capture failed immediately after init");
    } else {
        ESP_LOGI(TAG, "Camera probe ok: %ux%u len=%u fmt=%u",
                 (unsigned)probe->width, (unsigned)probe->height,
                 (unsigned)probe->len, (unsigned)probe->format);
        esp_camera_fb_return(probe);
    }

    return true;
}

bool cam_capture_is_ready(void) {
    return s_camera_ready;
}

int cam_capture_last_error(void) {
    return (int)s_last_err;
}

const char *cam_capture_last_error_name(void) {
    return esp_err_to_name(s_last_err);
}

// ---------------------------------------------------------------------------
// Sensor settings
// ---------------------------------------------------------------------------
void cam_capture_apply_sensor(const cam_settings_t *settings) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;

    s->set_brightness(s, settings->brightness);
    s->set_contrast(s, settings->contrast);
    s->set_exposure_ctrl(s, settings->aec_enable ? 1 : 0);
    s->set_gain_ctrl(s, settings->agc_enable ? 1 : 0);

    if (!settings->aec_enable)
        s->set_aec_value(s, settings->aec_value);
    if (!settings->agc_enable)
        s->set_agc_gain(s, settings->agc_gain);

    s->set_aec2(s, 0);
    s->set_ae_level(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
}

// ---------------------------------------------------------------------------
// AEC/AGC periodic re-lock
// ---------------------------------------------------------------------------
void cam_capture_set_ae_lock_interval(uint8_t interval) {
    s_ae_lock_interval = interval;
    s_ae_frame_counter = 0;
    s_ae_locked        = false;  // start unlocked; first tick will lock
    ESP_LOGI(TAG, "AE lock interval set to %u frames (0=disabled)", interval);
}

bool cam_capture_tick_ae(void) {
    if (!s_camera_ready || s_ae_lock_interval == 0) return false;

    sensor_t *s = esp_camera_sensor_get();
    if (!s) return false;

    bool unlocked_this_frame = false;

    if (!s_ae_locked) {
        // Previous frame was taken with AEC/AGC running — re-lock now so the
        // NEXT capture uses a stable, freshly-measured exposure value.
        s->set_exposure_ctrl(s, 0);
        s->set_gain_ctrl(s, 0);
        s_ae_locked = true;
        s_ae_frame_counter = 0;
        ESP_LOGD(TAG, "AE re-locked after re-sample frame");
        // Return true: the just-captured frame was taken mid-AEC-adjustment;
        // caller should force a keyframe so the pendant sees a clean baseline.
        unlocked_this_frame = true;
    } else {
        s_ae_frame_counter++;
        if (s_ae_frame_counter >= s_ae_lock_interval) {
            // Unlock for the NEXT capture so the sensor can re-sample.
            s->set_exposure_ctrl(s, 1);
            s->set_gain_ctrl(s, 1);
            s_ae_locked = false;
            ESP_LOGD(TAG, "AE unlocked for re-sample");
        }
    }
    return unlocked_this_frame;
}

// ---------------------------------------------------------------------------
// Resolution change
// ---------------------------------------------------------------------------
bool cam_capture_set_resolution(cam_resolution_t res) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return false;

    framesize_t fs = res_to_framesize(res);
    if (fs == s_current_size) return true;

    if (s->set_framesize(s, fs) != 0) {
        ESP_LOGE(TAG, "set_framesize failed");
        return false;
    }
    s_current_size = fs;
    return true;
}

// ---------------------------------------------------------------------------
// Capture RGB565 frame
// ---------------------------------------------------------------------------
bool cam_capture_rgb565(uint8_t **buf, size_t *len,
                        uint16_t *width, uint16_t *height) {
    if (!s_camera_ready) {
        s_last_err = ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "RGB565 capture requested while camera is not initialized");
        return false;
    }

    if (s_last_fb) {
        esp_camera_fb_return(s_last_fb);
        s_last_fb = NULL;
    }

    if (s_current_fmt != PIXFORMAT_RGB565) {
        sensor_t *s = esp_camera_sensor_get();
        if (s) s->set_pixformat(s, PIXFORMAT_RGB565);
        s_current_fmt = PIXFORMAT_RGB565;
    }

    s_last_fb = esp_camera_fb_get();
    if (!s_last_fb) {
        s_last_err = ESP_FAIL;
        ESP_LOGE(TAG, "RGB565 capture failed");
        return false;
    }

    s_last_err = ESP_OK;

    *buf    = s_last_fb->buf;
    *len    = s_last_fb->len;
    *width  = s_last_fb->width;
    *height = s_last_fb->height;
    return true;
}

// ---------------------------------------------------------------------------
// Capture JPEG frame (for web server snapshots)
// ---------------------------------------------------------------------------
bool cam_capture_jpeg(uint8_t **buf, size_t *len) {
    if (!s_camera_ready) {
        s_last_err = ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "JPEG capture requested while camera is not initialized");
        return false;
    }

    if (s_last_fb) {
        esp_camera_fb_return(s_last_fb);
        s_last_fb = NULL;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        s_last_err = ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "JPEG capture failed: sensor handle is null");
        return false;
    }

    if (s->set_pixformat(s, PIXFORMAT_JPEG) != 0) {
        s_last_err = ESP_FAIL;
        ESP_LOGE(TAG, "JPEG capture failed: set_pixformat(PIXFORMAT_JPEG) failed");
        return false;
    }
    s_current_fmt = PIXFORMAT_JPEG;

    s_last_fb = esp_camera_fb_get();
    if (!s_last_fb) {
        s_last_err = ESP_FAIL;
        ESP_LOGE(TAG, "JPEG capture failed");
        if (s) s->set_pixformat(s, PIXFORMAT_RGB565);
        s_current_fmt = PIXFORMAT_RGB565;
        return false;
    }

    if (s_last_fb->format != PIXFORMAT_JPEG) {
        ESP_LOGE(TAG, "JPEG capture returned non-JPEG frame format=%u len=%u",
                 (unsigned)s_last_fb->format, (unsigned)s_last_fb->len);
        esp_camera_fb_return(s_last_fb);
        s_last_fb = NULL;
        s_last_err = ESP_ERR_INVALID_RESPONSE;
        s->set_pixformat(s, PIXFORMAT_RGB565);
        s_current_fmt = PIXFORMAT_RGB565;
        return false;
    }

    s_last_err = ESP_OK;

    *buf = s_last_fb->buf;
    *len = s_last_fb->len;
    return true;
}

// ---------------------------------------------------------------------------
// Release
// ---------------------------------------------------------------------------
void cam_capture_release(void) {
    if (s_last_fb) {
        esp_camera_fb_return(s_last_fb);
        s_last_fb = NULL;
    }
    if (s_current_fmt != PIXFORMAT_RGB565) {
        sensor_t *s = esp_camera_sensor_get();
        if (s) s->set_pixformat(s, PIXFORMAT_RGB565);
        s_current_fmt = PIXFORMAT_RGB565;
    }
}

void cam_capture_get_resolution(uint16_t *w, uint16_t *h) {
    switch (s_current_size) {
        case FRAMESIZE_QVGA: *w = 320; *h = 240; break;
        case FRAMESIZE_SVGA: *w = 800; *h = 600; break;
        default:             *w = 640; *h = 480; break;
    }
}
