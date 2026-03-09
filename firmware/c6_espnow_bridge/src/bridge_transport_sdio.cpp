/**
 * bridge_transport_sdio.cpp  —  SDIO slave transport (C6 side) — v2 clean-room
 *
 * Design goals:
 *   1. ZERO deadlocks — TX and RX use separate SDIO DMA channels; no shared
 *      mutex is needed because sdio_slave_recv() and sdio_slave_send_queue()
 *      operate on independent hardware queues.
 *   2. Dedicated TX task drains a FreeRTOS queue — ESP-NOW callbacks (ISR ctx)
 *      push into the queue via the main loop; the TX task dequeues and calls
 *      sdio_slave_send_queue().  No blocking in the main loop for TX.
 *   3. bridge_transport_read() is called from the main loop() context and
 *      calls sdio_slave_recv() — this is the only call site for recv.
 *   4. Host-ready gate: TX is suppressed until the P4 completes essl_init()
 *      and sends its first packet (the '?' ping).
 *   5. Overflow handling: when the TX queue is full, the OLDEST entry is
 *      evicted and a warning is printed.  Data is never silently lost.
 *   6. Large buffers: 32 TX queue slots, 8 RX DMA buffers, 8 TX DMA slots.
 *
 * Physical wiring (C6 SDIO slave GPIO are fixed in silicon):
 *   C6 GPIO18 = CMD, GPIO19 = CLK, GPIO20-23 = D0-D3
 *
 * Compile guard: only builds when -D C6_BRIDGE_TRANSPORT_SDIO is set.
 */

#ifdef C6_BRIDGE_TRANSPORT_SDIO

#define UI_LOCAL_DEBUG_LEVEL D_WARN
#include "debug.h"

#include <Arduino.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "driver/sdio_slave.h"

#include "bridge_transport.h"
#include "bridge_protocol.h"

static const char *TAG = "sdio_xport";

/* ═══════════════════════════════════════════════════════════════════════════
 *  Configuration (build-time overridable)
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifndef C6_BRIDGE_SDIO_BUS_WIDTH
#  define C6_BRIDGE_SDIO_BUS_WIDTH   4
#endif

/* RX: host→slave DMA receive buffers.  More buffers = wider pipeline for
 * bursty P4 traffic.  Each is BRIDGE_MAX_FRAME_SIZE bytes of DMA RAM. */
#ifndef C6_BRIDGE_SDIO_RX_BUF_COUNT
#  define C6_BRIDGE_SDIO_RX_BUF_COUNT  8
#endif

/* TX FreeRTOS queue depth (software queue fed by bridge_transport_write). */
#ifndef C6_BRIDGE_SDIO_TX_QUEUE_DEPTH
#  define C6_BRIDGE_SDIO_TX_QUEUE_DEPTH  32
#endif

/* TX DMA slot pool: buffers handed to sdio_slave_send_queue.  Must be ≥
 * send_queue_size in the sdio_slave_config_t. */
#ifndef C6_BRIDGE_SDIO_TX_DMA_SLOTS
#  define C6_BRIDGE_SDIO_TX_DMA_SLOTS    8
#endif

/* TX task configuration. */
#ifndef C6_BRIDGE_SDIO_TX_TASK_STACK
#  define C6_BRIDGE_SDIO_TX_TASK_STACK  4096
#endif
#ifndef C6_BRIDGE_SDIO_TX_TASK_PRIO
#  define C6_BRIDGE_SDIO_TX_TASK_PRIO  2
#endif

#define SDIO_PKT_SIZE  BRIDGE_MAX_FRAME_SIZE   /* 262 */

/* How long before an in-flight DMA slot is considered stale. */
#define TX_SLOT_STALE_MS  3000

/* ═══════════════════════════════════════════════════════════════════════════
 *  Host-ready gate
 * ═══════════════════════════════════════════════════════════════════════════ */

static volatile bool s_host_ready = false;

void bridge_transport_set_host_ready(void)
{
    if (!s_host_ready) {
        s_host_ready = true;
        LOGI(TAG, "Host confirmed ready — C6→P4 TX enabled");
    }
}

bool bridge_transport_is_host_ready(void) { return s_host_ready; }

