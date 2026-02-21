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
    case CAM_CMD_REQUEST_FRAME: {
        g_frame_requested = true;
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
    cam_capture_release();

    // 3. Diff against previous frame.
    bool keyframe = g_force_keyframe ||
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

    // Build pipeline (LUT + diff engine + buffers).
    if (!setup_pipeline()) {
        ESP_LOGE(TAG, "Pipeline setup failed!");
        cam_led_set(CAM_LED_ERROR);
        while (true) { cam_led_tick(); delay(20); }
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
    // Poll-driven: only capture and send when the pendant requests a frame.
    if (g_frame_requested) {
        g_frame_requested = false;
        process_frame();
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
