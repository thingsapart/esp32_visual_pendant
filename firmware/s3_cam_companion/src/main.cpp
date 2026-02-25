// main.cpp — Camera Companion entry point
//
// Boot flow:
//   1. Read BOOT button (GPIO0).
//   2. Load settings from NVS.
//   3. If BOOT held or no calibration → config mode (AP + web server).
//   4. Otherwise → normal mode:
//      a. Init camera (RGB565 capture).
//      b. Build homography LUT.
//      c. Init ESP-NOW.
//      d. Init diff engine.
//      e. Wait for pendant frame requests; capture → transform → diff → send.

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>

#include "cam_pins.h"
#include "cam_protocol.h"
#include "cam_settings.h"
#include "cam_capture.h"
#include "cam_transform.h"
#include "cam_diff.h"
#include "cam_espnow.h"
#include "cam_led.h"
#include "cam_webserver.h"
#include "app_log.h"
#include "esp_camera.h"

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

static const char *TAG = "cam_main";

static int serial_log_vprintf(const char *fmt, va_list args) {
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n <= 0) return n;

    if (n < (int)sizeof(buf)) {
        Serial.write((const uint8_t *)buf, n);
    } else {
        Serial.write((const uint8_t *)buf, sizeof(buf) - 1);
    }
    return n;
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static cam_settings_t g_settings;
static cam_transform_t g_transform = NULL;
static cam_diff_t      g_diff      = NULL;

// PSRAM buffers for the warped frame.
static uint16_t *g_warped_buf = NULL;

#if CAM_ENH_TEMPORAL_DENOISE
// Temporal denoise: previous warped frame for 2-frame EMA.
static uint16_t *g_temporal_prev = NULL;
static bool      g_temporal_valid = false;
// EMA alpha in 8-bit fixed-point: alpha=0.7 → 179/256.  Higher = more
// new-frame weight (less smoothing, less motion blur).
#define TEMPORAL_ALPHA  179   // 0.7 × 256 ≈ 179
#define TEMPORAL_INV    (256 - TEMPORAL_ALPHA)  // 77
#endif

// Output resolution requested by the pendant (0 = don't-care → use defaults).
static uint16_t g_output_w = 0;
static uint16_t g_output_h = 0;

// Set to true when g_output_w/h changes so the pipeline is rebuilt from loop().
static volatile bool g_pipeline_dirty = false;

static uint16_t g_frame_id   = 0;
static bool     g_frame_requested = false;
static bool     g_force_keyframe  = false;
static bool     g_espnow_ready    = false;
static uint32_t g_last_frame_ms   = 0;
static uint32_t g_frames_sent     = 0;
static uint32_t g_fps_timer_ms    = 0;
static uint8_t  g_fps_x10        = 0;
static bool     g_logged_color_probe = false;

// Streaming session state:
//   g_streaming = true  → pendant has sent STREAM_START; accept REQUEST_FRAME
//   g_streaming = false → idle; next REQUEST_FRAME or STREAM_START restarts a session
static bool     g_streaming        = false;
static uint32_t g_last_request_ms  = 0;  // millis() of last received frame request
static uint8_t  g_grid_send_count  = 0;  // # of grid packets sent this session; reset on STREAM_START

// BOOT button press flag (set from ISR on FALLING edge)
static volatile bool g_boot_pressed = false;
static uint32_t g_last_boot_press_ms = 0;  // millis() when debounced press started
// Long-press factory reset tracking (polled in loop)
static bool g_boot_hold_armed = false;  // true while a hold is being measured

// ISR for BOOT button (minimal work)
static void IRAM_ATTR boot_isr(void) {
    g_boot_pressed = true;
}