/* ═══════════════════════════════════════════════════════════════════════════
 *  RX buffer pool (host → slave)
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint8_t                 *s_rx_bufs[C6_BRIDGE_SDIO_RX_BUF_COUNT];
static sdio_slave_buf_handle_t  s_rx_handles[C6_BRIDGE_SDIO_RX_BUF_COUNT];

/* ═══════════════════════════════════════════════════════════════════════════
 *  TX queue item — frame copies queued by bridge_transport_write
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint16_t len;
    uint8_t  data[SDIO_PKT_SIZE];
} tx_queue_item_t;

static QueueHandle_t s_tx_queue = NULL;

/* ═══════════════════════════════════════════════════════════════════════════
 *  TX DMA slot pool — fixed buffers for sdio_slave_send_queue
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t    data[SDIO_PKT_SIZE];
    bool       in_use;
    TickType_t queued_at;
} tx_dma_slot_t;

static tx_dma_slot_t     s_tx_dma_pool[C6_BRIDGE_SDIO_TX_DMA_SLOTS];
static SemaphoreHandle_t s_tx_dma_sem = NULL;

static tx_dma_slot_t *tx_dma_acquire(TickType_t wait)
{
    if (xSemaphoreTake(s_tx_dma_sem, wait) != pdTRUE)
        return NULL;
    for (int i = 0; i < C6_BRIDGE_SDIO_TX_DMA_SLOTS; i++) {
        if (!s_tx_dma_pool[i].in_use) {
            s_tx_dma_pool[i].in_use    = true;
            s_tx_dma_pool[i].queued_at = xTaskGetTickCount();
            return &s_tx_dma_pool[i];
        }
    }
    xSemaphoreGive(s_tx_dma_sem);
    return NULL;
}

static void tx_dma_release(tx_dma_slot_t *slot)
{
    slot->in_use    = false;
    slot->queued_at = 0;
    xSemaphoreGive(s_tx_dma_sem);
}

static int tx_dma_reclaim_stale(TickType_t age_ticks)
{
    int reclaimed = 0;
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < C6_BRIDGE_SDIO_TX_DMA_SLOTS; i++) {
        if (s_tx_dma_pool[i].in_use && s_tx_dma_pool[i].queued_at != 0) {
            if ((now - s_tx_dma_pool[i].queued_at) >= age_ticks) {
                LOGW(TAG, "DMA slot %d stale (%u ms) — reclaiming",
                         i, (unsigned)((now - s_tx_dma_pool[i].queued_at)
                                       * portTICK_PERIOD_MS));
                tx_dma_release(&s_tx_dma_pool[i]);
                reclaimed++;
            }
        }
    }
    return reclaimed;
}

static void tx_dma_drain_finished(void)
{
    void *arg = NULL;
    while (sdio_slave_send_get_finished(&arg, 0) == ESP_OK) {
        if (arg) tx_dma_release((tx_dma_slot_t *)arg);
        arg = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TX Task — sole writer to sdio_slave_send_queue
 *
 *  Runs as a dedicated FreeRTOS task.  Blocks on s_tx_queue for items.
 *  Only this task touches sdio_slave_send_queue / send_get_finished.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void sdio_tx_task(void *arg)
{
    (void)arg;
    LOGI(TAG, "TX task started");
    tx_queue_item_t item;

    while (true) {
        if (xQueueReceive(s_tx_queue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
            /* Idle — periodic maintenance. */
            tx_dma_drain_finished();
            tx_dma_reclaim_stale(pdMS_TO_TICKS(TX_SLOT_STALE_MS));
            continue;
        }

        if (!s_host_ready) {
            LOGD(TAG, "TX: host not ready, discarding %u B", item.len);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        tx_dma_drain_finished();

        tx_dma_slot_t *slot = tx_dma_acquire(pdMS_TO_TICKS(20));
        if (!slot) {
            int reclaimed = tx_dma_reclaim_stale(pdMS_TO_TICKS(TX_SLOT_STALE_MS));
            if (reclaimed > 0) {
                LOGW(TAG, "TX: reclaimed %d stale DMA slot(s) — "
                              "clearing host-ready gate; dropping %u B",
                     reclaimed, item.len);
                s_host_ready = false;

                /* The hw descriptor queue is still full from the SDIO
                 * peripheral's perspective even though we just released the
                 * software slots.  Calling sdio_slave_send_queue now would
                 * return ESP_FAIL and lock the TX path permanently.  Drop
                 * this packet; the host-ready gate will suppress further TX
                 * until the P4 sends a fresh ping that re-enables it. */
                vTaskDelay(1);
                continue;
            }
            slot = tx_dma_acquire(0);
            if (!slot) {
                LOGW(TAG, "TX: DMA slots busy, dropping %u B", item.len);
                continue;
            }
        }

        memcpy(slot->data, item.data, item.len);

        esp_err_t err = sdio_slave_send_queue(slot->data, item.len,
                                              (void *)slot,
                                              pdMS_TO_TICKS(50));
        if (err != ESP_OK) {
            LOGW(TAG, "TX: send_queue: %s", esp_err_to_name(err));
            tx_dma_release(slot);
        }

        /* ALWAYS yield after processing an item.  Without this, a full
         * queue causes this prio-2 task to spin without ever yielding,
         * starving the Arduino loop() and other equal/lower-prio tasks. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

void bridge_transport_init(void)
{
    LOGI(TAG, "Init SDIO slave (bus_width=%d rx_bufs=%d tx_q=%d "
             "tx_dma=%d pkt=%d)",
             C6_BRIDGE_SDIO_BUS_WIDTH, C6_BRIDGE_SDIO_RX_BUF_COUNT,
             C6_BRIDGE_SDIO_TX_QUEUE_DEPTH, C6_BRIDGE_SDIO_TX_DMA_SLOTS,
             SDIO_PKT_SIZE);

    /* 1. Configure SDIO slave peripheral. */
    sdio_slave_config_t cfg = {
        .sending_mode     = SDIO_SLAVE_SEND_PACKET,
        .send_queue_size  = C6_BRIDGE_SDIO_TX_DMA_SLOTS,
        .recv_buffer_size = SDIO_PKT_SIZE,
        .event_cb         = NULL,
    };

    esp_err_t err = sdio_slave_initialize(&cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "sdio_slave_initialize: %s — restarting in 3 s",
                 esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        return;
    }

    /* 2. Allocate and register host→slave DMA receive buffers. */
    for (int i = 0; i < C6_BRIDGE_SDIO_RX_BUF_COUNT; i++) {
        s_rx_bufs[i] = (uint8_t *)heap_caps_malloc(SDIO_PKT_SIZE,
                                                    MALLOC_CAP_DMA);
        if (!s_rx_bufs[i]) {
            LOGE(TAG, "OOM: RX buf %d", i);
            esp_restart();
            return;
        }
        s_rx_handles[i] = sdio_slave_recv_register_buf(s_rx_bufs[i]);
        if (!s_rx_handles[i]) {
            LOGE(TAG, "recv_register_buf failed for %d", i);
            esp_restart();
            return;
        }
        err = sdio_slave_recv_load_buf(s_rx_handles[i]);
        if (err != ESP_OK) {
            LOGE(TAG, "recv_load_buf[%d]: %s", i, esp_err_to_name(err));
            esp_restart();
            return;
        }
    }

    /* 3. Create TX queue and DMA semaphore. */
    s_tx_queue = xQueueCreate(C6_BRIDGE_SDIO_TX_QUEUE_DEPTH,
                              sizeof(tx_queue_item_t));
    configASSERT(s_tx_queue);

    s_tx_dma_sem = xSemaphoreCreateCounting(C6_BRIDGE_SDIO_TX_DMA_SLOTS,
                                            C6_BRIDGE_SDIO_TX_DMA_SLOTS);
    configASSERT(s_tx_dma_sem);

    memset(s_tx_dma_pool, 0, sizeof(s_tx_dma_pool));

    /* 4. Start SDIO slave. */
    err = sdio_slave_start();
    if (err != ESP_OK) {
        LOGE(TAG, "sdio_slave_start: %s — restarting in 3 s",
                 esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        return;
    }

    /* 5. Launch the dedicated TX task. */
    BaseType_t rc = xTaskCreate(sdio_tx_task, "sdio_tx",
                                C6_BRIDGE_SDIO_TX_TASK_STACK, NULL,
                                C6_BRIDGE_SDIO_TX_TASK_PRIO, NULL);
    configASSERT(rc == pdPASS);

    LOGI(TAG, "SDIO slave started — waiting for host enumeration");
}

int bridge_transport_write(const uint8_t *buf, size_t len)
{
    if (!s_host_ready) return -1;
    if (!buf || len == 0 || len > SDIO_PKT_SIZE) return -1;

    tx_queue_item_t item;
    item.len = (uint16_t)len;
    memcpy(item.data, buf, len);

    /* Non-blocking enqueue.  On full queue, evict oldest item. */
    if (xQueueSend(s_tx_queue, &item, 0) == pdTRUE)
        return (int)len;

    /* Queue full — evict oldest. */
    tx_queue_item_t discard;
    if (xQueueReceive(s_tx_queue, &discard, 0) == pdTRUE) {
        LOGW(TAG, "TX queue full — evicted oldest (%u B)", discard.len);
    }
    if (xQueueSend(s_tx_queue, &item, 0) == pdTRUE)
        return (int)len;

    LOGE(TAG, "TX queue insert failed after eviction");
    return -1;
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
        LOGE(TAG, "recv: %s", esp_err_to_name(err));
        return -1;
    }

    size_t copy_len = (recv_len < max_len) ? recv_len : max_len;
    memcpy(buf, recv_data, copy_len);

    /* Recycle buffer immediately. */
    sdio_slave_recv_load_buf(handle);

    return (int)copy_len;
}

size_t bridge_transport_describe(char *buf, size_t buf_size)
{
    return (size_t)snprintf(buf, buf_size,
        "SDIO slave %d-bit: CLK=19 CMD=18 D0-3=20-23 "
        "(rx_bufs=%d tx_q=%d tx_dma=%d)",
        C6_BRIDGE_SDIO_BUS_WIDTH,
        C6_BRIDGE_SDIO_RX_BUF_COUNT,
        C6_BRIDGE_SDIO_TX_QUEUE_DEPTH,
        C6_BRIDGE_SDIO_TX_DMA_SLOTS);
}

// UART transport stubs — compiled when SDIO is NOT selected, providing the
// same host_ready symbols so the linker is satisfied regardless of transport.
// (The #ifdef guard prevents double-definition when both files are compiled
//  with different transport defines — normally only one .cpp is active.)
// Actually these stubs are NOT needed here — they live in bridge_transport_uart.cpp
// or are provided by the inline fallbacks in bridge_transport.h.  No stubs here.

#endif  // C6_BRIDGE_TRANSPORT_SDIO
