/**
 * c6_bridge_sdio.c  —  P4-side SDIO host transport (implementation)
 *
 * See c6_bridge_sdio.h for full documentation.
 *
 * This file is compiled only when REMOTE_COMMS_C6_SDIO_BRIDGE is defined.
 * It uses the ESP-IDF SDMMC host driver together with the
 * esp_serial_slave_link (ESSL) component to talk to the C6 SDIO slave.
 *
 * c6_sdio_bridge_read() is a simple blocking-with-timeout call that
 * remote_comms_wrapper's own RX task drives in a loop — the same pattern used
 * by the UART bridge with uart_read_bytes().
 */

#if defined(ESP32P4_HW) && defined(REMOTE_COMMS_C6_SDIO_BRIDGE)

#include "c6_bridge_sdio.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_serial_slave_link/essl_sdio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

/* BRIDGE_MAX_FRAME_SIZE mirrors the definition in the companion firmware's
 * bridge_protocol.h: BRIDGE_HEADER_SIZE(12) + BRIDGE_MAX_PAYLOAD(250) = 262.
 * It is reproduced here to avoid a fragile cross-project include path.
 * If you change bridge_protocol.h, update this constant to match. */
#ifndef BRIDGE_MAX_FRAME_SIZE
#  define BRIDGE_MAX_FRAME_SIZE  262
#endif

static const char *TAG = "c6_bridge_sdio";

// ─── Build-time pin / bus configuration ──────────────────────────────────────

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
/** Initial SDIO clock frequency in kHz.  SDMMC_FREQ_HIGHSPEED = 40 MHz.   */
#ifndef C6_SDIO_FREQ_KHZ
#  define C6_SDIO_FREQ_KHZ    20000   /* 20 MHz — safe for most boards */
#endif
/** Each SDIO packet is one bridge frame (max 262 bytes). */
#ifndef C6_SDIO_PKT_SIZE
#  define C6_SDIO_PKT_SIZE    BRIDGE_MAX_FRAME_SIZE
#endif

// ─── Module state ─────────────────────────────────────────────────────────────

static sdmmc_card_t     *s_card      = NULL;
static essl_handle_t     s_essl      = NULL;
static SemaphoreHandle_t s_tx_mutex  = NULL;   /* serialise concurrent sends */
static SemaphoreHandle_t s_rx_mutex  = NULL;   /* serialise concurrent reads */

/** One static DMA-capable receive buffer reused on every read call. */
static uint8_t          *s_rx_dma_buf = NULL;

// ─── Public API ───────────────────────────────────────────────────────────────

bool c6_sdio_bridge_init(void)
{
    ESP_LOGI(TAG,
             "Initialising SDIO host (slot=%d, width=%d, freq=%d kHz, "
             "CLK=GPIO%d CMD=GPIO%d D0=GPIO%d D1=GPIO%d D2=GPIO%d D3=GPIO%d)",
             C6_SDIO_HOST_SLOT, C6_SDIO_BUS_WIDTH, C6_SDIO_FREQ_KHZ,
             C6_SDIO_CLK_PIN, C6_SDIO_CMD_PIN,
             C6_SDIO_D0_PIN,  C6_SDIO_D1_PIN,
             C6_SDIO_D2_PIN,  C6_SDIO_D3_PIN);

    /* 1. Allocate card descriptor. */
    s_card = (sdmmc_card_t *)heap_caps_malloc(sizeof(sdmmc_card_t),
                                               MALLOC_CAP_DEFAULT);
    if (!s_card) {
        ESP_LOGE(TAG, "OOM: sdmmc_card_t");
        return false;
    }
    memset(s_card, 0, sizeof(sdmmc_card_t));

    /* 2. Configure SDMMC host. */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot         = C6_SDIO_HOST_SLOT;
    host.max_freq_khz = C6_SDIO_FREQ_KHZ;
    host.flags        = SDMMC_HOST_FLAG_4BIT;   /* also supports 1-bit fallback */

    /* 3. Configure slot (GPIO assignments). */
    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.clk   = C6_SDIO_CLK_PIN;
    slot_cfg.cmd   = C6_SDIO_CMD_PIN;
    slot_cfg.d0    = C6_SDIO_D0_PIN;
#if C6_SDIO_BUS_WIDTH >= 4
    slot_cfg.d1    = C6_SDIO_D1_PIN;
    slot_cfg.d2    = C6_SDIO_D2_PIN;
    slot_cfg.d3    = C6_SDIO_D3_PIN;
    slot_cfg.width = 4;
    host.flags     = SDMMC_HOST_FLAG_4BIT;
#else
    slot_cfg.width = 1;
    host.flags     = SDMMC_HOST_FLAG_1BIT;
#endif
    slot_cfg.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    /* 4. Initialise the SDMMC host hardware. */
    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init: %s", esp_err_to_name(err));
        goto fail;
    }

    err = sdmmc_host_init_slot(C6_SDIO_HOST_SLOT, &slot_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init_slot: %s", esp_err_to_name(err));
        goto fail;
    }

    /* 5. Enumerate the slave card (this also activates SDIO function 0). */
    err = sdmmc_card_init(&host, s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init: %s", esp_err_to_name(err));
        goto fail;
    }
    sdmmc_card_print_info(stdout, s_card);

    /* 6. Create an ESSL device on SDIO function 1.
     *    recv_buffer_size must match SDIO_PKT_SIZE used by the C6 slave. */
    essl_sdio_config_t essl_cfg = {
        .card             = s_card,
        .recv_buffer_size = C6_SDIO_PKT_SIZE,
    };
    err = essl_sdio_init_dev(&s_essl, &essl_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "essl_sdio_init_dev: %s", esp_err_to_name(err));
        goto fail;
    }

    /* 7. Perform the ESSL handshake with the slave. */
    err = essl_init(s_essl, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "essl_init: %s", esp_err_to_name(err));
        goto fail;
    }
    ESP_LOGI(TAG, "ESSL handshake with C6 slave succeeded");

    /* 8. Create the TX and RX mutexes. */
    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) {
        ESP_LOGE(TAG, "OOM: TX mutex");
        goto fail;
    }

    s_rx_mutex = xSemaphoreCreateMutex();
    if (!s_rx_mutex) {
        ESP_LOGE(TAG, "OOM: RX mutex");
        goto fail;
    }

    /* 9. Allocate the shared DMA receive buffer. */
    s_rx_dma_buf = (uint8_t *)heap_caps_malloc(C6_SDIO_PKT_SIZE,
                                                MALLOC_CAP_DMA |
                                                MALLOC_CAP_INTERNAL);
    if (!s_rx_dma_buf) {
        ESP_LOGE(TAG, "OOM: RX DMA buffer");
        goto fail;
    }

    ESP_LOGI(TAG, "C6 SDIO bridge ready");
    return true;

