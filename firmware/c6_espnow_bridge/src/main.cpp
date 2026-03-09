#include "debug.h"

#include <Arduino.h>
#include "esp_now.h"
#include <WiFi.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bridge_protocol.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <esp_log.h>

// Bring in the physical transport (UART or SDIO)
#include "bridge_transport.h"

// --- Configuration ----------------------------------------------------------

#ifndef C6_BRIDGE_WIFI_CHANNEL
#define C6_BRIDGE_WIFI_CHANNEL 1
#endif

// Structured logging.  Define C6_BRIDGE_LOG_EXT (build flag) to enable verbose
// per-packet and per-event logs beyond the always-on startup / error logs.
static const char *TAG = "c6_bridge";
#ifdef C6_BRIDGE_LOG_EXT
#  define BLOG_I(fmt, ...) LOGI(TAG, fmt, ##__VA_ARGS__)
#  define BLOG_W(fmt, ...) LOGW(TAG, fmt, ##__VA_ARGS__)
#else
#  define BLOG_I(fmt, ...) do {} while (0)
#  define BLOG_W(fmt, ...) do {} while (0)
#endif

// Always-on logs (errors and critical state changes).
#define BLOG_E(fmt, ...) LOGE(TAG, fmt, ##__VA_ARGS__)

// Number of pending TX segments in the ring buffer.
// Sized larger than the SDIO TX queue to absorb ESP-NOW bursts.
#define C6_BRIDGE_TX_QUEUE_LEN 48

// --- Globals ----------------------------------------------------------------

uint32_t g_last_p4_rx_time = 0;
const uint32_t P4_HEARTBEAT_TIMEOUT_MS = 10000; // 10 seconds

// Statistics
static uint32_t s_stat_radio_rx = 0;
static uint32_t s_stat_radio_tx = 0;
static uint32_t s_stat_host_rx  = 0;
static uint32_t s_stat_host_tx  = 0;

static uint8_t s_local_mac[6];

// Verbose serial logging control: enabled at boot, auto-disabled if no
// host activity shortly after startup so we don't waste CPU sending data
// to nowhere.  Timeout can be overridden via build flag.
bool g_c6_serial_verbose = true;
static unsigned long g_c6_serial_disable_deadline = 0;
#ifndef C6_BRIDGE_SERIAL_DISABLE_TIMEOUT_MS
#define C6_BRIDGE_SERIAL_DISABLE_TIMEOUT_MS 30000 // Increased timeout for debugging
#endif

// Queue element for deferred TX to Host (P4)
typedef struct {
    uint16_t len;
    uint8_t buf[BRIDGE_MAX_FRAME_SIZE];
} tx_queue_slot_t;

// simple ring buffer for tx queue
static tx_queue_slot_t s_tx_queue[C6_BRIDGE_TX_QUEUE_LEN];
static volatile uint16_t s_tx_q_head = 0;
static volatile uint16_t s_tx_q_tail = 0;
static portMUX_TYPE s_tx_q_lock = portMUX_INITIALIZER_UNLOCKED;

// Statistics for dropped frames (overflow eviction).
static uint32_t s_stat_tx_evict = 0;

// Helper to push frame to Host via queue.
// If the queue is full, evicts the oldest entry and logs a warning.
// Safe to call from ISR context (ESP-NOW callbacks).
static bool tx_queue_push(uint8_t dir, const uint8_t *mac, const uint8_t *payload, uint16_t payload_len)
{
    if (payload_len > BRIDGE_MAX_PAYLOAD) return false;

    portENTER_CRITICAL_ISR(&s_tx_q_lock);
    uint16_t next_head = (s_tx_q_head + 1) % C6_BRIDGE_TX_QUEUE_LEN;
    if (next_head == s_tx_q_tail) {
        // Queue full — evict oldest to make room for fresh data.
        s_tx_q_tail = (s_tx_q_tail + 1) % C6_BRIDGE_TX_QUEUE_LEN;
        s_stat_tx_evict++;
    }

    tx_queue_slot_t *slot = &s_tx_queue[s_tx_q_head];
    slot->buf[0] = BRIDGE_SOF0;
    slot->buf[1] = BRIDGE_SOF1;
    slot->buf[2] = dir;
    memcpy(&slot->buf[3], mac, BRIDGE_MAC_LEN);
    slot->buf[9]  = (uint8_t)(payload_len & 0xFF);
    slot->buf[10] = (uint8_t)((payload_len >> 8) & 0xFF);
    if (payload_len > 0 && payload) {
        memcpy(&slot->buf[11], payload, payload_len);
    }
    slot->buf[11 + payload_len] = bridge_crc8(mac, payload_len, payload);
    slot->len = BRIDGE_FRAME_OVERHEAD + payload_len;

    s_tx_q_head = next_head;
    portEXIT_CRITICAL_ISR(&s_tx_q_lock);
    return true;
}

