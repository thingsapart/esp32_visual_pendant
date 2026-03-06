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
#include "esp_system.h"
#include "driver/sdio_slave.h"

#include "bridge_transport.h"
#include "bridge_protocol.h"

static const char *TAG = "sdio_transport";

// Verbose logging macro: enabled only when C6_BRIDGE_LOG_EXT is defined.
#ifdef C6_BRIDGE_LOG_EXT
#define BRIDGE_VLOG(fmt, ...) ESP_LOGV(TAG, fmt, ##__VA_ARGS__)
#else
#define BRIDGE_VLOG(fmt, ...) do { (void)0; } while (0)
#endif

// ---- Build-time configuration -----------------------------------------------

#ifndef C6_BRIDGE_SDIO_BUS_WIDTH
#define C6_BRIDGE_SDIO_BUS_WIDTH      4
#endif

#ifndef C6_BRIDGE_SDIO_RX_BUF_COUNT
#define C6_BRIDGE_SDIO_RX_BUF_COUNT   4
#endif

#ifndef C6_BRIDGE_SDIO_TX_BUF_COUNT
#define C6_BRIDGE_SDIO_TX_BUF_COUNT   8
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
    TickType_t ts;
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
            s_tx_pool[i].ts = xTaskGetTickCount();
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
    slot->ts = 0;
    xSemaphoreGive(s_tx_sem);
}

// Reclaim any slots that appear to have been stuck in-use for longer than
// 'age_ticks'. Returns number of reclaimed slots (and gives the semaphore
// for each reclaimed slot). This is a best-effort rescue path for cases
// where the host DMA completion was missed and a slot was leaked.
static int tx_pool_reclaim_old_slots(TickType_t age_ticks, int max_reclaim)
{
    int reclaimed = 0;
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < C6_BRIDGE_SDIO_TX_BUF_COUNT && reclaimed < max_reclaim; i++) {
        if (s_tx_pool[i].in_use && s_tx_pool[i].ts != 0) {
            TickType_t age = now - s_tx_pool[i].ts;
            if (age >= age_ticks) {
                BRIDGE_VLOG("Reclaiming stuck TX slot %d (age=%u ticks)", i, (unsigned)age);
                s_tx_pool[i].in_use = false;
                s_tx_pool[i].ts = 0;
                xSemaphoreGive(s_tx_sem);
                reclaimed++;
            }
        }
    }
    return reclaimed;
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
    // NOTE: calling sdio_slave_deinit() here on some ESP32-C6 toolchain
    // versions can dereference uninitialised internals and crash during
    // early boot (observed in practice). Skip the unconditional deinit
    // and initialise directly; if a previous run left the peripheral in a
    // bad state a reset will be performed by the watchdog/restart logic.

    sdio_slave_config_t cfg = {
        .sending_mode       = SDIO_SLAVE_SEND_PACKET,
        .send_queue_size    = C6_BRIDGE_SDIO_TX_BUF_COUNT,
        .recv_buffer_size   = SDIO_PKT_SIZE,
        .event_cb           = NULL,    // no interrupt callback needed
    };
    esp_err_t err = sdio_slave_initialize(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdio_slave_initialize failed: %d — will retry in 3 s", err);
        // Delay before restart so the log is visible and we don't spin-reboot.
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        return;
    }

    // 2. Allocate and register host→slave DMA receive buffers.
    // sdio_slave_recv_register_buf() returns the handle directly (not via an
    // output pointer) in ESP-IDF 5.x / arduino-esp32 >= 3.x.
    for (int i = 0; i < C6_BRIDGE_SDIO_RX_BUF_COUNT; i++) {
        s_rx_bufs[i] = (uint8_t *)heap_caps_malloc(SDIO_PKT_SIZE,
                                                    MALLOC_CAP_DMA);
        if (!s_rx_bufs[i]) {
            ESP_LOGE(TAG, "OOM: failed to allocate RX buffer %d", i);
            sdio_slave_deinit();
            return;
        }
        s_rx_handles[i] = sdio_slave_recv_register_buf(s_rx_bufs[i]);
        if (!s_rx_handles[i]) {
            ESP_LOGE(TAG, "sdio_slave_recv_register_buf failed for buf %d", i);
            sdio_slave_deinit();
            return;
        }
        esp_err_t lerr = sdio_slave_recv_load_buf(s_rx_handles[i]);
        if (lerr != ESP_OK) {
            ESP_LOGE(TAG, "sdio_slave_recv_load_buf[%d] failed: %d", i, lerr);
            sdio_slave_deinit();
            return;
        }
    }

    // 3. Initialise the TX pool semaphore.
    s_tx_sem = xSemaphoreCreateCounting(C6_BRIDGE_SDIO_TX_BUF_COUNT,
                                         C6_BRIDGE_SDIO_TX_BUF_COUNT);
    configASSERT(s_tx_sem);

    // 4. Start the slave (begins responding to host enumeration/SDIO traffic).
    err = sdio_slave_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdio_slave_start failed: %d — will retry in 3 s", err);
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        return;
    }

    ESP_LOGI(TAG, "SDIO slave started, waiting for host to enumerate...");
}