static inline uint16_t bswap16_local(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static inline void rgb565_unpack(uint16_t px, bool bswap,
                                 uint8_t *r5, uint8_t *g6, uint8_t *b5) {
    if (bswap) px = bswap16_local(px);
    *r5 = (uint8_t)((px >> 11) & 0x1F);
    *g6 = (uint8_t)((px >> 5) & 0x3F);
    *b5 = (uint8_t)(px & 0x1F);
}

static void log_resize_color_probe(const uint16_t *raw, uint16_t raw_w, uint16_t raw_h,
                                   const uint16_t *warped, uint16_t out_w, uint16_t out_h) {
    if (!raw || !warped || raw_w == 0 || raw_h == 0 || out_w == 0 || out_h == 0) return;

    uint16_t raw_x = raw_w / 2;
    uint16_t raw_y = raw_h / 2;
    uint16_t out_x = out_w / 2;
    uint16_t out_y = out_h / 2;

    uint16_t raw_px = raw[(size_t)raw_y * raw_w + raw_x];
    uint16_t out_px = warped[(size_t)out_y * out_w + out_x];

    uint8_t rr_n, rg_n, rb_n, rr_s, rg_s, rb_s;
    uint8_t or_n, og_n, ob_n, or_s, og_s, ob_s;
    rgb565_unpack(raw_px, false, &rr_n, &rg_n, &rb_n);
    rgb565_unpack(raw_px, true,  &rr_s, &rg_s, &rb_s);
    rgb565_unpack(out_px, false, &or_n, &og_n, &ob_n);
    rgb565_unpack(out_px, true,  &or_s, &og_s, &ob_s);

    ESP_LOGI(TAG,
             "Resize color probe RAW(%ux%u)[%u,%u]=0x%04X native=%u/%u/%u bswap=%u/%u/%u | "
             "OUT(%ux%u)[%u,%u]=0x%04X native=%u/%u/%u bswap=%u/%u/%u",
             raw_w, raw_h, raw_x, raw_y, raw_px,
             rr_n, rg_n, rb_n, rr_s, rg_s, rb_s,
             out_w, out_h, out_x, out_y, out_px,
             or_n, og_n, ob_n, or_s, og_s, ob_s);
}

// ---------------------------------------------------------------------------
// ESP-NOW receive callback (runs from WiFi task — keep minimal!)
// ---------------------------------------------------------------------------
static void on_espnow_recv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < 1) return;
    uint8_t type = data[0];

    switch (type) {
    case CAM_CMD_STREAM_START: {
        // Pendant explicitly opens a streaming session.
        cam_espnow_learn_peer(mac, g_settings.hub_mac);
        if (!g_streaming) {
            ESP_LOGI(TAG, "STREAM_START from " MACSTR " — session opened", MAC2STR(mac));
        }
        g_streaming       = true;
        g_force_keyframe  = true;
        g_frame_requested = true;
        g_last_request_ms = millis();
        g_grid_send_count = 0;  // New session — resend grid to pendant

        // Parse desired output resolution from extended STREAM_START (v2).
        if (len >= (int)sizeof(cam_stream_start_cmd_t)) {
            const cam_stream_start_cmd_t *cmd = (const cam_stream_start_cmd_t *)data;
            uint16_t dw = cmd->desired_width;
            uint16_t dh = cmd->desired_height;
            if (dw != 0 && dh != 0) {
                // Client requests a specific resolution — override default.
                if (dw != g_output_w || dh != g_output_h) {
                    g_output_w = dw;
                    g_output_h = dh;
                    g_pipeline_dirty = true;
                }
            } else {
                // Client says "don't care" — clear override so setup_pipeline
                // falls through to the webui default (output_width/height).
                if (g_output_w != 0 || g_output_h != 0) {
                    g_output_w = 0;
                    g_output_h = 0;
                    g_pipeline_dirty = true;
                }
            }
            ESP_LOGI(TAG, "Client requested output %ux%u (0=don't-care)", dw, dh);
        } else {
            // Legacy client without output resolution fields
            if (g_output_w != 0 || g_output_h != 0) {
                g_output_w = 0;
                g_output_h = 0;
                g_pipeline_dirty = true;
            }
        }

        // Immediately acknowledge with a STATUS so the pendant confirms link.
        if (g_espnow_ready) {
            cam_espnow_send_status(g_settings.hub_mac,
                                   CAM_STATUS_IDLE, g_frame_id, g_fps_x10);
        }
        break;
    }
    case CAM_CMD_STREAM_STOP: {
        ESP_LOGI(TAG, "STREAM_STOP from " MACSTR " — session closed", MAC2STR(mac));
        g_streaming       = false;
        g_frame_requested = false;
        // Force a keyframe when streaming resumes so the pendant gets a clean
        // baseline (avoids displaying stale diff fragments from prior session).
        g_force_keyframe  = true;
        cam_led_set(CAM_LED_NO_PEER);
        break;
    }
    case CAM_CMD_REQUEST_FRAME: {
        uint32_t now = millis();
        // If the camera was idle (not streaming), treat this as an implicit
        // session start so old clients that don't send STREAM_START still work.
        if (!g_streaming) {
            g_streaming      = true;
            g_force_keyframe = true;  // Re-establish with keyframe after idle gap
            g_grid_send_count = 0;    // New implicit session — resend grid
            ESP_LOGI(TAG, "Implicit stream start from " MACSTR, MAC2STR(mac));
        }
        g_frame_requested = true;
        g_last_request_ms = now;
        if (len >= (int)sizeof(cam_request_frame_cmd_t)) {
            const cam_request_frame_cmd_t *cmd = (const cam_request_frame_cmd_t *)data;
            if (cmd->flags & 0x01) g_force_keyframe = true;
        }
        // Learn the sender as our peer.
        cam_espnow_learn_peer(mac, g_settings.hub_mac);
        break;
    }
    case CAM_CMD_SET_CONFIG: {
        if (len >= (int)sizeof(cam_config_cmd_t)) {
            bool old_swap_rb       = g_settings.swap_rb;
            bool old_swap_bytes    = g_settings.swap_bytes;
            bool old_invert_colors = g_settings.invert_colors;
            uint8_t old_tiles_x    = g_settings.tiles_x;
            uint8_t old_tiles_y    = g_settings.tiles_y;
            uint8_t old_jpeg_q    = g_settings.jpeg_quality;
            float old_h[9];
            memcpy(old_h, g_settings.homography, sizeof(old_h));

            const cam_config_cmd_t *cfg = (const cam_config_cmd_t *)data;

            cam_settings_apply_config(&g_settings, cfg);

            bool color_space_changed =
                (old_swap_rb != g_settings.swap_rb) ||
                (old_swap_bytes != g_settings.swap_bytes) ||
                (old_invert_colors != g_settings.invert_colors);
            bool tile_grid_changed =
                (old_tiles_x != g_settings.tiles_x) ||
                (old_tiles_y != g_settings.tiles_y);
            bool homography_changed =
                (memcmp(old_h, g_settings.homography, sizeof(old_h)) != 0);

            // Any config change should be followed by a keyframe so the pendant
            // does not blend DIFF tiles from old/new colour spaces.
            g_force_keyframe = true;

            // Rebuild pipeline in the main loop for changes that alter transform
            // or diff geometry/state (must not allocate from WiFi callback).
            if (color_space_changed || tile_grid_changed || homography_changed) {
                g_pipeline_dirty = true;
                ESP_LOGI(TAG,
                         "Config affects pipeline (color=%d grid=%d H=%d) -> rebuild + keyframe",
                         color_space_changed,
                         tile_grid_changed,
                         homography_changed);
            }

            // Propagate swap_rb to the diff engine immediately.
            if (g_diff) cam_diff_set_swap_rb(g_diff, g_settings.swap_rb);
            // Propagate byte-swap preference as well.
            if (g_diff) cam_diff_set_swap_bytes(g_diff, g_settings.swap_bytes);
            // Propagate colour-inversion preference.
            if (g_diff) cam_diff_set_invert_colors(g_diff, g_settings.invert_colors);
            ESP_LOGI(TAG, "Config received: swap_rb=%d swap_bytes=%d invert=%d -> applied swap_rb=%d swap_bytes=%d invert=%d",
                     cfg->swap_rb, cfg->swap_bytes, cfg->invert_colors,
                     g_settings.swap_rb, g_settings.swap_bytes, g_settings.invert_colors);
        }
        break;
    }
    case CAM_CMD_FORCE_KEYFRAME:
        g_force_keyframe = true;
        break;
    default:
        // Ignore hub CNC messages (0x00–0x1F) — they're not for us.
        break;
    }
}