static void tx_queue_flush(void)
{
    // Drain ALL queued items.  bridge_transport_write() is non-blocking
    // (enqueues into the SDIO TX task's FreeRTOS queue), so this loop
    // cannot stall and will not starve bridge_transport_read().
    while (s_tx_q_tail != s_tx_q_head) {
        tx_queue_slot_t *slot = &s_tx_queue[s_tx_q_tail];
        if (bridge_transport_write(slot->buf, slot->len) < 0) {
            // Underlying queue full or host not ready; stop and retry next loop.
            break;
        }
        s_stat_host_tx++;

        portENTER_CRITICAL_ISR(&s_tx_q_lock);
        s_tx_q_tail = (s_tx_q_tail + 1) % C6_BRIDGE_TX_QUEUE_LEN;
        portEXIT_CRITICAL_ISR(&s_tx_q_lock);
    }

    // Periodically log eviction stats if any occurred.
    static uint32_t s_last_evict_log = 0;
    if (s_stat_tx_evict > 0 && (millis() - s_last_evict_log) > 5000) {
        LOGW(TAG, "TX queue evictions: %u total", (unsigned)s_stat_tx_evict);
        s_last_evict_log = millis();
    }
}
// --- ESP-NOW Callbacks ------------------------------------------------------

static void esp_now_recv_callback(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    s_stat_radio_rx++;
    C6_LOG("radio-rx #%u len=%d from " MACSTR,
           (unsigned)s_stat_radio_rx, len, MAC2STR(info->src_addr));
    tx_queue_push(BRIDGE_DIR_INCOMING, info->src_addr, data, len);
}

static void esp_now_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    const char *status_str = (status == ESP_NOW_SEND_SUCCESS) ? "OK" : "FAIL";
    // Use LOGE for failures so they survive the verbose-logging gate.
    if (status != ESP_NOW_SEND_SUCCESS) {
        C6_LOG(TAG, "radio-tx #%u FAIL to " MACSTR,
                 (unsigned)s_stat_radio_tx, MAC2STR(mac_addr));
    } else {
        C6_LOG("radio-tx #%u done: OK to " MACSTR,
               (unsigned)s_stat_radio_tx, MAC2STR(mac_addr));
    }

    uint8_t payload[8];
    payload[0] = BRIDGE_CTRL_TX_STATUS;
    memcpy(&payload[1], mac_addr, 6);
    payload[7] = (status == ESP_NOW_SEND_SUCCESS) ? 1 : 0;

    tx_queue_push(BRIDGE_DIR_CONTROL, mac_addr, payload, sizeof(payload));
}

// ---- Hello / debug helpers --------------------------------------------------

static size_t build_hello_text(char *buf, size_t buf_size)
{
    char transport_desc[120];
    bridge_transport_describe(transport_desc, sizeof(transport_desc));

    return (size_t)snprintf(buf, buf_size,
        "\r\n"
        "=== C6 Bridge ===\r\n"
        "Version  : %d\r\n"
        "Build    : " __DATE__ "\r\n"
        "%s\r\n"
        "Channel  : %d\r\n"
        "MAC      : %s\r\n"
        "Status   : ready\r\n",
        BRIDGE_PROTOCOL_VERSION,
        transport_desc,
        C6_BRIDGE_WIFI_CHANNEL,
        WiFi.macAddress().c_str());
}

/**
 * Encode @p text as a BRIDGE_DIR_DEBUG frame and queue it to the host.
 */
