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

static uint16_t g_frame_id   = 0;
static bool     g_frame_requested = false;
static bool     g_force_keyframe  = false;
static bool     g_espnow_ready    = false;
static uint32_t g_last_frame_ms   = 0;
static uint32_t g_frames_sent     = 0;
static uint32_t g_fps_timer_ms    = 0;
static uint8_t  g_fps_x10        = 0;

// Streaming session state:
//   g_streaming = true  → pendant has sent STREAM_START; accept REQUEST_FRAME
//   g_streaming = false → idle; next REQUEST_FRAME or STREAM_START restarts a session
static bool     g_streaming        = false;
static uint32_t g_last_request_ms  = 0;  // millis() of last received frame request

// BOOT button press flag (set from ISR)
static volatile bool g_boot_pressed = false;
static uint32_t g_last_boot_press_ms = 0;

// ISR for BOOT button (minimal work)
static void IRAM_ATTR boot_isr(void) {
    g_boot_pressed = true;
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
            const cam_config_cmd_t *cfg = (const cam_config_cmd_t *)data;
            cam_settings_apply_config(&g_settings, cfg);
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
static bool allocate_buffers(uint16_t w, uint16_t h) {
    size_t frame_bytes = (size_t)w * h * sizeof(uint16_t);

    if (g_warped_buf) heap_caps_free(g_warped_buf);
    g_warped_buf = (uint16_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM);
    if (!g_warped_buf) {
        ESP_LOGE(TAG, "Failed to alloc warped buffer (%zu bytes)", frame_bytes);
        return false;
    }
    memset(g_warped_buf, 0, frame_bytes);
    return true;
}

// ---------------------------------------------------------------------------
// Build / rebuild the pipeline after settings change
// ---------------------------------------------------------------------------
static bool setup_pipeline(void) {
    uint16_t w, h;
    cam_capture_get_resolution(&w, &h);

    ESP_LOGI(TAG, "Setting up pipeline: %ux%u, %dx%d tiles",
             w, h, g_settings.tiles_x, g_settings.tiles_y);

    ESP_LOGI(TAG, "Pipeline settings: swap_rb=%d swap_bytes=%d jpeg_q=%d",
             g_settings.swap_rb, g_settings.swap_bytes, g_settings.jpeg_quality);

    // Homography LUT.
    if (g_transform) cam_transform_destroy(g_transform);
    g_transform = cam_transform_create(w, h, w, h, g_settings.homography);
    if (!g_transform) return false;

    // Diff engine.
    if (g_diff) cam_diff_destroy(g_diff);
    g_diff = cam_diff_create(w, h, g_settings.tiles_x, g_settings.tiles_y);
    if (!g_diff) return false;

    // Warped frame buffer.
    if (!allocate_buffers(w, h)) return false;

    // Apply channel-swap preference (must happen after diff engine is created).
    cam_diff_set_swap_rb(g_diff, g_settings.swap_rb);
    cam_diff_set_swap_bytes(g_diff, g_settings.swap_bytes);
    cam_diff_set_invert_colors(g_diff, g_settings.invert_colors);

    return true;
}

// ---------------------------------------------------------------------------
// Process one frame: capture → transform → diff → send
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

    // 2. Apply perspective transform.
    cam_transform_apply(g_transform,
                        (const uint16_t *)raw_buf,
                        g_warped_buf);

    // Tick the AEC re-lock cycle now that the frame has been consumed.
    // If the frame was taken during a re-sample unlock we force a keyframe
    // so the pendant gets a clean baseline at the new exposure level.
    bool ae_resampled = cam_capture_tick_ae();
    cam_capture_release();

    // 3. Diff against previous frame.
    bool keyframe = g_force_keyframe || ae_resampled ||
                    (g_settings.keyframe_interval > 0 &&
                     (g_frame_id % g_settings.keyframe_interval) == 0);
    g_force_keyframe = false;

    cam_diff_process(g_diff, g_warped_buf, fw, fh,
                     g_settings.jpeg_quality,
                     g_settings.diff_threshold,
                     keyframe);

    const cam_diff_result_t *result = cam_diff_get_result(g_diff);

    // 4. Send via ESP-NOW.
    if (result && result->num_changed > 0) {
        cam_led_set(CAM_LED_SENDING);

        const uint8_t *dst = (g_settings.hub_mac[0] | g_settings.hub_mac[1] |
                              g_settings.hub_mac[2] | g_settings.hub_mac[3] |
                              g_settings.hub_mac[4] | g_settings.hub_mac[5])
                             ? g_settings.hub_mac : NULL;

        ESP_LOGI(TAG, "Sending frame id=%u type=%s tiles=%u swap_rb=%d swap_bytes=%d jpeg_q=%d",
                 g_frame_id,
                 result->is_keyframe ? "KEY" : "DIFF",
                 result->num_changed,
                 g_settings.swap_rb,
                 g_settings.swap_bytes,
                 g_settings.jpeg_quality);

        cam_espnow_send_frame(dst, g_frame_id, result,
                              g_settings.send_interval_ms);
        g_frames_sent++;
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

    // Init camera hardware.
    if (!cam_capture_init(&g_settings)) {
        ESP_LOGE(TAG, "Camera init failed!");
        cam_led_set(CAM_LED_ERROR);
        while (true) { cam_led_tick(); delay(20); }
    }

    // Init ESP-NOW.
    if (!cam_espnow_init(g_settings.wifi_channel, on_espnow_recv)) {
        ESP_LOGE(TAG, "ESP-NOW init failed!");
        cam_led_set(CAM_LED_ERROR);
        while (true) { cam_led_tick(); delay(20); }
    }
    g_espnow_ready = true;

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

    // Build pipeline (LUT + diff engine + buffers).
    if (!setup_pipeline()) {
        ESP_LOGE(TAG, "Pipeline setup failed!");
        cam_led_set(CAM_LED_ERROR);
        while (true) { cam_led_tick(); delay(20); }
    }

    // Lock AEC/AGC between frames; re-sample every 30 captures.
    // This prevents the sensor's per-frame gain/shutter adjustment from
    // producing a uniform brightness step that the diff mistakes for scene
    // change.  Only engages when AEC/AGC are enabled in settings.
    if (g_settings.aec_enable || g_settings.agc_enable) {
        cam_capture_set_ae_lock_interval(30);
    }

    cam_led_set(CAM_LED_NO_PEER);
    g_fps_timer_ms = millis();

    ESP_LOGI(TAG, "Ready. Waiting for pendant frame requests...");
}

// ---------------------------------------------------------------------------
// Arduino loop()
// ---------------------------------------------------------------------------
void loop() {
    // Handle BOOT button toggle (debounced)
    if (g_boot_pressed) {
        g_boot_pressed = false;
        uint32_t now = millis();
        if (now - g_last_boot_press_ms > 300) {
            g_last_boot_press_ms = now;
            if (cam_webserver_is_running()) {
                ESP_LOGI(TAG, "BOOT pressed: stopping AP/web UI");
                cam_webserver_stop();
                cam_led_set(CAM_LED_IDLE);

                // AP mode switches Wi-Fi state; bring ESP-NOW back up.
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
                ESP_LOGI(TAG, "BOOT pressed: starting AP/web UI");

                // Suspend ESP-NOW while AP config UI is active.
                if (g_espnow_ready) {
                    cam_espnow_deinit();
                    g_espnow_ready = false;
                }

                cam_led_set(CAM_LED_CONFIG_MODE);
                cam_webserver_start(&g_settings);
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

    // Poll-driven: only capture and send when the pendant requests a frame.
    if (g_frame_requested) {
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
            // Periodically broadcast grid mapping if available
            if (g_settings.grid_calibrated) {
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
                        for (uint16_t i = 0; i < pts; i++) {
                            uint16_t ix = i % nx;
                            uint16_t iy = i / nx;
                            float real_x = g_settings.grid_minx + (float)ix * g_settings.grid_dx;
                            float real_y = g_settings.grid_miny + (float)iy * g_settings.grid_dy;
                            float ideal_px = (real_x / g_settings.surface_width) * (float)img_w;
                            float ideal_py = (real_y / g_settings.surface_height) * (float)img_h;
                            float actual_px = g_settings.grid_points[i][0] * (float)img_w;
                            float actual_py = g_settings.grid_points[i][1] * (float)img_h;
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
                                                 offsets, pts);
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