int bridge_transport_write(const uint8_t *buf, size_t len)
{
    if (len == 0 || len > SDIO_PKT_SIZE) {
        ESP_LOGE(TAG, "write: invalid len %zu", len);
        return -1;
    }

    // Acquire a DMA-accessible TX slot from the pool.
    // Wait a bit longer before giving up — host stalls have been observed.
    tx_slot_t *slot = tx_pool_acquire(pdMS_TO_TICKS(500));
    if (!slot) {
        ESP_LOGW(TAG, "write: TX pool empty, dropping %zu bytes", len);
        return -1;
    }
    memcpy(slot->data, buf, len);

    // Queue the buffer for the host to read.  The 'arg' (last parameter) is
    // the slot pointer so we can release it in sdio_slave_send_get_finished().
    // Use a bounded timeout (200 ms) instead of portMAX_DELAY so a stalled
    // host cannot deadlock the main task and trigger the task watchdog.
    esp_err_t err = sdio_slave_send_queue(slot->data, len,
                                          (void *)slot, pdMS_TO_TICKS(2000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write: send_queue err %d (%s)", err, esp_err_to_name(err));
        // Diagnostic: count in-use slots
        int in_use = 0;
        for (int i = 0; i < C6_BRIDGE_SDIO_TX_BUF_COUNT; i++) if (s_tx_pool[i].in_use) in_use++;
        UBaseType_t sem_count = uxSemaphoreGetCount(s_tx_sem);
        BRIDGE_VLOG("write: send_queue failed, in_use=%d sem_count=%u", in_use, (unsigned)sem_count);
        // Attempt conservative reclaim of one old slot to recover from leaks.
        int reclaimed = tx_pool_reclaim_old_slots(pdMS_TO_TICKS(3000), 1);
        if (reclaimed) BRIDGE_VLOG("write: reclaimed %d old slot(s)", reclaimed);
        tx_pool_release(slot);
        return -1;
    }

    // Wait for the host DMA transfer to complete and reclaim the slot.
    void *finished_arg = NULL;
    err = sdio_slave_send_get_finished(&finished_arg,
                                       pdMS_TO_TICKS(2000));
    if (err == ESP_OK && finished_arg) {
        tx_pool_release((tx_slot_t *)finished_arg);
    } else {
        // Timeout or error — the slot is likely leaked. Log diagnostics and
        // attempt to reclaim stuck slots older than a threshold.
        int in_use = 0;
        for (int i = 0; i < C6_BRIDGE_SDIO_TX_BUF_COUNT; i++) if (s_tx_pool[i].in_use) in_use++;
        UBaseType_t sem_count = uxSemaphoreGetCount(s_tx_sem);
        ESP_LOGW(TAG, "write: send_get_finished err %d (%s)", err, esp_err_to_name(err));
        BRIDGE_VLOG("write: detailed state: in_use=%d sem_count=%u",
                    in_use, (unsigned)sem_count);

        int reclaimed = tx_pool_reclaim_old_slots(pdMS_TO_TICKS(3000), C6_BRIDGE_SDIO_TX_BUF_COUNT);
        if (reclaimed) {
            BRIDGE_VLOG("write: reclaimed %d stuck TX slot(s)", reclaimed);
        } else {
            BRIDGE_VLOG("write: no stuck slots eligible for reclaim");
        }
        return -1;
    }

    // Successful completion of the host DMA transfer.
    BRIDGE_VLOG("write: completed %zu bytes", len);
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
        "SDIO slave: CLK 19 CMD 18 D0-3 20-23",
        C6_BRIDGE_SDIO_BUS_WIDTH);
}

#endif  // C6_BRIDGE_TRANSPORT_SDIO