// ---------------------------------------------------------------------------
// Allocate PSRAM buffers
// ---------------------------------------------------------------------------
static bool allocate_buffers(uint16_t out_w, uint16_t out_h) {
    size_t frame_bytes = (size_t)out_w * out_h * sizeof(uint16_t);

    if (g_warped_buf) heap_caps_free(g_warped_buf);
    g_warped_buf = (uint16_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM);
    if (!g_warped_buf) {
        ESP_LOGE(TAG, "Failed to alloc warped buffer (%zu bytes)", frame_bytes);
        return false;
    }
    memset(g_warped_buf, 0, frame_bytes);

#if CAM_ENH_TEMPORAL_DENOISE
    if (g_temporal_prev) heap_caps_free(g_temporal_prev);
    g_temporal_prev = (uint16_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM);
    if (!g_temporal_prev) {
        ESP_LOGW(TAG, "Failed to alloc temporal buffer — temporal denoise disabled");
    }
    g_temporal_valid = false;
#endif

    return true;
}

// Forward declaration (defined below).
static bool setup_pipeline(void);
static bool setup_pipeline_with_capture_dims(uint16_t cap_w, uint16_t cap_h);

// ---------------------------------------------------------------------------
// Resolution fallback ladder (highest to lowest).
// ---------------------------------------------------------------------------
static const uint8_t s_res_ladder[] = {
    CAM_RES_UXGA, CAM_RES_SXGA, CAM_RES_XGA, CAM_RES_SVGA, CAM_RES_VGA, CAM_RES_QVGA
};
static const int s_res_ladder_len = sizeof(s_res_ladder) / sizeof(s_res_ladder[0]);

// Return the next-lower resolution in the ladder, or -1 if none left.
static int next_lower_resolution(uint8_t current) {
    for (int i = 0; i < s_res_ladder_len; i++) {
        if (s_res_ladder[i] == current) {
            return (i + 1 < s_res_ladder_len) ? s_res_ladder[i + 1] : -1;
        }
    }
    return -1;  // not in ladder
}

// Try to bring up camera + full pipeline.  Returns true on success.
// On failure, cleans up partially-initialised state so a retry is safe.
static bool try_camera_and_pipeline(void) {
    // (Re)init camera with current settings.
    if (!cam_capture_init(&g_settings)) {
        ESP_LOGE(TAG, "Camera init failed at resolution %d", g_settings.resolution);
        return false;
    }

    if (!setup_pipeline()) {
        ESP_LOGE(TAG, "Pipeline setup failed at resolution %d", g_settings.resolution);
        // Camera is initialised but pipeline alloc failed — tear down camera
        // to free PSRAM for the next attempt.
        cam_capture_release();
        esp_camera_deinit();
        return false;
    }
    return true;
}

