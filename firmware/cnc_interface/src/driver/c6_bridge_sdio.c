/**
 * c6_bridge_sdio.c  —  P4-side SDIO host transport — v2 clean-room redesign
 *
 * Architecture:
 *   ONE dedicated FreeRTOS task ("sdio_bus") owns ALL access to the ESSL
 *   handle.  No mutex is needed.  In a tight loop it:
 *     1. Does a non-blocking essl_get_packet() — if data arrived, pushes it
 *        into s_rx_queue (a FreeRTOS queue).
 *     2. Checks s_tx_queue — if a frame is waiting, calls essl_send_packet().
 *     3. If nothing happened in either direction, yields for 1 ms.
 *
 *   TX callers (any task/core): c6_sdio_bridge_write() pushes into s_tx_queue
 *     — non-blocking, never touches ESSL, cannot deadlock.
 *
 *   RX callers (bridged_sdio_rx_task in esp_bridged_esp_now.c):
 *     c6_sdio_bridge_read() pops from s_rx_queue with timeout — never touches
 *     ESSL, cannot deadlock.
 *
 *   Deadlock analysis:
 *     - No mutex exists.
 *     - FreeRTOS queues are safe for multi-producer/multi-consumer.
 *     - The bus task is the sole ESSL accessor — no concurrent bus access.
 *     - TX callers never block on ESSL, only on queue space (bounded, short).
 *     - RX callers never block on ESSL, only on queue data (bounded, short).
 *
 *   Pre-allocated DMA buffers: no per-call heap allocation.
 */

#if defined(ESP32P4_HW) && defined(REMOTE_COMMS_C6_SDIO_BRIDGE)

#define UI_DEBUG_LOCAL_LEVEL D_WARN
#include "debug.h"

#include "c6_bridge_sdio.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_serial_slave_link/essl_sdio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

static const char *TAG = "c6_sdio";

/* ═══════════════════════════════════════════════════════════════════════════
 *  Constants (build-time overridable)
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifndef BRIDGE_MAX_FRAME_SIZE
#  define BRIDGE_MAX_FRAME_SIZE  262
#endif

#ifndef C6_SDIO_HOST_SLOT
#  define C6_SDIO_HOST_SLOT   SDMMC_HOST_SLOT_1
#endif
#ifndef C6_SDIO_CLK_PIN
#  define C6_SDIO_CLK_PIN     43
#endif
#ifndef C6_SDIO_CMD_PIN
#  define C6_SDIO_CMD_PIN     44
#endif
#ifndef C6_SDIO_D0_PIN
#  define C6_SDIO_D0_PIN      39
#endif
#ifndef C6_SDIO_D1_PIN
#  define C6_SDIO_D1_PIN      40
#endif
#ifndef C6_SDIO_D2_PIN
#  define C6_SDIO_D2_PIN      41
#endif
#ifndef C6_SDIO_D3_PIN
#  define C6_SDIO_D3_PIN      42
#endif
#ifndef C6_SDIO_BUS_WIDTH
#  define C6_SDIO_BUS_WIDTH   4
#endif
#ifndef C6_SDIO_FREQ_KHZ
#  define C6_SDIO_FREQ_KHZ    20000
#endif
#ifndef C6_SDIO_PKT_SIZE
#  define C6_SDIO_PKT_SIZE    BRIDGE_MAX_FRAME_SIZE
#endif

/* Queue depths. */
#ifndef C6_SDIO_TX_QUEUE_DEPTH
#  define C6_SDIO_TX_QUEUE_DEPTH  16
#endif
#ifndef C6_SDIO_RX_QUEUE_DEPTH
#  define C6_SDIO_RX_QUEUE_DEPTH  16
#endif

/* Bus task configuration. */
#ifndef C6_SDIO_BUS_TASK_STACK
#  define C6_SDIO_BUS_TASK_STACK  6144
#endif
#ifndef C6_SDIO_BUS_TASK_PRIO
#  define C6_SDIO_BUS_TASK_PRIO   (tskIDLE_PRIORITY + 3)
#endif
#ifndef C6_SDIO_BUS_TASK_CORE
#  define C6_SDIO_BUS_TASK_CORE   1   /* Same core as machine pipeline. */
#endif

