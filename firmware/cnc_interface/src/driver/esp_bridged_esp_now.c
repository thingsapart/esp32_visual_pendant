/**
 * esp_bridged_esp_now.c  —  Drop-in ESP-NOW API shim via SDIO bridge — v2
 *
 * Provides standard esp_now_*() function signatures that internally route
 * through the C6 SDIO bridge transport (c6_bridge_sdio.c).
 *
 * Architecture:
 *   - bridged_sdio_rx_task: polls c6_sdio_bridge_read() which reads from the
 *     internal RX queue (populated by the bus task in c6_bridge_sdio.c).
 *     No ESSL access, no mutex contention.
 *   - esp_now_send() etc: call c6_sdio_bridge_write() which pushes into a
 *     TX queue — non-blocking, never stalls.
 *   - bridged_heartbeat_task: sends '?' every 5 s to keep the C6 watchdog
 *     happy and the host-ready gate open.
 *
 *   Deadlock-free by design: no mutexes, no shared ESSL access.
 *   Tasks cannot starve each other because neither blocks on bus operations.
 */

#ifdef REMOTE_COMMS_C6_SDIO_BRIDGE

#define UI_LOCAL_DEBUG_LEVEL D_VERBOSE
#include "debug.h"

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bridge_protocol.h"
#include "c6_bridge_sdio.h"
#include <string.h>

#ifndef TASK_MACHINE_CORE
#  define TASK_MACHINE_CORE 1
#endif

static const char *TAG = "bridged_espnow";

static esp_now_recv_cb_t s_recv_cb = NULL;
static esp_now_send_cb_t s_send_cb = NULL;