// Attempt camera + pipeline with automatic resolution fallback.
// Returns true if at least one resolution succeeds.
static bool init_camera_with_fallback(void) {
    // First attempt: user-configured resolution.
    if (try_camera_and_pipeline()) return true;

    // Walk down the resolution ladder trying progressively lower resolutions.
    uint8_t current = g_settings.resolution;
    while (true) {
        int next = next_lower_resolution(current);
        if (next < 0) break;  // nothing lower to try

        current = (uint8_t)next;
        g_settings.resolution = current;
        ESP_LOGW(TAG, "Falling back to capture resolution %d", current);

        if (try_camera_and_pipeline()) {
            ESP_LOGW(TAG, "Pipeline OK after fallback to resolution %d — "
                     "saving updated settings", current);
            cam_settings_save(&g_settings);
            return true;
        }
    }

    ESP_LOGE(TAG, "All capture resolutions exhausted — cannot start camera");
    return false;
}
static bool setup_pipeline(void) {
    uint16_t cap_w, cap_h;
    cam_capture_get_resolution(&cap_w, &cap_h);

    return setup_pipeline_with_capture_dims(cap_w, cap_h);
}

static bool setup_pipeline_with_capture_dims(uint16_t cap_w, uint16_t cap_h) {
    if (cap_w == 0 || cap_h == 0) {
        ESP_LOGE(TAG, "Invalid capture dimensions for pipeline: %ux%u", cap_w, cap_h);
        return false;
    }

    // Re-emit one-shot resize probe after every pipeline rebuild.
    g_logged_color_probe = false;

    // Determine output dimensions.  Priority:
    //  1. Last client handshake with a non-"don't-care" resolution
    //  2. WebUI default (output_width/output_height > 0)
    //  3. Fall back to capture resolution
    uint16_t out_w, out_h;
    if (g_output_w > 0 && g_output_h > 0) {
        out_w = g_output_w;
        out_h = g_output_h;
    } else if (g_settings.output_width > 0 && g_settings.output_height > 0) {
        out_w = g_settings.output_width;
        out_h = g_settings.output_height;
    } else {
        out_w = cap_w;
        out_h = cap_h;
    }

    ESP_LOGI(TAG, "Setting up pipeline: capture %ux%u → output %ux%u, %dx%d tiles",
             cap_w, cap_h, out_w, out_h, g_settings.tiles_x, g_settings.tiles_y);

    ESP_LOGI(TAG, "Pipeline settings: swap_rb=%d swap_bytes=%d jpeg_q=%d",
             g_settings.swap_rb, g_settings.swap_bytes, g_settings.jpeg_quality);

    // Homography LUT — maps from output pixels directly to capture pixels.
    if (g_transform) cam_transform_destroy(g_transform);
    g_transform = cam_transform_create(cap_w, cap_h, out_w, out_h, g_settings.homography);
    if (!g_transform) return false;

    // Diff engine works on the output (warped) frame dimensions.
    if (g_diff) cam_diff_destroy(g_diff);
    g_diff = cam_diff_create(out_w, out_h, g_settings.tiles_x, g_settings.tiles_y);
    if (!g_diff) return false;

    // Warped frame buffer (output dimensions).
    if (!allocate_buffers(out_w, out_h)) return false;

    // Apply channel-swap preference (must happen after diff engine is created).
    cam_diff_set_swap_rb(g_diff, g_settings.swap_rb);
    cam_diff_set_swap_bytes(g_diff, g_settings.swap_bytes);
    cam_diff_set_invert_colors(g_diff, g_settings.invert_colors);

    return true;
}