static void send_debug_frame(const char *text)
{
    static const uint8_t debug_mac[BRIDGE_MAC_LEN] = BRIDGE_DEBUG_MAC;
    uint16_t len = (uint16_t)strnlen(text, BRIDGE_MAX_PAYLOAD);
    tx_queue_push(BRIDGE_DIR_DEBUG, debug_mac, (const uint8_t *)text, len);
}

/**
 * Respond to a '?' probe received on the bridge link.
 * For UART, also writes plain ASCII so a human with a serial monitor sees it.
 */
static void send_hello_probe_response(void)
{
    char buf[BRIDGE_MAX_PAYLOAD];
    build_hello_text(buf, sizeof(buf));
    C6_PRINT(buf);  // USB-CDC always
#ifndef C6_BRIDGE_TRANSPORT_SDIO
    // UART only: plain ASCII hello so a human monitoring the wire sees it.
    extern void bridge_transport_uart_write_raw(const uint8_t *b, size_t l);
    bridge_transport_uart_write_raw((const uint8_t *)buf, strlen(buf));
#endif
    send_debug_frame(buf);
}

// --- Frame Parser (Host RX) -------------------------------------------------

typedef enum {
    PARSE_SOF0,
    PARSE_SOF1,
    PARSE_DIR,
    PARSE_MAC,
    PARSE_LEN_0,
    PARSE_LEN_1,
    PARSE_PAYLOAD,
    PARSE_CRC
} parse_state_t;

static parse_state_t s_parse_state = PARSE_SOF0;
static uint8_t  s_rx_dir;
static uint8_t  s_rx_mac[BRIDGE_MAC_LEN];
static uint16_t s_rx_len;
static uint16_t s_rx_idx;
static uint8_t  s_rx_payload[BRIDGE_MAX_PAYLOAD];

static void process_frame(void)
{
    s_stat_host_rx++;
    C6_LOG("host-rx #%u dir=0x%02x len=%u",
           (unsigned)s_stat_host_rx, s_rx_dir, (unsigned)s_rx_len);

    if (s_rx_dir == BRIDGE_DIR_OUTGOING) {
        // Forward to ESP-NOW
        esp_err_t err = esp_now_send(s_rx_mac, s_rx_payload, s_rx_len);
        if (err == ESP_OK) {
            s_stat_radio_tx++;
            // Use BLOG_I (ESP_LOGI) so this survives the verbose-logging gate.
            BLOG_I("radio-tx #%u queued to " MACSTR,
                   (unsigned)s_stat_radio_tx, MAC2STR(s_rx_mac));
        } else {
            // Always-on error: visible even after verbose auto-disable.
            LOGE(TAG, "esp-now-send FAILED err=%d to " MACSTR,
                     err, MAC2STR(s_rx_mac));
        }
    } else if (s_rx_dir == BRIDGE_DIR_CONTROL) {
        // Handle control frames
        if (s_rx_len > 0) {
            uint8_t cmd = s_rx_payload[0];
            if (cmd == BRIDGE_CTRL_ADD_PEER && s_rx_len >= 7) {
                esp_now_peer_info_t peer = {};
                memcpy(peer.peer_addr, &s_rx_payload[1], 6);
                peer.channel = C6_BRIDGE_WIFI_CHANNEL;
                peer.ifidx = WIFI_IF_STA;
                peer.encrypt = false;
                if (!esp_now_is_peer_exist(peer.peer_addr)) {
                    esp_now_add_peer(&peer);
                }
            } else if (cmd == BRIDGE_CTRL_DEL_PEER && s_rx_len >= 7) {
                esp_now_del_peer(&s_rx_payload[1]);
            }
        }
    }
}

