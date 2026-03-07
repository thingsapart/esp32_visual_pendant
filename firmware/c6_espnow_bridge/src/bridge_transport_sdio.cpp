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

// ---- Host-ready gate --------------------------------------------------------
// The P4 host resets the SDIO slave's TX_BUFFER_NUM register during essl_init.
// Any frames queued in sdio_slave_send_queue() *before* that reset become
// permanently invisible to essl_get_packet() — the host sees zero buffers
// available and never issues a DMA read, so the slot semaphore stalls at 0.
// We defer all TX until we have received at least one frame from the P4
// (confirming essl_init is complete and the ESSL layer is operational).
static volatile bool s_host_ready = false;

void bridge_transport_set_host_ready(void)
{
    if (!s_host_ready) {
        s_host_ready = true;
        ESP_LOGI(TAG, "P4 ESSL confirmed ready — C6\xe2\x86\x92P4 TX forwarding enabled");
    }
}

bool bridge_transport_is_host_ready(void) { return s_host_ready; }

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
    // Don't touch the SDIO send queue until the host has completed essl_init.
    // See comment on s_host_ready / bridge_transport_set_host_ready().
    if (!s_host_ready) return -1;

    if (len == 0 || len > SDIO_PKT_SIZE) {
        ESP_LOGE(TAG, "write: invalid len %zu", len);
        return -1;
    }

    // Non-blocking: drain any TX slots that have already been read by the host
    // before we try to acquire a new one.  This keeps the pool healthy without
    // blocking on the host.
    {
        void *finished_arg = NULL;
        while (sdio_slave_send_get_finished(&finished_arg, 0) == ESP_OK) {
            if (finished_arg) tx_pool_release((tx_slot_t *)finished_arg);
            finished_arg = NULL;
        }
    }

    // Acquire a DMA-accessible TX slot from the pool.
    // Use a short timeout: the C6 loop() is single-threaded; long waits here
    // block bridge_transport_read() and cause P4→C6 command loss.
    tx_slot_t *slot = tx_pool_acquire(pdMS_TO_TICKS(20));
    if (!slot) {
        // One more attempt: do a short blocking reclaim of completed sends.
        void *finished_arg = NULL;
        if (sdio_slave_send_get_finished(&finished_arg, pdMS_TO_TICKS(20)) == ESP_OK && finished_arg) {
            tx_pool_release((tx_slot_t *)finished_arg);
            slot = tx_pool_acquire(0);
        }
        if (!slot) {
            // Last resort: force-reclaim slots that have been in-flight for
            // more than 3 s.  This handles P4 crash/reboot: after essl_init
            // the host resets TX_BUFFER_NUM so those queued items can never
            // be acknowledged.  Reclaiming them also clears s_host_ready so
            // the gate re-opens cleanly when the P4 sends its next ping.
            int reclaimed = tx_pool_reclaim_old_slots(pdMS_TO_TICKS(3000),
                                                      C6_BRIDGE_SDIO_TX_BUF_COUNT);
            if (reclaimed > 0) {
                // Slots have been force-reclaimed due to prolonged P4 absence.
                // Do NOT restart — that would lose all queued hub state.
                // Instead, clear the host-ready gate; the P4 heartbeat task
                // sends a '?' ping every 5 s which will re-open it.
                ESP_LOGW(TAG, "write: force-reclaimed %d stale TX slot(s) — "
                              "clearing host-ready, awaiting P4 heartbeat",
                         reclaimed);
                s_host_ready = false;
                return -1;
            }
        }
        if (!slot) {
            ESP_LOGW(TAG, "write: TX pool empty, dropping %zu bytes", len);
            return -1;
        }
    }
    memcpy(slot->data, buf, len);

    // Short timeout: the loop() calling this is single-threaded.  A long
    // wait here starves bridge_transport_read() and silently drops P4 TX.
    esp_err_t err = sdio_slave_send_queue(slot->data, len,
                                          (void *)slot, pdMS_TO_TICKS(20));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write: send_queue err %d (%s)", err, esp_err_to_name(err));
        tx_pool_release(slot);
        return -1;
    }

    // Do NOT wait here for send_get_finished — the host reads it asynchronously.
    // We will reclaim the slot at the top of the next bridge_transport_write()
    // call (the non-blocking drain above), keeping total blocking time short.
    // The slot and its memory remain valid until reclaimed because we hold it
    // in the in_use pool.
    BRIDGE_VLOG("write: queued %zu bytes (slot %td)", len, slot - s_tx_pool);
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
        ESP_LOGE(TAG, "recv err %d (%s)", err, esp_err_to_name(err));
        return -1;
    }

    size_t copy_len = (recv_len < max_len) ? recv_len : max_len;
    memcpy(buf, recv_data, copy_len);

    // Recycle the buffer immediately so the host sees it as available again.
    sdio_slave_recv_load_buf(handle);

    BRIDGE_VLOG("read: %zu bytes from host", copy_len);
    return (int)copy_len;
}

size_t bridge_transport_describe(char *buf, size_t buf_size)
{
    return (size_t)snprintf(buf, buf_size,
        "SDIO slave: CLK 19 CMD 18 D0-3 20-23",
        C6_BRIDGE_SDIO_BUS_WIDTH);
}

// UART transport stubs — compiled when SDIO is NOT selected, providing the
// same host_ready symbols so the linker is satisfied regardless of transport.
// (The #ifdef guard prevents double-definition when both files are compiled
//  with different transport defines — normally only one .cpp is active.)
// Actually these stubs are NOT needed here — they live in bridge_transport_uart.cpp
// or are provided by the inline fallbacks in bridge_transport.h.  No stubs here.

#endif  // C6_BRIDGE_TRANSPORT_SDIO