// ---------------------------------------------------------------------------
// Process one frame: capture → transform → [temporal denoise] → diff → send
// ---------------------------------------------------------------------------
static void process_frame(void) {
    uint8_t *raw_buf = NULL;
    size_t   raw_len = 0;
    uint16_t fw, fh;

    cam_led_set(CAM_LED_CAPTURING);

    // 1. Capture RGB565.
    if (!cam_capture_rgb565(&raw_buf, &raw_len, &fw, &fh)) {
        cam_led_set(CAM_LED_ERROR);
        return;
    }

    uint16_t src_w = 0, src_h = 0;
    cam_transform_get_src_size(g_transform, &src_w, &src_h);
    if (fw != src_w || fh != src_h) {
        ESP_LOGW(TAG,
                 "Capture size mismatch: transform src=%ux%u, camera fb=%ux%u; rebuilding pipeline",
                 src_w, src_h, fw, fh);
        if (!setup_pipeline_with_capture_dims(fw, fh)) {
            ESP_LOGE(TAG, "Pipeline rebuild failed for actual capture size %ux%u", fw, fh);
            cam_capture_release();
            cam_led_set(CAM_LED_ERROR);
            return;
        }
        g_force_keyframe = true;
    }

    // 2. Apply perspective transform (capture → output dimensions).
    uint16_t out_w = 0, out_h = 0;
    cam_transform_get_size(g_transform, &out_w, &out_h);
    cam_transform_apply(g_transform,
                        (const uint16_t *)raw_buf,
                        g_warped_buf);

    if (!g_logged_color_probe && (fw != out_w || fh != out_h)) {
        log_resize_color_probe((const uint16_t *)raw_buf, fw, fh,
                               g_warped_buf, out_w, out_h);
        g_logged_color_probe = true;
    }

    // Tick the AEC re-lock cycle now that the frame has been consumed.
    // If the frame was taken during a re-sample unlock we force a keyframe
    // so the pendant gets a clean baseline at the new exposure level.
    bool ae_resampled = cam_capture_tick_ae();
    cam_capture_release();

    // 2b. Temporal denoise — EMA blend with previous warped frame.
#if CAM_ENH_TEMPORAL_DENOISE
    uint16_t out_w, out_h;
    cam_transform_get_size(g_transform, &out_w, &out_h);
    size_t px_count = (size_t)out_w * out_h;

    if (g_temporal_prev && g_temporal_valid) {
        // Blend: out[i] = (ALPHA * cur + INV * prev) >> 8
        // Camera outputs big-endian RGB565; byte-swap for correct channel
        // decomposition on the little-endian ESP32, then swap back.
        for (size_t i = 0; i < px_count; i++) {
            uint16_t cur  = __builtin_bswap16(g_warped_buf[i]);
            uint16_t prev = __builtin_bswap16(g_temporal_prev[i]);
            // Decompose RGB565
            uint32_t cr = (cur >> 11) & 0x1F, cg = (cur >> 5) & 0x3F, cb = cur & 0x1F;
            uint32_t pr = (prev >> 11) & 0x1F, pg = (prev >> 5) & 0x3F, pb = prev & 0x1F;
            uint32_t r = (TEMPORAL_ALPHA * cr + TEMPORAL_INV * pr + 128u) >> 8;
            uint32_t g = (TEMPORAL_ALPHA * cg + TEMPORAL_INV * pg + 128u) >> 8;
            uint32_t b = (TEMPORAL_ALPHA * cb + TEMPORAL_INV * pb + 128u) >> 8;
            g_warped_buf[i] = __builtin_bswap16((uint16_t)((r << 11) | (g << 5) | b));
        }
    }

    // Store current frame as the new "previous".
    if (g_temporal_prev) {
        memcpy(g_temporal_prev, g_warped_buf, px_count * sizeof(uint16_t));
        g_temporal_valid = true;
    }
#endif // CAM_ENH_TEMPORAL_DENOISE

    // 3. Diff against previous frame.
    //    Use the output (warped) dimensions, NOT the capture dimensions.
    uint16_t diff_w = out_w;
    uint16_t diff_h = out_h;

    bool keyframe = g_force_keyframe || ae_resampled ||
                    (g_settings.keyframe_interval > 0 &&
                     (g_frame_id % g_settings.keyframe_interval) == 0);
    g_force_keyframe = false;

    cam_diff_process(g_diff, g_warped_buf, diff_w, diff_h,
                     g_settings.jpeg_quality,
                     g_settings.diff_threshold,
                     keyframe);

    const cam_diff_result_t *result = cam_diff_get_result(g_diff);

    // 4. Send via ESP-NOW.
    // Always send a frame message to the pendant, even when 0 tiles changed.
    // Sending FRAME_START(num_tiles=0)+FRAME_END releases the pendant's
    // frame_in_flight backpressure flag so it keeps sending REQUEST_FRAME.
    // Without this, a static scene (0/N tiles changed) causes the pendant to
    // stall waiting for a reply that never arrives, and the camera eventually
    // hits its 15-second idle timeout and drops the streaming session.
    if (result) {
        const uint8_t *dst = (g_settings.hub_mac[0] | g_settings.hub_mac[1] |
                              g_settings.hub_mac[2] | g_settings.hub_mac[3] |
                              g_settings.hub_mac[4] | g_settings.hub_mac[5])
                             ? g_settings.hub_mac : NULL;

        if (result->num_changed > 0) {
            cam_led_set(CAM_LED_SENDING);
            ESP_LOGI(TAG, "Sending frame id=%u type=%s tiles=%u swap_rb=%d swap_bytes=%d jpeg_q=%d",
                     g_frame_id,
                     result->is_keyframe ? "KEY" : "DIFF",
                     result->num_changed,
                     g_settings.swap_rb,
                     g_settings.swap_bytes,
                     g_settings.jpeg_quality);
            bool sent_ok = cam_espnow_send_frame(dst, g_frame_id, result,
                                                 g_settings.send_interval_ms);
            if (!sent_ok) {
                // TX queue was saturated (peer likely restarted mid-frame).
                // Force a full keyframe once the peer reconnects so the
                // pendant gets a clean baseline rather than a partial diff.
                g_force_keyframe = true;
                ESP_LOGW(TAG, "Frame aborted — will retry as keyframe");
            } else {
                g_frames_sent++;
            }
        } else {
            // No tiles changed — send a lightweight empty FRAME_START+FRAME_END
            // (no tile chunks) so the pendant's backpressure is released.
            cam_espnow_send_frame(dst, g_frame_id, result, 0);
        }
    }

    cam_diff_free_tiles(g_diff);
    g_frame_id++;
    g_last_frame_ms = millis();

    cam_led_set(CAM_LED_IDLE);
}