static void parse_byte(uint8_t c)
{
    if (s_parse_state == PARSE_SOF0 && c == BRIDGE_DEBUG_TRIGGER) {
        bridge_transport_set_host_ready();
        g_last_p4_rx_time = millis();
        /* Send a PONG so the P4's ping-reply wait exits immediately instead
         * of burning through all retries (each retry = REPLY_TIMEOUT +
         * RETRY_INTERVAL ms on the P4 side). */
        uint8_t pong_payload[1] = { BRIDGE_PROTOCOL_VERSION };
        tx_queue_push(BRIDGE_DIR_DEBUG, s_local_mac, pong_payload, sizeof(pong_payload));
        LOGI(TAG, "[ping] debug trigger received — host_ready set, pong queued");
        return;
    }

    switch (s_parse_state) {
        case PARSE_SOF0:
            if (c == BRIDGE_SOF0) s_parse_state = PARSE_SOF1;
            break;
        case PARSE_SOF1:
            if (c == BRIDGE_SOF1) s_parse_state = PARSE_DIR;
            else s_parse_state = (c == BRIDGE_SOF0) ? PARSE_SOF1 : PARSE_SOF0;
            break;
        case PARSE_DIR:
            s_rx_dir = c;
            s_rx_idx = 0;
            s_parse_state = PARSE_MAC;
            break;
        case PARSE_MAC:
            s_rx_mac[s_rx_idx++] = c;
            if (s_rx_idx == BRIDGE_MAC_LEN) s_parse_state = PARSE_LEN_0;
            break;
        case PARSE_LEN_0:
            s_rx_len = c;
            s_parse_state = PARSE_LEN_1;
            break;
        case PARSE_LEN_1:
            s_rx_len |= ((uint16_t)c << 8);
            if (s_rx_len > BRIDGE_MAX_PAYLOAD) {
                s_parse_state = PARSE_SOF0; // invalid len
            } else if (s_rx_len == 0) {
                s_parse_state = PARSE_CRC;
            } else {
                s_rx_idx = 0;
                s_parse_state = PARSE_PAYLOAD;
            }
            break;
        case PARSE_PAYLOAD:
            s_rx_payload[s_rx_idx++] = c;
            if (s_rx_idx == s_rx_len) s_parse_state = PARSE_CRC;
            break;
        case PARSE_CRC: {
            uint8_t expect_crc = bridge_crc8(s_rx_mac, s_rx_len, s_rx_payload);
            if (c == expect_crc) {
                process_frame();
            } else {
                LOGE(TAG, "CRC mismatch: got=0x%02x expect=0x%02x dir=0x%02x len=%u from " MACSTR,
                         c, expect_crc, s_rx_dir, (unsigned)s_rx_len, MAC2STR(s_rx_mac));
            }
            s_parse_state = PARSE_SOF0;
            break;
        }
    }
}

// --- Setup & Main Loop ------------------------------------------------------

void setup()
{
    Serial.begin(115200);
#ifdef C6_BRIDGE_TRANSPORT_SDIO
    /* In SDIO builds, initialise physical UART0 early so that all log output
     * (including the very first line below) reaches the hardware UART pins.
     * Also redirect ESP-IDF log output to the same UART. */
    Serial0.begin(C6_BRIDGE_UART_BAUD, SERIAL_8N1, C6_BRIDGE_UART_RX, C6_BRIDGE_UART_TX);
#ifdef C6_BRIDGE_TRANSPORT_SDIO
    esp_log_set_vprintf(c6_esp_log_vprintf);
#endif
#endif
    delay(100);
    LOGI(TAG, "C6 bridge starting (built " __DATE__ " " __TIME__ ")");
    C6_LOG("[BRIDGE] Booting ESP32-C6 ESP-NOW bridge...\n");

    /* Start a short timeout after which verbose serial logging will be
     * auto-disabled if the host hasn't shown activity. */
    g_c6_serial_disable_deadline = millis() + C6_BRIDGE_SERIAL_DISABLE_TIMEOUT_MS;

    // 1. Initialize Wi-Fi
    LOGI(TAG, "Init WiFi STA channel=%d ...", C6_BRIDGE_WIFI_CHANNEL);
    WiFi.mode(WIFI_STA);
    WiFi.setChannel(C6_BRIDGE_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    WiFi.disconnect();

    esp_read_mac(s_local_mac, ESP_MAC_WIFI_STA);
    LOGI(TAG, "Local MAC: " MACSTR, MAC2STR(s_local_mac));

    if (esp_now_init() != ESP_OK) {
        LOGE(TAG, "ESP-NOW init failed — restarting");
        esp_restart();
    }
    LOGI(TAG, "ESP-NOW init OK");

    esp_now_register_recv_cb(esp_now_recv_callback);
    esp_now_register_send_cb(esp_now_send_callback);

    // Provide a broadcast peer by default
    esp_now_peer_info_t bcast = {};
    memset(bcast.peer_addr, 0xFF, 6);
    bcast.channel = C6_BRIDGE_WIFI_CHANNEL;
    bcast.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&bcast);

    // 2. Initialize Transport
    LOGI(TAG, "Init transport...");
    bridge_transport_init();
    LOGI(TAG, "Setup complete — waiting for P4 host ping");
#ifdef C6_BRIDGE_TRANSPORT_SDIO
    C6_LOG("[BRIDGE] Transport: SDIO slave\n");
#else
    C6_LOG("[BRIDGE] Transport: UART\n");
#endif

    g_last_p4_rx_time = millis();
}