/* How many consecutive ESSL errors before attempting bus recovery. */
#define BUS_ERROR_THRESHOLD  5

/* ═══════════════════════════════════════════════════════════════════════════
 *  Queue item types
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint16_t len;
    uint8_t  data[C6_SDIO_PKT_SIZE];
} sdio_frame_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Module state
 * ═══════════════════════════════════════════════════════════════════════════ */

static sdmmc_card_t  *s_card = NULL;
static essl_handle_t  s_essl = NULL;

/* Pre-allocated DMA buffers for bus task — never freed, no per-call malloc. */
static uint8_t       *s_rx_dma_buf = NULL;   /* for essl_get_packet      */
static uint8_t       *s_tx_dma_buf = NULL;   /* for essl_send_packet     */

/* FreeRTOS queues for decoupled, deadlock-free data flow. */
static QueueHandle_t  s_tx_queue  = NULL;    /* any task → bus task      */
static QueueHandle_t  s_rx_queue  = NULL;    /* bus task → reader task   */

static TaskHandle_t   s_bus_task  = NULL;

/* Statistics (atomic increments from single-writer bus task). */
static volatile uint32_t s_stat_tx_ok     = 0;
static volatile uint32_t s_stat_tx_fail   = 0;
static volatile uint32_t s_stat_rx_ok     = 0;
static volatile uint32_t s_stat_rx_drop   = 0;  /* RX queue full, evicted. */
static volatile uint32_t s_stat_bus_err   = 0;
static volatile uint32_t s_stat_rx_nfound = 0;  /* essl_get_packet: no data */
static volatile uint32_t s_stat_tx_queued = 0;  /* items popped from s_tx_queue */

/* ═══════════════════════════════════════════════════════════════════════════
 *  Bus recovery
 * ═══════════════════════════════════════════════════════════════════════════ */

