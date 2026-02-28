/**
 * bridge_transport_sdio.cpp  —  SDIO slave transport implementation (C6 side)
 *
 * Provides the bridge_transport_* functions when C6_BRIDGE_TRANSPORT_SDIO is
 * defined.  The ESP32-C6 acts as an SDIO slave; the ESP32-P4 is the SDIO host.
 *
 * Physical wiring (C6 slave GPIO are fixed in silicon):
 *   C6 GPIO18 = SDIO CMD   ←→  P4 SDMMC CMD   pin
 *   C6 GPIO19 = SDIO CLK   ←→  P4 SDMMC CLK   pin
 *   C6 GPIO20 = SDIO DAT0  ←→  P4 SDMMC DAT0  pin
 *   C6 GPIO21 = SDIO DAT1  ←→  P4 SDMMC DAT1  pin  (4-bit only)
 *   C6 GPIO22 = SDIO DAT2  ←→  P4 SDMMC DAT2  pin  (4-bit only)
 *   C6 GPIO23 = SDIO DAT3  ←→  P4 SDMMC DAT3  pin  (4-bit only)
 *   GND ←→ GND  (shared on-board)
 *
 * Protocol:
 *   Each bridge_protocol.h frame is transferred as a single SDIO packet.
 *   The existing CRC8 field in the frame provides an extra integrity check on
 *   top of the SDIO CRC that the hardware generates automatically.
 *
 * Build-time knobs (set via build_flags):
 *   C6_BRIDGE_SDIO_BUS_WIDTH      – 1 or 4 (default: 4)
 *   C6_BRIDGE_SDIO_RX_BUF_COUNT  – Pre-registered host→slave buffers (default 4)
 *   C6_BRIDGE_SDIO_TX_BUF_COUNT  – Pre-allocated slave→host TX pool (default 4)
 */

#ifdef C6_BRIDGE_TRANSPORT_SDIO

#include <Arduino.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/sdio_slave.h"

#include "bridge_transport.h"
#include "bridge_protocol.h"

static const char *TAG = "sdio_transport";

// ---- Build-time configuration -----------------------------------------------

#ifndef C6_BRIDGE_SDIO_BUS_WIDTH
#define C6_BRIDGE_SDIO_BUS_WIDTH      4
#endif

#ifndef C6_BRIDGE_SDIO_RX_BUF_COUNT
#define C6_BRIDGE_SDIO_RX_BUF_COUNT   4
#endif

#ifndef C6_BRIDGE_SDIO_TX_BUF_COUNT
#define C6_BRIDGE_SDIO_TX_BUF_COUNT   4
#endif

/** Each SDIO packet holds exactly one bridge frame (max size 262 bytes). */
#define SDIO_PKT_SIZE  BRIDGE_MAX_FRAME_SIZE

// ---- Receive-buffer pool (host → slave) -------------------------------------
// Buffers must live in DMA-capable memory and stay alive for the lifetime of
// the driver.  We allocate them once in init and recycle them via
// sdio_slave_recv_load_buf() after each received packet is consumed.

static uint8_t              *s_rx_bufs  [C6_BRIDGE_SDIO_RX_BUF_COUNT];
static sdio_slave_buf_handle_t s_rx_handles[C6_BRIDGE_SDIO_RX_BUF_COUNT];

// ---- Transmit-buffer pool (slave → host) ------------------------------------
// Simple fixed-size pool protected by a FreeRTOS semaphore.  acquire/release
// happen inside bridge_transport_write.

typedef struct {
    uint8_t   data[SDIO_PKT_SIZE];
    bool      in_use;
} tx_slot_t;

static tx_slot_t    s_tx_pool[C6_BRIDGE_SDIO_TX_BUF_COUNT];
static SemaphoreHandle_t s_tx_sem = NULL;   // counts available TX slots

// ---- Internal helpers -------------------------------------------------------

static tx_slot_t *tx_pool_acquire(TickType_t ticks_to_wait)
{
    if (xSemaphoreTake(s_tx_sem, ticks_to_wait) != pdTRUE) return NULL;
    for (int i = 0; i < C6_BRIDGE_SDIO_TX_BUF_COUNT; i++) {
        if (!s_tx_pool[i].in_use) {
            s_tx_pool[i].in_use = true;
            return &s_tx_pool[i];
        }
    }
    // Should never reach here if semaphore count is consistent.
    xSemaphoreGive(s_tx_sem);
    return NULL;
}

static void tx_pool_release(tx_slot_t *slot)
{
    slot->in_use = false;
    xSemaphoreGive(s_tx_sem);
}

// ---- Public API -------------------------------------------------------------