// Periodic stats ticker: print once per STATS_INTERVAL_MS when C6_BRIDGE_LOG_EXT set.
#ifdef C6_BRIDGE_LOG_EXT
#  define C6_STATS_INTERVAL_MS  5000
static uint32_t s_last_stats_ms = 0;
#endif

void loop()
{
    // Feed SDIO packets into parser
    uint8_t pkt_buf[BRIDGE_MAX_FRAME_SIZE];
    int pkt_len = bridge_transport_read(pkt_buf, sizeof(pkt_buf), 5);

    if (pkt_len > 0) {
        bridge_transport_set_host_ready();
        g_last_p4_rx_time = millis();
        // P4 is actively talking — keep verbose logging on.
        g_c6_serial_disable_deadline = 0;
        C6_LOG("host-pkt len=%d", pkt_len);
        for (int i = 0; i < pkt_len; i++) {
            parse_byte(pkt_buf[i]);
        }
    }

    // ---- USB-CDC console → debug / human interaction -----------------------
    while (Serial.available() > 0) {
        uint8_t ch = (uint8_t)Serial.read();
        /* If verbose logging was auto-disabled because the host appeared
         * absent at boot time, re-enable it on any serial activity. */
        if (!g_c6_serial_verbose) {
            g_c6_serial_verbose = true;
            g_c6_serial_disable_deadline = 0;
            C6_LOG("[BRIDGE] Serial activity detected — enabling verbose logs\n");
        }
        if (ch == BRIDGE_DEBUG_TRIGGER) {
            char buf[BRIDGE_MAX_PAYLOAD];
            build_hello_text(buf, sizeof(buf));
            C6_PRINT(buf);
            send_debug_frame(buf);
        }
    }

    /* Auto-disable verbose logging shortly after boot if no host activity
     * was observed in the initial window. */
    if (g_c6_serial_verbose && g_c6_serial_disable_deadline != 0 &&
        millis() > g_c6_serial_disable_deadline) {
        if (Serial.available() == 0) {
            g_c6_serial_verbose = false;
        } else {
            /* Host was active; don't auto-disable in future. */
            g_c6_serial_disable_deadline = 0;
        }
    }

    // Attempt to flush TX queue if host is ready
    if (bridge_transport_is_host_ready()) {
        tx_queue_flush();
    }

    // Heartbeat timeout watchdog
    if (g_last_p4_rx_time > 0 && bridge_transport_is_host_ready()) {
        if (millis() - g_last_p4_rx_time > P4_HEARTBEAT_TIMEOUT_MS) {
            LOGE(TAG, "P4 heartbeat missing for >%u ms — rebooting",
                     (unsigned)P4_HEARTBEAT_TIMEOUT_MS);
            delay(100);
            esp_restart();
        }
    }

#ifdef C6_BRIDGE_LOG_EXT
    uint32_t now_ms = millis();
    if (now_ms - s_last_stats_ms >= C6_STATS_INTERVAL_MS) {
        s_last_stats_ms = now_ms;
        LOGI(TAG, "[stats] radio_rx=%u radio_tx=%u host_rx=%u host_tx=%u "
                      "host_ready=%d",
                 (unsigned)s_stat_radio_rx, (unsigned)s_stat_radio_tx,
                 (unsigned)s_stat_host_rx,  (unsigned)s_stat_host_tx,
                 (int)bridge_transport_is_host_ready());
    }
#endif
}