fail:
    c6_sdio_bridge_deinit();
    return false;
}

bool c6_sdio_bridge_write(const uint8_t *buf, size_t len)
{
    if (!s_essl || len == 0 || len > C6_SDIO_PKT_SIZE) {
        ESP_LOGE(TAG, "write: bad state or invalid len %zu", len);
        return false;
    }

    /* Allocate a DMA-accessible copy (essl_send_packet may DMA directly). */
    uint8_t *tx_buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_DMA |
                                                         MALLOC_CAP_INTERNAL);
    if (!tx_buf) {
        ESP_LOGE(TAG, "write: OOM for TX buffer");
        return false;
    }
    memcpy(tx_buf, buf, len);

    /* Serialise concurrent sends. */
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);

    /* Wait until the slave has a receive buffer available. */
    esp_err_t err = essl_wait_for_ready(s_essl, pdMS_TO_TICKS(500));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "essl_wait_for_ready: %s", esp_err_to_name(err));
        xSemaphoreGive(s_tx_mutex);
        heap_caps_free(tx_buf);
        return false;
    }

    err = essl_send_packet(s_essl, tx_buf, len, portMAX_DELAY);

    xSemaphoreGive(s_tx_mutex);
    heap_caps_free(tx_buf);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "essl_send_packet: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool c6_sdio_bridge_read(uint8_t *buf, size_t max_len,
                          size_t *out_len, uint32_t timeout_ms)
{
    if (!s_essl || !s_rx_dma_buf || !buf || !out_len) return false;

    /* Only one reader at a time (the remote_comms_wrapper RX task). */
    xSemaphoreTake(s_rx_mutex, portMAX_DELAY);

    size_t    rx_size = 0;
    /* essl_get_packet expects a timeout in milliseconds.  Do not convert
     * to ticks here (the implementation converts ms->ticks internally). */
    esp_err_t err = essl_get_packet(s_essl, s_rx_dma_buf, C6_SDIO_PKT_SIZE,
                                     &rx_size, timeout_ms);

    xSemaphoreGive(s_rx_mutex);

    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT) {
        return false;   /* nothing available within timeout */
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "essl_get_packet: %s", esp_err_to_name(err));
        /* Avoid busy-looping if the underlying ESSL layer reports invalid
         * arguments (which can happen early during bring-up). Pause briefly
         * to yield CPU and allow other init tasks to make progress. */
        if (err == ESP_ERR_INVALID_ARG) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return false;
    }
    if (rx_size == 0 || rx_size > C6_SDIO_PKT_SIZE) {
        ESP_LOGW(TAG, "read: unexpected rx_size %zu", rx_size);
        return false;
    }

    size_t copy_len = (rx_size < max_len) ? rx_size : max_len;
    memcpy(buf, s_rx_dma_buf, copy_len);
    *out_len = copy_len;
    return true;
}

void c6_sdio_bridge_deinit(void)
{
    if (s_rx_dma_buf) {
        heap_caps_free(s_rx_dma_buf);
        s_rx_dma_buf = NULL;
    }
    if (s_rx_mutex) {
        vSemaphoreDelete(s_rx_mutex);
        s_rx_mutex = NULL;
    }
    if (s_tx_mutex) {
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
    }
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