void bridge_transport_init(void)
{
    ESP_LOGI(TAG, "Initialising SDIO slave (bus_width=%d, rx_bufs=%d, "
             "tx_bufs=%d, pkt_size=%d)",
             C6_BRIDGE_SDIO_BUS_WIDTH,
             C6_BRIDGE_SDIO_RX_BUF_COUNT,
             C6_BRIDGE_SDIO_TX_BUF_COUNT,
             SDIO_PKT_SIZE);

    // 1. Configure and initialise the SDIO slave peripheral.
    sdio_slave_config_t cfg = {
        .sending_mode       = SDIO_SLAVE_SEND_PACKET,
        .send_queue_size    = C6_BRIDGE_SDIO_TX_BUF_COUNT,
        .recv_buffer_size   = SDIO_PKT_SIZE,
        .event_cb           = NULL,    // no interrupt callback needed
    };
    ESP_ERROR_CHECK(sdio_slave_initialize(&cfg));

    // 2. Allocate and register host→slave DMA receive buffers.
    // sdio_slave_recv_register_buf() returns the handle directly (not via an
    // output pointer) in ESP-IDF 5.x / arduino-esp32 >= 3.x.
    for (int i = 0; i < C6_BRIDGE_SDIO_RX_BUF_COUNT; i++) {
        s_rx_bufs[i] = (uint8_t *)heap_caps_malloc(SDIO_PKT_SIZE,
                                                    MALLOC_CAP_DMA);
        if (!s_rx_bufs[i]) {
            ESP_LOGE(TAG, "OOM: failed to allocate RX buffer %d", i);
            return;
        }
        s_rx_handles[i] = sdio_slave_recv_register_buf(s_rx_bufs[i]);
        if (!s_rx_handles[i]) {
            ESP_LOGE(TAG, "sdio_slave_recv_register_buf failed for buf %d", i);
            return;
        }
        ESP_ERROR_CHECK(sdio_slave_recv_load_buf(s_rx_handles[i]));
    }

    // 3. Initialise the TX pool semaphore.
    s_tx_sem = xSemaphoreCreateCounting(C6_BRIDGE_SDIO_TX_BUF_COUNT,
                                         C6_BRIDGE_SDIO_TX_BUF_COUNT);
    configASSERT(s_tx_sem);

    // 4. Start the slave (begins responding to host enumeration/SDIO traffic).
    ESP_ERROR_CHECK(sdio_slave_start());

    ESP_LOGI(TAG, "SDIO slave started, waiting for host to enumerate...");
}

int bridge_transport_write(const uint8_t *buf, size_t len)
{
    if (len == 0 || len > SDIO_PKT_SIZE) {
        ESP_LOGE(TAG, "write: invalid len %zu", len);
        return -1;
    }

    // Acquire a DMA-accessible TX slot from the pool.
    // Wait up to 100 ms before giving up (host stall is unusual).
    tx_slot_t *slot = tx_pool_acquire(pdMS_TO_TICKS(100));
    if (!slot) {
        ESP_LOGW(TAG, "write: TX pool empty, dropping %zu bytes", len);
        return -1;
    }
    memcpy(slot->data, buf, len);

    // Queue the buffer for the host to read.  The 'arg' (last parameter) is
    // the slot pointer so we can release it in sdio_slave_send_get_finished().
    esp_err_t err = sdio_slave_send_queue(slot->data, len,
                                          (void *)slot, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write: send_queue err %d", err);
        tx_pool_release(slot);
        return -1;
    }

    // Wait for the host DMA transfer to complete and reclaim the slot.
    void *finished_arg = NULL;
    err = sdio_slave_send_get_finished(&finished_arg,
                                       pdMS_TO_TICKS(500));
    if (err == ESP_OK && finished_arg) {
        tx_pool_release((tx_slot_t *)finished_arg);
    } else {
        // Timeout or error — the slot is leaked but the system stays alive.
        ESP_LOGW(TAG, "write: send_get_finished err %d", err);
        return -1;
    }

    return (int)len;
}

int bridge_transport_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    sdio_slave_buf_handle_t handle = NULL;
    uint8_t  *recv_data = NULL;
    size_t    recv_len  = 0;

    esp_err_t err = sdio_slave_recv(&handle, &recv_data, &recv_len,
                                    pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_ERR_TIMEOUT) return 0;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "recv err %d", err);
        return -1;
    }

    size_t copy_len = (recv_len < max_len) ? recv_len : max_len;
    memcpy(buf, recv_data, copy_len);

    // Recycle the buffer immediately so the host sees it as available again.
    sdio_slave_recv_load_buf(handle);

    return (int)copy_len;
}

size_t bridge_transport_describe(char *buf, size_t buf_size)
{
    return (size_t)snprintf(buf, buf_size,
        "Transport: SDIO slave, bus_width=%d, C6 CLK=GPIO19 CMD=GPIO18 "
        "D0=GPIO20 D1=GPIO21 D2=GPIO22 D3=GPIO23",
        C6_BRIDGE_SDIO_BUS_WIDTH);
}

#endif  // C6_BRIDGE_TRANSPORT_SDIO