static bool do_bus_recovery(void)
{
    LOGE(TAG, "=== SDIO BUS RECOVERY ===");

    /* Tear down ESSL + SDMMC host. */
    if (s_essl) {
        essl_sdio_deinit_dev(s_essl);
        s_essl = NULL;
    }
    if (s_card) {
        sdmmc_host_deinit();
        heap_caps_free(s_card);
        s_card = NULL;
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    /* Re-init from scratch.  We keep the DMA buffers and queues — they are
     * independent of the bus hardware state. */
    s_card = (sdmmc_card_t *)heap_caps_calloc(1, sizeof(sdmmc_card_t),
                                               MALLOC_CAP_DEFAULT);
    if (!s_card) return false;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot         = C6_SDIO_HOST_SLOT;
    host.max_freq_khz = C6_SDIO_FREQ_KHZ;
    host.flags        = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.clk   = C6_SDIO_CLK_PIN;
    slot_cfg.cmd   = C6_SDIO_CMD_PIN;
    slot_cfg.d0    = C6_SDIO_D0_PIN;
#if C6_SDIO_BUS_WIDTH >= 4
    slot_cfg.d1    = C6_SDIO_D1_PIN;
    slot_cfg.d2    = C6_SDIO_D2_PIN;
    slot_cfg.d3    = C6_SDIO_D3_PIN;
    slot_cfg.width = 4;
#else
    slot_cfg.width = 1;
    host.flags     = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
#endif
    slot_cfg.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) { LOGE(TAG, "recovery: host_init: %s", esp_err_to_name(err)); return false; }

    err = sdmmc_host_init_slot(C6_SDIO_HOST_SLOT, &slot_cfg);
    if (err != ESP_OK) { LOGE(TAG, "recovery: init_slot: %s", esp_err_to_name(err)); return false; }

    vTaskDelay(pdMS_TO_TICKS(1000));

    err = sdmmc_card_init(&host, s_card);
    if (err != ESP_OK) { LOGE(TAG, "recovery: card_init: %s", esp_err_to_name(err)); return false; }

    essl_sdio_config_t essl_cfg = {
        .card             = s_card,
        .recv_buffer_size = C6_SDIO_PKT_SIZE,
    };
    err = essl_sdio_init_dev(&s_essl, &essl_cfg);
    if (err != ESP_OK) { LOGE(TAG, "recovery: essl_init_dev: %s", esp_err_to_name(err)); return false; }

    err = essl_init(s_essl, 10000);
    if (err != ESP_OK) { LOGE(TAG, "recovery: essl_init: %s", esp_err_to_name(err)); return false; }

    LOGI(TAG, "Bus recovery successful");
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Bus task — sole accessor of ESSL
 *
 *  Loop:
 *    1. Non-blocking RX: essl_get_packet with 0 timeout.
 *       - On success: push into s_rx_queue (evict oldest on full).
 *       - On ESP_ERR_NOT_FOUND: no data, move on.
 *    2. Non-blocking TX: peek s_tx_queue.
 *       - If item waiting: essl_send_packet, on success dequeue.
 *    3. If neither RX nor TX produced work: vTaskDelay(1) to yield.
 *
 *  This ensures the bus is never held for more than one operation at a time
 *  and both directions make progress every iteration.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void sdio_bus_task(void *arg)
{
    (void)arg;
    LOGI(TAG, "Bus task started on core %d  essl=%p rx_dma=%p tx_dma=%p"
             " tx_q=%p rx_q=%p",
             xPortGetCoreID(), s_essl, s_rx_dma_buf, s_tx_dma_buf,
             (void *)s_tx_queue, (void *)s_rx_queue);

    uint32_t consecutive_errors = 0;
    uint32_t loop_count = 0;
    TickType_t last_stats = xTaskGetTickCount();

    while (true) {
        bool did_work = false;
        loop_count++;

        /* ── 1. RX: non-blocking read ── */
        if (s_essl && s_rx_dma_buf) {
            size_t rx_size = 0;
            esp_err_t err = essl_get_packet(s_essl, s_rx_dma_buf,
                                            C6_SDIO_PKT_SIZE, &rx_size, 0);
            if (err == ESP_OK && rx_size > 0 && rx_size <= C6_SDIO_PKT_SIZE) {
                sdio_frame_t frame;
                frame.len = (uint16_t)rx_size;
                memcpy(frame.data, s_rx_dma_buf, rx_size);

                if (xQueueSend(s_rx_queue, &frame, 0) != pdTRUE) {
                    /* RX queue full — evict oldest to keep fresh data flowing. */
                    sdio_frame_t discard;
                    xQueueReceive(s_rx_queue, &discard, 0);
                    xQueueSend(s_rx_queue, &frame, 0);
                    s_stat_rx_drop++;
                }
                s_stat_rx_ok++;
                consecutive_errors = 0;
                did_work = true;
                /* Log first 20 RX events for diagnostics. */
                if (s_stat_rx_ok <= 20) {
                    LOGI(TAG, "[bus] RX #%lu: %u B  sof=0x%02x%02x",
                             (unsigned long)s_stat_rx_ok, (unsigned)rx_size,
                             s_rx_dma_buf[0],
                             rx_size > 1 ? s_rx_dma_buf[1] : 0);
                }
            } else if (err != ESP_OK && err != ESP_ERR_NOT_FOUND
                       && err != ESP_ERR_TIMEOUT) {
                consecutive_errors++;
                s_stat_bus_err++;
                LOGW(TAG, "[bus] RX err: %s (streak=%lu)",
                         esp_err_to_name(err),
                         (unsigned long)consecutive_errors);
            } else {
                /* Normal: no data available. */
                s_stat_rx_nfound++;
            }
        } else {
            /* Log once if pre-conditions aren't met. */
            if (loop_count <= 3) {
                LOGE(TAG, "[bus] RX skip: essl=%p rx_dma=%p",
                         s_essl, s_rx_dma_buf);
            }
        }

        /* ── 2. TX: drain one frame from the queue ── */
        if (s_essl && s_tx_dma_buf) {
            sdio_frame_t tx_frame;
            if (xQueueReceive(s_tx_queue, &tx_frame, 0) == pdTRUE) {
                s_stat_tx_queued++;
                memset(s_tx_dma_buf, 0, C6_SDIO_PKT_SIZE);
                memcpy(s_tx_dma_buf, tx_frame.data, tx_frame.len);

                esp_err_t err = essl_send_packet(s_essl, s_tx_dma_buf,
                                                 C6_SDIO_PKT_SIZE,
                                                 pdMS_TO_TICKS(100));
                if (err == ESP_OK) {
                    s_stat_tx_ok++;
                    consecutive_errors = 0;
                    /* Log first 20 TX events for diagnostics. */
                    if (s_stat_tx_ok <= 20) {
                        LOGI(TAG, "[bus] TX #%lu: %u B  first=0x%02x",
                                 (unsigned long)s_stat_tx_ok,
                                 (unsigned)tx_frame.len,
                                 tx_frame.data[0]);
                    }
                } else {
                    s_stat_tx_fail++;
                    consecutive_errors++;
                    LOGW(TAG, "[bus] TX err: %s  len=%u first=0x%02x "
                             "(streak=%lu)",
                             esp_err_to_name(err),
                             (unsigned)tx_frame.len,
                             tx_frame.data[0],
                             (unsigned long)consecutive_errors);
                }
                did_work = true;
            }
        } else {
            if (loop_count <= 3) {
                LOGE(TAG, "[bus] TX skip: essl=%p tx_dma=%p",
                         s_essl, s_tx_dma_buf);
            }
        }

        /* ── 3. Error recovery ── */
        if (consecutive_errors >= BUS_ERROR_THRESHOLD) {
            LOGE(TAG, "Bus error threshold reached (%lu) — attempting recovery",
                     (unsigned long)consecutive_errors);
            if (do_bus_recovery()) {
                consecutive_errors = 0;
            } else {
                LOGE(TAG, "Recovery failed — retrying in 2 s");
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            continue;
        }

        /* ── 4. Periodic stats ── */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_stats) >= pdMS_TO_TICKS(5000)) {
            last_stats = now;
            LOGI(TAG, "[stats] loops=%lu tx_ok=%lu tx_fail=%lu "
                     "tx_queued=%lu rx_ok=%lu rx_nf=%lu "
                     "rx_drop=%lu bus_err=%lu "
                     "txQ=%u/%d rxQ=%u/%d",
                     (unsigned long)loop_count,
                     (unsigned long)s_stat_tx_ok,
                     (unsigned long)s_stat_tx_fail,
                     (unsigned long)s_stat_tx_queued,
                     (unsigned long)s_stat_rx_ok,
                     (unsigned long)s_stat_rx_nfound,
                     (unsigned long)s_stat_rx_drop,
                     (unsigned long)s_stat_bus_err,
                     (unsigned)(s_tx_queue ? uxQueueMessagesWaiting(s_tx_queue) : 0),
                     C6_SDIO_TX_QUEUE_DEPTH,
                     (unsigned)(s_rx_queue ? uxQueueMessagesWaiting(s_rx_queue) : 0),
                     C6_SDIO_RX_QUEUE_DEPTH);
        }

        /* ── 5. ALWAYS yield — even when busy.  Without this,
         *  continuous data flow keeps did_work==true and this task
         *  starves everything at prio ≤ C6_SDIO_BUS_TASK_PRIO on the
         *  same core (bridged_rx, swdraw workers, heartbeat, etc.).
         *  In the SDIO build C6_SDIO_BUS_TASK_PRIO is overridden to 2
         *  (display-jc8012p4a1-sdio env) so the LVGL swdraw workers
         *  (prio 3, unpinned) can be scheduled on Core 1 alongside
         *  lvgl_task (Core 0), enabling true parallel SW rendering.
         *  1 tick ≈ 1 ms is enough for the scheduler to service them. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

bool c6_sdio_bridge_init(void)
{
    LOGI(TAG, "Init SDIO host (slot=%d width=%d freq=%d kHz "
             "CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d)",
             C6_SDIO_HOST_SLOT, C6_SDIO_BUS_WIDTH, C6_SDIO_FREQ_KHZ,
             C6_SDIO_CLK_PIN, C6_SDIO_CMD_PIN,
             C6_SDIO_D0_PIN, C6_SDIO_D1_PIN, C6_SDIO_D2_PIN, C6_SDIO_D3_PIN);

    /* 1. Allocate card descriptor. */
    s_card = (sdmmc_card_t *)heap_caps_calloc(1, sizeof(sdmmc_card_t),
                                               MALLOC_CAP_DEFAULT);
    if (!s_card) { LOGE(TAG, "OOM: card"); return false; }

    /* 2. Configure SDMMC host. */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot         = C6_SDIO_HOST_SLOT;
    host.max_freq_khz = C6_SDIO_FREQ_KHZ;
    host.flags        = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;

    /* 3. Configure slot. */
    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.clk   = C6_SDIO_CLK_PIN;
    slot_cfg.cmd   = C6_SDIO_CMD_PIN;
    slot_cfg.d0    = C6_SDIO_D0_PIN;
#if C6_SDIO_BUS_WIDTH >= 4
    slot_cfg.d1    = C6_SDIO_D1_PIN;
    slot_cfg.d2    = C6_SDIO_D2_PIN;
    slot_cfg.d3    = C6_SDIO_D3_PIN;
    slot_cfg.width = 4;
#else
    slot_cfg.width = 1;
    host.flags     = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
#endif
    slot_cfg.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    /* 4. Initialise SDMMC host. */
    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        LOGE(TAG, "sdmmc_host_init: %s", esp_err_to_name(err));
        goto fail;
    }
    err = sdmmc_host_init_slot(C6_SDIO_HOST_SLOT, &slot_cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "sdmmc_host_init_slot: %s", esp_err_to_name(err));
        goto fail;
    }

    /* 5. Wait for C6 to boot its SDIO slave. */
    LOGI(TAG, "Waiting 1.5 s for C6 SDIO slave...");
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* 6. Enumerate slave. */
    err = sdmmc_card_init(&host, s_card);
    if (err != ESP_OK) {
        LOGE(TAG, "sdmmc_card_init: %s", esp_err_to_name(err));
        goto fail;
    }
    sdmmc_card_print_info(stdout, s_card);

    /* 7. Create ESSL device. */
    essl_sdio_config_t essl_cfg = {
        .card             = s_card,
        .recv_buffer_size = C6_SDIO_PKT_SIZE,
    };
    err = essl_sdio_init_dev(&s_essl, &essl_cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "essl_sdio_init_dev: %s", esp_err_to_name(err));
        goto fail;
    }

    /* 8. ESSL handshake (10 s timeout). */
    err = essl_init(s_essl, 10000);
    if (err != ESP_OK) {
        LOGE(TAG, "essl_init: %s", esp_err_to_name(err));
        goto fail;
    }
    LOGI(TAG, "ESSL handshake OK");

    /* 9. Host-ready ping: send '?' to open the C6's TX gate. */
    {
        uint8_t *ping = (uint8_t *)heap_caps_calloc(C6_SDIO_PKT_SIZE, 1,
                                                     MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        uint8_t *rx_tmp = (uint8_t *)heap_caps_malloc(C6_SDIO_PKT_SIZE,
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (ping && rx_tmp) {
            ping[0] = 0x3F;  /* '?' */
            bool acked = false;
            for (int attempt = 0; attempt < 5 && !acked; attempt++) {
                esp_err_t perr = essl_send_packet(s_essl, ping,
                                                  C6_SDIO_PKT_SIZE,
                                                  pdMS_TO_TICKS(500));
                if (perr != ESP_OK) {
                    LOGW(TAG, "Ping %d/5 send: %s", attempt + 1,
                             esp_err_to_name(perr));
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                size_t rx_sz = 0;
                esp_err_t rerr = essl_get_packet(s_essl, rx_tmp,
                                                 C6_SDIO_PKT_SIZE,
                                                 &rx_sz, 200);
                    if (rerr == ESP_OK && rx_sz > 0) {
                    LOGI(TAG, "C6 replied (%u B) on ping %d", (unsigned)rx_sz,
                             attempt + 1);
                    acked = true;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
            if (!acked) {
                LOGW(TAG, "C6 did not reply to ping — will open gate on "
                              "first app traffic");
            }
        }
        if (ping)   heap_caps_free(ping);
        if (rx_tmp) heap_caps_free(rx_tmp);
    }

    /* 10. Allocate pre-allocated DMA buffers for the bus task. */
    s_rx_dma_buf = (uint8_t *)heap_caps_malloc(C6_SDIO_PKT_SIZE,
                                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_tx_dma_buf = (uint8_t *)heap_caps_malloc(C6_SDIO_PKT_SIZE,
                                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_rx_dma_buf || !s_tx_dma_buf) {
        LOGE(TAG, "OOM: DMA buffers");
        goto fail;
    }

    /* 11. Create TX and RX queues. */
    if (!s_tx_queue)
        s_tx_queue = xQueueCreate(C6_SDIO_TX_QUEUE_DEPTH, sizeof(sdio_frame_t));
    if (!s_rx_queue)
        s_rx_queue = xQueueCreate(C6_SDIO_RX_QUEUE_DEPTH, sizeof(sdio_frame_t));
    if (!s_tx_queue || !s_rx_queue) {
        LOGE(TAG, "OOM: queues");
        goto fail;
    }

    /* 12. Start the bus task (if not already running). */
    if (!s_bus_task) {
        BaseType_t rc = xTaskCreatePinnedToCore(
            sdio_bus_task, "sdio_bus",
            C6_SDIO_BUS_TASK_STACK, NULL,
            C6_SDIO_BUS_TASK_PRIO, &s_bus_task,
            C6_SDIO_BUS_TASK_CORE);
        if (rc != pdPASS) {
            LOGE(TAG, "Failed to create bus task");
            goto fail;
        }
    }

    LOGI(TAG, "C6 SDIO bridge ready (tx_q=%d rx_q=%d)",
             C6_SDIO_TX_QUEUE_DEPTH, C6_SDIO_RX_QUEUE_DEPTH);
    return true;

fail:
    c6_sdio_bridge_deinit();
    return false;
}

bool c6_sdio_bridge_write(const uint8_t *buf, size_t len)
{
    if (!s_tx_queue || !buf || len == 0 || len > C6_SDIO_PKT_SIZE)
        return false;

    sdio_frame_t frame;
    frame.len = (uint16_t)len;
    memcpy(frame.data, buf, len);

    /* Non-blocking enqueue.  If full, evict oldest and retry.
     * This guarantees the calling task NEVER blocks on SDIO. */
    if (xQueueSend(s_tx_queue, &frame, 0) == pdTRUE)
        return true;

    /* Queue full — evict oldest. */
    sdio_frame_t discard;
    if (xQueueReceive(s_tx_queue, &discard, 0) == pdTRUE) {
        LOGW(TAG, "TX queue full — evicted oldest (%u B)", discard.len);
    }
    return (xQueueSend(s_tx_queue, &frame, 0) == pdTRUE);
}

bool c6_sdio_bridge_read(uint8_t *buf, size_t max_len,
                          size_t *out_len, uint32_t timeout_ms)
{
    if (!s_rx_queue || !buf || !out_len) return false;

    sdio_frame_t frame;
    if (xQueueReceive(s_rx_queue, &frame, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return false;

    size_t copy = (frame.len < max_len) ? frame.len : max_len;
    memcpy(buf, frame.data, copy);
    *out_len = copy;
    return true;
}

void c6_sdio_bridge_deinit(void)
{
    /* NOTE: We do NOT delete the bus task, queues, or DMA buffers here.
     * They survive across recovery cycles.  Only the ESSL + SDMMC state
     * is torn down. */
    if (s_rx_dma_buf) { heap_caps_free(s_rx_dma_buf); s_rx_dma_buf = NULL; }
    if (s_tx_dma_buf) { heap_caps_free(s_tx_dma_buf); s_tx_dma_buf = NULL; }

    if (s_essl) {
        essl_sdio_deinit_dev(s_essl);
        s_essl = NULL;
    }
    if (s_card) {
        sdmmc_host_deinit();
        heap_caps_free(s_card);
        s_card = NULL;
    }
}

bool c6_sdio_bridge_is_ready(void)
{
    return (s_essl != NULL) && (s_card != NULL);
}

#endif /* ESP32P4_HW && REMOTE_COMMS_C6_SDIO_BRIDGE */
