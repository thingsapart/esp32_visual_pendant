// cam_espnow.cpp — ESP-NOW init + rate-limited frame sending

#include "cam_espnow.h"
#include "cam_protocol.h"
#include "app_log.h"

#include <string.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

static const char *TAG = "cam_espnow";
static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static cam_espnow_recv_cb_t s_recv_cb = NULL;

// ---------------------------------------------------------------------------
// Internal callbacks
// ---------------------------------------------------------------------------
static void on_data_recv(const esp_now_recv_info_t *info,
                         const uint8_t *data, int len) {
    if (s_recv_cb && info && data && len > 0) {
        s_recv_cb(info->src_addr, data, len);
    }
}

static void on_data_sent(const uint8_t *mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "Send failed to " MACSTR, MAC2STR(mac));
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
bool cam_espnow_init(uint8_t wifi_channel, cam_espnow_recv_cb_t recv_cb) {
    s_recv_cb = recv_cb;

    // NVS (required by Wi-Fi).
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Network interface.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Wi-Fi in STA mode (required for ESP-NOW).
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Lock to the hub's channel.
    ESP_ERROR_CHECK(esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE));

    // ESP-NOW.
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));

    // Add broadcast peer so we can always broadcast.
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, BROADCAST_MAC, 6);
    bcast.channel = 0;
    bcast.ifidx   = WIFI_IF_STA;
    bcast.encrypt = false;
    esp_now_add_peer(&bcast);

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "ESP-NOW ready on ch%d, MAC=" MACSTR,
             wifi_channel, MAC2STR(mac));
    return true;
}

void cam_espnow_deinit(void) {
    esp_now_deinit();
    esp_wifi_stop();
}

// ---------------------------------------------------------------------------
// Peer management
// ---------------------------------------------------------------------------
bool cam_espnow_add_peer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_peer failed: %d", err);
        return false;
    }
    ESP_LOGI(TAG, "Peer added: " MACSTR, MAC2STR(mac));
    return true;
}

bool cam_espnow_learn_peer(const uint8_t *received_mac, uint8_t *stored_mac) {
    bool is_empty = true;
    for (int i = 0; i < 6; i++) {
        if (stored_mac[i] != 0) { is_empty = false; break; }
    }

    if (is_empty) {
        memcpy(stored_mac, received_mac, 6);
        cam_espnow_add_peer(received_mac);
        ESP_LOGI(TAG, "Learned peer: " MACSTR, MAC2STR(received_mac));
        return true;
    }

    if (memcmp(received_mac, stored_mac, 6) == 0) return true;

    ESP_LOGW(TAG, "Ignoring unknown peer " MACSTR, MAC2STR(received_mac));
    return false;
}