/* ═══════════════════════════════════════════════════════════════════════════
 *  RX task — reads frames from the SDIO RX queue, dispatches callbacks
 *
 *  c6_sdio_bridge_read() blocks on the FreeRTOS RX queue with a short
 *  timeout — it NEVER touches the SDIO bus directly.  This means the RX
 *  task cannot block or starve the TX path.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void bridged_sdio_rx_task(void *pvParameter)
{
    (void)pvParameter;
    uint8_t rx_buf[BRIDGE_MAX_FRAME_SIZE];
    uint32_t rx_pkt   = 0;
    uint32_t rx_err   = 0;
    uint32_t rx_miss  = 0;
    TickType_t last_status = xTaskGetTickCount();

    LOGI(TAG, "[rx] task started on core %d  recv_cb=%p send_cb=%p",
             xPortGetCoreID(), (void *)s_recv_cb, (void *)s_send_cb);

    while (true) {
        /* Periodic status ticker (every 5 s). */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_status) >= pdMS_TO_TICKS(5000)) {
            LOGI(TAG, "[rx] pkt=%lu err=%lu miss=%lu  recv_cb=%p",
                     (unsigned long)rx_pkt, (unsigned long)rx_err,
                     (unsigned long)rx_miss, (void *)s_recv_cb);
            last_status = now;
        }

        /* Read with 20 ms timeout — short enough to keep the status ticker
         * responsive, long enough to avoid wasteful busy-loop CPU burn.
         * This does NOT hold any mutex or touch the SDIO bus. */
        size_t len = 0;
        if (!c6_sdio_bridge_read(rx_buf, sizeof(rx_buf), &len, 20)) {
            rx_miss++;
            continue;   /* timeout or error — loop back */
        }

        /* Validate frame header. */
        if (len < BRIDGE_FRAME_OVERHEAD ||
            rx_buf[0] != BRIDGE_SOF0 || rx_buf[1] != BRIDGE_SOF1) {
            rx_err++;
            LOGW(TAG, "[rx] bad frame: len=%u sof=0x%02x%02x",
                     (unsigned)len, rx_buf[0], len > 1 ? rx_buf[1] : 0);
            continue;
        }

        rx_pkt++;
        uint8_t   dir         = rx_buf[2];
        uint8_t  *mac         = &rx_buf[3];
        uint16_t  payload_len = (uint16_t)(rx_buf[9] | (rx_buf[10] << 8));
        uint8_t  *payload     = &rx_buf[11];

        /* Log first 20 received frames for diagnostics. */
        if (rx_pkt <= 20) {
            LOGI(TAG, "[rx] frame #%lu: dir=0x%02x plen=%u "
                     "mac=%02x:%02x:%02x:%02x:%02x:%02x",
                     (unsigned long)rx_pkt, dir, (unsigned)payload_len,
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }

        if (dir == BRIDGE_DIR_INCOMING && s_recv_cb) {
            esp_now_recv_info_t info;
            uint8_t src_addr[ESP_NOW_ETH_ALEN];
            uint8_t des_addr[ESP_NOW_ETH_ALEN];
            memset(&info, 0, sizeof(info));
            memcpy(src_addr, mac, ESP_NOW_ETH_ALEN);
            memset(des_addr, 0xFF, ESP_NOW_ETH_ALEN);
            info.src_addr = src_addr;
            info.des_addr = des_addr;
            info.rx_ctrl  = NULL;
            s_recv_cb(&info, payload, payload_len);
        } else if (dir == BRIDGE_DIR_INCOMING && !s_recv_cb) {
            LOGW(TAG, "[rx] INCOMING frame but recv_cb is NULL!");
        } else if (dir == BRIDGE_DIR_CONTROL) {
            if (payload_len >= 8 && payload[0] == BRIDGE_CTRL_TX_STATUS
                && s_send_cb) {
                esp_now_send_status_t status =
                    payload[7] ? ESP_NOW_SEND_SUCCESS : ESP_NOW_SEND_FAIL;
                s_send_cb(mac, status);
            }
        }

        /* Yield to let equal/lower priority tasks run, even under flood. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Heartbeat task — keeps C6 watchdog happy
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint32_t s_hb_count = 0;

static void bridged_heartbeat_task(void *pvParameter)
{
    (void)pvParameter;
    while (true) {
        uint8_t sync = BRIDGE_DEBUG_TRIGGER;
        bool ok = c6_sdio_bridge_write(&sync, 1);
        s_hb_count++;
        /* Log every heartbeat so we can verify they're reaching the bus. */
        LOGI(TAG, "[hb] #%lu write=%s",
                 (unsigned long)s_hb_count, ok ? "OK" : "FAIL");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  esp_now_*() drop-in API
 * ═══════════════════════════════════════════════════════════════════════════ */

esp_err_t esp_now_init(void)
{
    if (!c6_sdio_bridge_init()) {
        LOGE(TAG, "SDIO bridge init failed");
        return ESP_FAIL;
    }

    /* Send initial handshake ping. */
    uint8_t sync = BRIDGE_DEBUG_TRIGGER;
    c6_sdio_bridge_write(&sync, 1);

    /* RX task at prio +2, heartbeat at +1.
     * Neither task touches the SDIO bus directly — they only interact with
     * the c6_bridge_sdio queues, so there is no priority inversion risk. */
    xTaskCreatePinnedToCore(bridged_sdio_rx_task, "bridged_rx", 4096, NULL,
                            tskIDLE_PRIORITY + 2, NULL, TASK_MACHINE_CORE);
    xTaskCreatePinnedToCore(bridged_heartbeat_task, "bridged_hb", 2048, NULL,
                            tskIDLE_PRIORITY + 1, NULL, TASK_MACHINE_CORE);

    return ESP_OK;
}

esp_err_t esp_now_deinit(void)
{
    return ESP_OK;
}

esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len)
{
    if (len > BRIDGE_MAX_PAYLOAD) return ESP_ERR_ESPNOW_ARG;

    uint8_t frame[BRIDGE_MAX_FRAME_SIZE];
    frame[0] = BRIDGE_SOF0;
    frame[1] = BRIDGE_SOF1;
    frame[2] = BRIDGE_DIR_OUTGOING;
    memcpy(&frame[3], peer_addr, 6);
    frame[9]  = len & 0xFF;
    frame[10] = (len >> 8) & 0xFF;
    memcpy(&frame[11], data, len);
    frame[11 + len] = bridge_crc8(peer_addr, len, data);

    bool ok = c6_sdio_bridge_write(frame, 12 + len);
    LOGI(TAG, "[tx] esp_now_send %u B to %02x:%02x:%02x:%02x:%02x:%02x => %s",
             len, peer_addr[0], peer_addr[1], peer_addr[2],
             peer_addr[3], peer_addr[4], peer_addr[5],
             ok ? "queued" : "FAIL");
    return ok ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer)
{
    uint8_t frame[BRIDGE_MAX_FRAME_SIZE];
    frame[0] = BRIDGE_SOF0;
    frame[1] = BRIDGE_SOF1;
    frame[2] = BRIDGE_DIR_CONTROL;
    memcpy(&frame[3], peer->peer_addr, 6);

    uint16_t len = 7;
    frame[9]  = len & 0xFF;
    frame[10] = (len >> 8) & 0xFF;

    frame[11] = BRIDGE_CTRL_ADD_PEER;
    memcpy(&frame[12], peer->peer_addr, 6);

    frame[11 + len] = bridge_crc8(peer->peer_addr, len, &frame[11]);

    return c6_sdio_bridge_write(frame, 12 + len) ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_now_del_peer(const uint8_t *peer_addr)
{
    uint8_t frame[BRIDGE_MAX_FRAME_SIZE];
    frame[0] = BRIDGE_SOF0;
    frame[1] = BRIDGE_SOF1;
    frame[2] = BRIDGE_DIR_CONTROL;
    memcpy(&frame[3], peer_addr, 6);

    uint16_t len = 7;
    frame[9]  = len & 0xFF;
    frame[10] = (len >> 8) & 0xFF;

    frame[11] = BRIDGE_CTRL_DEL_PEER;
    memcpy(&frame[12], peer_addr, 6);

    frame[11 + len] = bridge_crc8(peer_addr, len, &frame[11]);

    return c6_sdio_bridge_write(frame, 12 + len) ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_now_modify_peer(const esp_now_peer_info_t *peer)
{
    return ESP_OK;
}

esp_err_t esp_now_get_peer(const uint8_t *peer_addr, esp_now_peer_info_t *peer)
{
    return ESP_ERR_ESPNOW_NOT_FOUND;
}

bool esp_now_is_peer_exist(const uint8_t *peer_addr)
{
    return true;   /* offloaded to C6 */
}

esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb)
{
    s_recv_cb = cb;
    return ESP_OK;
}

esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb)
{
    s_send_cb = cb;
    return ESP_OK;
}

#endif