// ---------------------------------------------------------------------------
// LED + status heartbeat task
// ---------------------------------------------------------------------------
static void led_task(void *arg) {
    (void)arg;
    while (true) {
        cam_led_tick();
        // Batch LED updates at 50ms to reduce RMT activity and CPU wakeups.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ---------------------------------------------------------------------------
// Arduino setup()
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    esp_log_set_vprintf(&serial_log_vprintf);
    delay(500);
    Serial.println();
    Serial.println("[cam_main] UART console online");
    Serial.flush();
    ESP_LOGI(TAG, "=== CNC Camera Companion ===");
    ESP_LOGI(TAG, "Build: " __DATE__ " " __TIME__);
    ESP_LOGI(TAG,
             "Enh flags: ENH=%d BIL=%d AREA=%d TEMP=%d RGB888=%d AB=%d",
             CAM_ENHANCED_PROCESSING,
             CAM_ENH_BILINEAR,
             CAM_ENH_AREA_AVERAGE,
             CAM_ENH_TEMPORAL_DENOISE,
             CAM_ENH_RGB888_CAPTURE,
             CAM_ENH_ANTI_BANDING);

    // Reduce global verbosity to INFO to avoid low-level HAL spam (RMT, etc.)
    esp_log_level_set("*", ESP_LOG_INFO);
    // Enable DEBUG for companion modules so we still see app logs
    esp_log_level_set("cam_main", ESP_LOG_DEBUG);
    esp_log_level_set("cam_web", ESP_LOG_DEBUG);
    esp_log_level_set("cam_capture", ESP_LOG_DEBUG);
    // Silence noisy RMT/NeoPixel HAL verbose logs (they flood when LED updates are frequent)
    esp_log_level_set("esp32-hal-rmt", ESP_LOG_ERROR);

    cam_led_init();
    cam_led_set(CAM_LED_BOOT);
    // Load settings from NVS.
    cam_settings_init(&g_settings);
    // Set up BOOT button interrupt so a short press toggles AP/web UI at runtime.
    pinMode(CAM_BOOT_PIN, INPUT_PULLUP);
    delay(50); // debounce
    attachInterrupt(digitalPinToInterrupt(CAM_BOOT_PIN), boot_isr, FALLING);

    // Start LED animation task early so LED changes are visible during toggles.
    xTaskCreatePinnedToCore(led_task, "led", 2048, NULL,
                            tskIDLE_PRIORITY + 1, NULL, 0);

    // If not calibrated, start web UI at boot so user can configure.
    if (!g_settings.calibrated) {
        ESP_LOGI(TAG, ">>> STARTING CONFIGURATION MODE (no calibration) <<<");
        cam_led_set(CAM_LED_CONFIG_MODE);
        cam_webserver_start(&g_settings);

        // Block here while user configures via web UI (previous behavior).
        while (cam_webserver_is_running()) {
            cam_webserver_handle();
            cam_led_tick();
            delay(10);
        }
    }

    // --- Normal camera mode ---
    ESP_LOGI(TAG, ">>> NORMAL MODE <<<");

    // Init camera hardware with automatic resolution fallback.
    // If the configured resolution exceeds available PSRAM, progressively
    // lower resolutions are tried until one succeeds (or we run out).
    bool camera_ok = init_camera_with_fallback();
    if (!camera_ok) {
        ESP_LOGE(TAG, "Camera + pipeline init failed at all resolutions!");
        ESP_LOGW(TAG, "Device will continue without camera — "
                 "use BOOT button or reflash to reconfigure.");
        cam_led_set(CAM_LED_ERROR);
        // Don't block here — fall through so ESP-NOW and BOOT button still work.
    }

    // Init ESP-NOW.
    if (!cam_espnow_init(g_settings.wifi_channel, on_espnow_recv)) {
        ESP_LOGE(TAG, "ESP-NOW init failed!");
        cam_led_set(CAM_LED_ERROR);
        // Continue anyway — the device stays alive for web UI reconfiguration.
    } else {
        g_espnow_ready = true;
    }

    // Re-register the saved hub MAC as an ESP-NOW peer so heartbeats and
    // frame replies can be sent immediately without waiting for the pendant
    // to send a packet first.
    bool hub_known = (g_settings.hub_mac[0] | g_settings.hub_mac[1] |
                      g_settings.hub_mac[2] | g_settings.hub_mac[3] |
                      g_settings.hub_mac[4] | g_settings.hub_mac[5]) != 0;
    if (hub_known) {
        cam_espnow_add_peer(g_settings.hub_mac);
        ESP_LOGI(TAG, "Restored saved peer: " MACSTR, MAC2STR(g_settings.hub_mac));
    }

    // Pipeline was already built inside init_camera_with_fallback().
    // AEC/AGC periodic re-lock is disabled — leaving auto-exposure running
    // continuously avoids brightness step artefacts in the diff and is more
    // practical for typical workshop lighting conditions.
    // cam_capture_set_ae_lock_interval(30);

    cam_led_set(CAM_LED_NO_PEER);
    g_fps_timer_ms = millis();

    ESP_LOGI(TAG, "Ready. Waiting for pendant frame requests...");
}

// ---------------------------------------------------------------------------
// Arduino loop()
// ---------------------------------------------------------------------------
void loop() {
    // ---------------------------------------------------------------------------
    // BOOT button handler
    //
    // Short press  (<2 s)  : toggle AP / camera mode (existing behaviour)
    // Hold 2–5 s          : LED switches to fast orange blink as a warning
    // Hold ≥5 s           : factory reset — erase NVS, restore defaults, reboot
    // Release 2–5 s       : cancelled — LED restored, no action taken
    // ---------------------------------------------------------------------------

    // Arm the hold tracker on the first debounced FALLING edge.
    if (g_boot_pressed && !g_boot_hold_armed) {
        g_boot_pressed = false;
        uint32_t now = millis();
        if (now - g_last_boot_press_ms > 300) {  // 300 ms debounce
            g_last_boot_press_ms = now;           // hold start time
            g_boot_hold_armed = true;
        }
    } else if (g_boot_pressed) {
        g_boot_pressed = false;  // discard edge while hold already tracked
    }

    if (g_boot_hold_armed) {
        uint32_t now      = millis();
        uint32_t held_ms  = now - g_last_boot_press_ms;
        bool     released = (digitalRead(CAM_BOOT_PIN) == HIGH);

        if (released) {
            g_boot_hold_armed = false;

            if (held_ms < 2000) {
                // ---- Short press: toggle AP/camera mode ----
                if (cam_webserver_is_running()) {
                    ESP_LOGI(TAG, "BOOT short press: stopping AP/web UI");
                    cam_webserver_stop();
                    cam_led_set(CAM_LED_IDLE);
                    if (!g_espnow_ready) {
                        if (cam_espnow_init(g_settings.wifi_channel, on_espnow_recv)) {
                            g_espnow_ready = true;
                            ESP_LOGI(TAG, "ESP-NOW reinitialized after AP mode");
                        } else {
                            g_espnow_ready = false;
                            ESP_LOGW(TAG, "ESP-NOW reinit failed after AP mode; status tx disabled");
                        }
                    }
                } else {
                    ESP_LOGI(TAG, "BOOT short press: starting AP/web UI");
                    if (g_espnow_ready) {
                        cam_espnow_deinit();
                        g_espnow_ready = false;
                    }
                    cam_led_set(CAM_LED_CONFIG_MODE);
                    cam_webserver_start(&g_settings);
                }
            } else {
                // ---- Hold 2–5 s released early: cancelled ----
                ESP_LOGI(TAG, "BOOT hold cancelled after %lums — no action",
                         (unsigned long)held_ms);
                cam_led_set(cam_webserver_is_running() ? CAM_LED_CONFIG_MODE
                                                       : CAM_LED_NO_PEER);
            }
        } else {
            // Still held — update LED and check for factory reset threshold.
            if (held_ms >= 2000) {
                cam_led_set(CAM_LED_FACTORY_RESET);  // fast orange blink warning
            }
            if (held_ms >= 5000) {
                // ---- Factory reset ----
                ESP_LOGW(TAG, "=== FACTORY RESET: erasing NVS settings ===");
                cam_settings_erase();
                cam_settings_defaults(&g_settings);
                cam_settings_save(&g_settings);
                delay(800);  // let LED blink a few more times before rebooting
                ESP.restart();
            }
        }
    }

    // If webserver/AP is active, service it and skip normal camera loop.
    if (cam_webserver_is_running()) {
        cam_webserver_handle();
        cam_led_tick();
        delay(10);
        return;
    }
    // Idle timeout: if no frame request has been received for 15 seconds while
    // streaming is supposedly active, reset the streaming state.  This handles
    // the case where the pendant reboots silently without sending STREAM_STOP.
    if (g_streaming && g_last_request_ms != 0) {
        uint32_t now_check = millis();
        if (now_check - g_last_request_ms > 15000) {
            ESP_LOGI(TAG, "Stream idle timeout — resetting streaming state");
            g_streaming       = false;
            g_frame_requested = false;
            g_force_keyframe  = true;  // Next session starts with keyframe
            cam_led_set(CAM_LED_NO_PEER);
        }
    }

    // If the output resolution changed (e.g. a new STREAM_START with different
    // desired dimensions), rebuild the transform LUT, diff engine, and PSRAM
    // buffers before the next frame is captured.  This must run on the main
    // task (not the WiFi callback) because it does heap allocations.
    if (g_pipeline_dirty && cam_capture_is_ready()) {
        g_pipeline_dirty = false;
        ESP_LOGI(TAG, "Output resolution changed — rebuilding pipeline");
        if (setup_pipeline()) {
            g_force_keyframe = true;
        } else {
            ESP_LOGE(TAG, "Pipeline rebuild failed after resolution change");
        }
    }

    // Poll-driven: only capture and send when the pendant requests a frame
    // and the camera pipeline is operational.
    if (g_frame_requested && cam_capture_is_ready() && g_diff) {
        uint32_t now = millis();
        const uint32_t min_frame_interval = (CAM_TARGET_FPS > 0) ? (1000 / CAM_TARGET_FPS) : 0;
        if (min_frame_interval == 0 || now - g_last_frame_ms >= min_frame_interval) {
            g_frame_requested = false;
            process_frame();
        } else {
            // Too soon to send next frame; skip this tick. Leave g_frame_requested
            // set so we'll attempt again on the next loop iteration.
        }
    }

    // Periodic status heartbeat (every ~2 seconds).
    uint32_t now = millis();
    static uint32_t last_heartbeat = 0;
    if (now - last_heartbeat > 2000) {
        last_heartbeat = now;

        // Calculate FPS.
        uint32_t elapsed = now - g_fps_timer_ms;
        if (elapsed > 0) {
            g_fps_x10 = (uint8_t)((g_frames_sent * 10000UL) / elapsed);
        }

        bool has_peer = (g_settings.hub_mac[0] | g_settings.hub_mac[1] |
                         g_settings.hub_mac[2] | g_settings.hub_mac[3] |
                         g_settings.hub_mac[4] | g_settings.hub_mac[5]) != 0;

        if (!has_peer && !g_frame_requested) {
            cam_led_set(CAM_LED_NO_PEER);
        }

        if (g_espnow_ready) {
            cam_espnow_send_status(has_peer ? g_settings.hub_mac : NULL,
                                   has_peer ? CAM_STATUS_IDLE : CAM_STATUS_IDLE,
                                   g_frame_id, g_fps_x10);
            // Send grid mapping at session start (up to 3 times to handle
            // packet loss), then stop until the next STREAM_START.
            if (g_settings.grid_calibrated && g_grid_send_count < 3) {
                uint16_t pts = g_settings.grid_points_count;
                if (pts > 0 && pts <= CAM_SETTINGS_MAX_GRID_POINTS) {
                    int8_t offsets[CAM_SETTINGS_MAX_GRID_POINTS * 2];
                    uint16_t nx = g_settings.grid_nx;
                    uint16_t ny = g_settings.grid_ny;
                    uint16_t img_w = 0, img_h = 0;
                    if (g_transform) cam_transform_get_size(g_transform, &img_w, &img_h);
                    if (img_w == 0 || img_h == 0 || g_settings.surface_width <= 0.0f || g_settings.surface_height <= 0.0f) {
                        for (uint16_t i = 0; i < pts; i++) { offsets[i*2] = 0; offsets[i*2+1] = 0; }
                    } else {
                        // Active area after image margins
                        uint16_t il = g_settings.image_margin_left;
                        uint16_t it = g_settings.image_margin_top;
                        uint16_t ir = g_settings.image_margin_right;
                        uint16_t ib = g_settings.image_margin_bottom;
                        float active_w = (float)((int)img_w - (int)il - (int)ir);
                        float active_h = (float)((int)img_h - (int)it - (int)ib);
                        if (active_w < 1.0f) active_w = 1.0f;
                        if (active_h < 1.0f) active_h = 1.0f;

                        for (uint16_t i = 0; i < pts; i++) {
                            uint16_t ixx = i % nx;
                            uint16_t iyy = i / nx;
                            // Ideal pixel position within the inset area
                            float frac_x = (nx > 1) ? ((float)ixx / (float)(nx - 1)) : 0.0f;
                            float frac_y = (ny > 1) ? ((float)iyy / (float)(ny - 1)) : 0.0f;
                            float ideal_px = (float)il + frac_x * (active_w - 1.0f);
                            float ideal_py = (float)it + frac_y * (active_h - 1.0f);
                            // grid_points are output-normalised (0..1 over [0..img_w-1]).
                            float actual_px = g_settings.grid_points[i][0] * (float)(img_w - 1);
                            float actual_py = g_settings.grid_points[i][1] * (float)(img_h - 1);
                            int dx_px = (int)lroundf(actual_px - ideal_px);
                            int dy_px = (int)lroundf(actual_py - ideal_py);
                            if (dx_px < -127) dx_px = -127; if (dx_px > 127) dx_px = 127;
                            if (dy_px < -127) dy_px = -127; if (dy_px > 127) dy_px = 127;
                            offsets[i*2 + 0] = (int8_t)dx_px;
                            offsets[i*2 + 1] = (int8_t)dy_px;
                        }
                    }
                    const uint8_t *dst = (g_settings.hub_mac[0]||g_settings.hub_mac[1]||g_settings.hub_mac[2]||g_settings.hub_mac[3]||g_settings.hub_mac[4]||g_settings.hub_mac[5]) ? g_settings.hub_mac : NULL;
                    cam_espnow_send_grid_compact(dst,
                                                 g_settings.surface_width, g_settings.surface_height,
                                                 g_settings.grid_dx, g_settings.grid_dy,
                                                 g_settings.grid_nx, g_settings.grid_ny,
                                                 img_w, img_h,
                                                 g_settings.image_margin_left, g_settings.image_margin_top,
                                                 g_settings.image_margin_right, g_settings.image_margin_bottom,
                                                 offsets, pts);
                    g_grid_send_count++;
                }
            }
        }

        // Reset FPS counter every 10 seconds.
        if (elapsed > 10000) {
            g_frames_sent  = 0;
            g_fps_timer_ms = now;
        }
    }

    // Yield to other tasks.
    delay(1);
}