// ---------------------------------------------------------------------------
// Raw send
// ---------------------------------------------------------------------------
bool cam_espnow_send(const uint8_t *mac, const uint8_t *data, size_t len) {
    if (len > CAM_ESPNOW_MAX_DATA) {
        ESP_LOGE(TAG, "Payload too large: %zu > %d", len, CAM_ESPNOW_MAX_DATA);
        return false;
    }
    const uint8_t *dst = mac ? mac : BROADCAST_MAC;
    esp_err_t err = esp_now_send(dst, data, (size_t)len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send err=%d", err);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Send a complete frame (frame_start + tile chunks + frame_end)
// ---------------------------------------------------------------------------
bool cam_espnow_send_frame(const uint8_t *peer_mac,
                           uint16_t frame_id,
                           const cam_diff_result_t *diff,
                           uint8_t send_interval_ms) {
    const uint8_t *dst = peer_mac ? peer_mac : BROADCAST_MAC;
    uint8_t bitmap_bytes = cam_tile_bitmap_bytes(diff->tiles_x, diff->tiles_y);

    // --- 1. FRAME_START ---
    uint8_t start_buf[CAM_ESPNOW_MAX_DATA];
    cam_frame_start_msg_t *start = (cam_frame_start_msg_t *)start_buf;
    start->type         = CAM_MSG_FRAME_START;
    start->frame_id     = frame_id;
    start->frame_type   = diff->is_keyframe ? CAM_FRAME_KEYFRAME : CAM_FRAME_DIFF;
    start->img_width    = diff->img_w;
    start->img_height   = diff->img_h;
    start->tiles_x      = diff->tiles_x;
    start->tiles_y       = diff->tiles_y;
    start->encoding     = CAM_ENC_JPEG;
    start->jpeg_quality = 0; // Already encoded
    start->num_tiles    = diff->num_changed;
    memcpy(start->tile_bitmap, diff->changed_bitmap, bitmap_bytes);

    size_t start_len = CAM_FRAME_START_HEADER_SIZE + bitmap_bytes;
    if (!cam_espnow_send(dst, start_buf, start_len)) {
        ESP_LOGW(TAG, "Failed to send FRAME_START");
        return false;
    }

    if (send_interval_ms > 0)
        vTaskDelay(pdMS_TO_TICKS(send_interval_ms));

    // --- 2. TILE CHUNKS ---
    uint8_t total_tiles = diff->tiles_x * diff->tiles_y;
    for (uint8_t ti = 0; ti < total_tiles; ti++) {
        if (!(diff->changed_bitmap[ti / 8] & (1 << (ti % 8))))
            continue;

        const cam_tile_data_t *td = &diff->tiles[ti];
        if (!td->jpeg_buf || td->jpeg_len == 0) continue;

        // Split tile JPEG into chunks that fit in ESP-NOW MTU.
        size_t remaining = td->jpeg_len;
        size_t offset    = 0;
        uint8_t chunk_idx = 0;
        uint8_t total_chunks = (uint8_t)((remaining + CAM_TILE_CHUNK_MAX_PAYLOAD - 1)
                                          / CAM_TILE_CHUNK_MAX_PAYLOAD);

        while (remaining > 0) {
            size_t payload = (remaining > CAM_TILE_CHUNK_MAX_PAYLOAD)
                             ? CAM_TILE_CHUNK_MAX_PAYLOAD : remaining;

            uint8_t chunk_buf[CAM_ESPNOW_MAX_DATA];
            cam_tile_chunk_msg_t *chunk = (cam_tile_chunk_msg_t *)chunk_buf;
            chunk->type          = CAM_MSG_TILE_CHUNK;
            chunk->frame_id      = frame_id;
            chunk->tile_idx      = ti;
            chunk->chunk_idx     = chunk_idx;
            chunk->total_chunks  = total_chunks;
            chunk->tile_data_len = (uint16_t)td->jpeg_len;
            memcpy(chunk->data, td->jpeg_buf + offset, payload);

            size_t msg_len = CAM_TILE_CHUNK_HEADER_SIZE + payload;
            if (!cam_espnow_send(dst, chunk_buf, msg_len)) {
                ESP_LOGW(TAG, "Failed to send tile %d chunk %d", ti, chunk_idx);
                // Continue — don't abort entire frame for one dropped chunk.
            }

            offset    += payload;
            remaining -= payload;
            chunk_idx++;

            if (send_interval_ms > 0)
                vTaskDelay(pdMS_TO_TICKS(send_interval_ms));
        }
    }

    // --- 3. FRAME_END ---
    cam_frame_end_msg_t end_msg = {};
    end_msg.type       = CAM_MSG_FRAME_END;
    end_msg.frame_id   = frame_id;
    end_msg.tiles_sent = diff->num_changed;
    cam_espnow_send(dst, (const uint8_t *)&end_msg, sizeof(end_msg));

    return true;
}

// ---------------------------------------------------------------------------
// Send status heartbeat
// ---------------------------------------------------------------------------
bool cam_espnow_send_status(const uint8_t *peer_mac,
                            cam_device_status_t status,
                            uint16_t frame_id,
                            uint8_t fps_x10) {
    cam_status_msg_t msg = {};
    msg.type         = CAM_MSG_STATUS;
    msg.status       = (uint8_t)status;
    msg.frame_id     = frame_id;
    msg.fps_x10      = fps_x10;

    uint8_t ch;
    wifi_second_chan_t sch;
    esp_wifi_get_channel(&ch, &sch);
    msg.wifi_channel = ch;

    esp_wifi_get_mac(WIFI_IF_STA, msg.mac);

    const uint8_t *dst = peer_mac ? peer_mac : BROADCAST_MAC;
    return cam_espnow_send(dst, (const uint8_t *)&msg, sizeof(msg));
}

void cam_espnow_get_mac(uint8_t mac[6]) {
    esp_wifi_get_mac(WIFI_IF_STA, mac);
}

// ---------------------------------------------------------------------------
// Send grid mapping (simple single-packet format)
// Format: [type=CAM_MSG_GRID_MAP][minx][maxx][miny][maxy][dx][dy][nx(uint16_t)][ny(uint16_t)][count(uint16_t)][points...]
// points: count * (float x, float y) normalized
// If the total payload exceeds CAM_ESPNOW_MAX_DATA, the function returns false.
bool cam_espnow_send_grid(const uint8_t *peer_mac,
                          float minx, float maxx, float miny, float maxy,
                          float dx, float dy,
                          uint16_t nx, uint16_t ny,
                          const float *points_xy, uint16_t points_count) {
    // Header: 1 + 6*4 bytes for floats + 3*2 bytes = 1 + 24 + 6 = 31
    size_t header = 1 + 6 * sizeof(float) + 3 * sizeof(uint16_t);
    size_t points_bytes = (size_t)points_count * 2 * sizeof(float);
    size_t total = header + points_bytes;
    if (total > CAM_ESPNOW_MAX_DATA) {
        ESP_LOGW(TAG, "grid send: payload %zu > %d bytes, skipping",
                 total, CAM_ESPNOW_MAX_DATA);
        return false;
    }

    uint8_t buf[CAM_ESPNOW_MAX_DATA];
    size_t off = 0;
    buf[off++] = (uint8_t)CAM_MSG_GRID_MAP;

    auto write_f = [&](float v) {
        memcpy(&buf[off], &v, sizeof(float)); off += sizeof(float);
    };
    auto write_u16 = [&](uint16_t v) {
        memcpy(&buf[off], &v, sizeof(uint16_t)); off += sizeof(uint16_t);
    };

    write_f(minx); write_f(maxx); write_f(miny); write_f(maxy);
    write_f(dx); write_f(dy);
    write_u16(nx); write_u16(ny); write_u16(points_count);

    for (uint16_t i = 0; i < points_count; i++) {
        float x = points_xy[i * 2 + 0];
        float y = points_xy[i * 2 + 1];
        write_f(x); write_f(y);
    }

    const uint8_t *dst = peer_mac ? peer_mac : BROADCAST_MAC;
    esp_err_t err = esp_now_send(dst, buf, off);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send grid err=%d", err);
        return false;
    }
    return true;
}

// Compact grid send implementation (see prototype in header).
bool cam_espnow_send_grid_compact(const uint8_t *peer_mac,
                                  float w, float h, float dx, float dy,
                                  uint16_t nx, uint16_t ny,
                                  uint16_t img_w, uint16_t img_h,
                                  uint16_t inset_l, uint16_t inset_t,
                                  uint16_t inset_r, uint16_t inset_b,
                                  const int8_t *offsets_xy, uint16_t points_count) {
    // Layout v3: [type:1][w:4][h:4][dx:4][dy:4][nx:2][ny:2][count:2][img_w:2][img_h:2]
    //            [inset_l:2][inset_t:2][inset_r:2][inset_b:2][offsets: count*2]
    size_t header = 1 + 4 * sizeof(float) + 3 * sizeof(uint16_t)
                    + 2 * sizeof(uint16_t)   // img_w, img_h
                    + 4 * sizeof(uint16_t);  // insets
    size_t payload = (size_t)points_count * 2 * sizeof(int8_t);
    size_t total = header + payload;
    if (total > CAM_ESPNOW_MAX_DATA) {
        // Truncate offset data to fit within the ESP-NOW MTU.
        // The receiver will zero-fill offsets for the remaining points.
        size_t max_payload = CAM_ESPNOW_MAX_DATA - header;
        uint16_t max_pts = (uint16_t)(max_payload / 2);
        ESP_LOGW(TAG, "grid send (compact): truncated %u→%u of %u*%u points (MTU %d)",
                 points_count, max_pts, nx, ny, CAM_ESPNOW_MAX_DATA);
        points_count = max_pts;
        payload = (size_t)points_count * 2;
        total = header + payload;
    }

    uint8_t buf[CAM_ESPNOW_MAX_DATA];
    size_t off = 0;
    buf[off++] = (uint8_t)CAM_MSG_GRID_MAP;

    auto write_f = [&](float v) {
        memcpy(&buf[off], &v, sizeof(float)); off += sizeof(float);
    };
    auto write_u16 = [&](uint16_t v) {
        memcpy(&buf[off], &v, sizeof(uint16_t)); off += sizeof(uint16_t);
    };

    write_f(w); write_f(h); write_f(dx); write_f(dy);
    write_u16(nx); write_u16(ny); write_u16(points_count);
    // Extended v2 fields: calibration image dimensions.
    write_u16(img_w); write_u16(img_h);
    // Extended v3 fields: grid insets (pixels).
    write_u16(inset_l); write_u16(inset_t); write_u16(inset_r); write_u16(inset_b);

    // Offsets are raw pixel differences: actual_px - ideal_px, clamped to int8.
    for (uint16_t i = 0; i < points_count; i++) {
        int8_t x = offsets_xy[i*2 + 0];
        int8_t y = offsets_xy[i*2 + 1];
        buf[off++] = (uint8_t)x;
        buf[off++] = (uint8_t)y;
    }

    const uint8_t *dst = peer_mac ? peer_mac : BROADCAST_MAC;
    esp_err_t err = esp_now_send(dst, buf, off);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send grid compact err=%d", err);
        return false;
    }
    return true;
}
