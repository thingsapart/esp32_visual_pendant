/**
 * main.cpp  —  ESP32-C6 ESP-NOW ↔ Bridge Firmware
 *
 * Bridges ESP-NOW wireless frames to the ESP32-P4 main chip using a binary
 * framing protocol (bridge_protocol.h).  The physical transport to the P4 is
 * selectable at build time:
 *
 *   Default (no flag)           → UART  (bridge_transport_uart.cpp)
 *   -D C6_BRIDGE_TRANSPORT_SDIO → SDIO slave (bridge_transport_sdio.cpp)
 *
 * Both transports share the same bridge_protocol.h frame format and the same
 * parsing state machine in this file.  Only the call sites that do I/O differ.
 *
 * Build-time knobs (set via platformio.ini build_flags):
 *
 *   UART transport:
 *     C6_BRIDGE_UART_NUM      – Arduino HardwareSerial number (default 0)
 *     C6_BRIDGE_UART_TX       – C6 GPIO for TX (default 16)
 *     C6_BRIDGE_UART_RX       – C6 GPIO for RX (default 17)
 *     C6_BRIDGE_UART_BAUD     – baud rate (default 921600)
 *
 *   SDIO transport:
 *     C6_BRIDGE_SDIO_BUS_WIDTH     – 1 or 4 (default 4)
 *     C6_BRIDGE_SDIO_RX_BUF_COUNT – host→slave buffer pool depth (default 4)
 *     C6_BRIDGE_SDIO_TX_BUF_COUNT – slave→host buffer pool depth (default 4)
 *
 *   Common:
 *     C6_BRIDGE_WIFI_CHANNEL  – ESP-NOW channel (default 1)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <esp_log.h>

#include "bridge_protocol.h"
#include "bridge_transport.h"

// ---- Common default ----

#ifndef C6_BRIDGE_WIFI_CHANNEL
#define C6_BRIDGE_WIFI_CHANNEL 1
#endif

// ---- Globals ----

static const uint8_t k_broadcast_mac[BRIDGE_MAC_LEN] =
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Verbose serial logging control: enabled at boot, auto-disabled if no
// host activity shortly after startup so we don't waste CPU sending data
// to nowhere.  Timeout can be overridden via build flag.
static bool g_c6_serial_verbose = true;
static unsigned long g_c6_serial_disable_deadline = 0;
#ifndef C6_BRIDGE_SERIAL_DISABLE_TIMEOUT_MS
#define C6_BRIDGE_SERIAL_DISABLE_TIMEOUT_MS 30000 // Increased timeout for debugging
#endif

/* In SDIO builds we still want to write short debug output to UART0 (pins
 * RX=30, TX=31) so a connected UART host can see boot/connection messages.
 * Provide defaults here in case the UART transport's file isn't compiled. */
#ifdef C6_BRIDGE_TRANSPORT_SDIO
#ifndef C6_BRIDGE_UART_TX
#define C6_BRIDGE_UART_TX 31
#endif
#ifndef C6_BRIDGE_UART_RX
#define C6_BRIDGE_UART_RX 30
#endif
#ifndef C6_BRIDGE_UART_BAUD
#define C6_BRIDGE_UART_BAUD 115200
#endif
#endif

static void c6_serial_print(const char *s)
{
    if (!g_c6_serial_verbose || !s) return;
    Serial.print(s);
#ifdef C6_BRIDGE_TRANSPORT_SDIO
    // Serial0 = physical UART0 (pins 30/31), initialised early in setup().
    Serial0.write((const uint8_t *)s, strlen(s));
#endif
}

static void c6_serial_vprintf(const char *fmt, ...)
{
    if (!g_c6_serial_verbose || !fmt) return;
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        /* Ensure newline termination for raw writes */
        if ((size_t)n >= sizeof(tmp) - 1) tmp[sizeof(tmp) - 1] = '\0';
        c6_serial_print(tmp);
    }
}

#define C6_LOG(...) c6_serial_vprintf(__VA_ARGS__)
#define C6_PRINT(s)   c6_serial_print(s)

#ifdef C6_BRIDGE_TRANSPORT_SDIO
static int c6_esp_log_vprintf(const char *fmt, va_list ap)
{
    char tmp[512];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    if (n > 0) {
        // Serial0 = physical UART0 (pins 30/31), initialised early in setup().
        Serial0.write((const uint8_t *)tmp, n);
    }
    return n;
}
#endif

// ---- Low-level helpers ------------------------------------------------------

static bool peer_is_known(const uint8_t *mac)
{
    return esp_now_is_peer_exist(mac);
}

static bool peer_add(const uint8_t *mac)
{
    if (peer_is_known(mac)) return true;
    esp_now_peer_info_t info;
    memset(&info, 0, sizeof(info));
    memcpy(info.peer_addr, mac, BRIDGE_MAC_LEN);
    info.channel = C6_BRIDGE_WIFI_CHANNEL;
    info.encrypt = false;
    esp_err_t err = esp_now_add_peer(&info);
    if (err != ESP_OK) {
        C6_LOG("[BRIDGE] esp_now_add_peer failed: %d\n", err);
        return false;
    }
    C6_LOG("[BRIDGE] Added peer %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

/**
 * @brief Encode one bridge frame into @p out_buf.
 * @return Total encoded byte count.
 */
static size_t encode_frame(uint8_t *out_buf, uint8_t dir,
                            const uint8_t *mac, const uint8_t *payload,
                            uint16_t len)
{
    size_t idx = 0;
    out_buf[idx++] = BRIDGE_SOF0;
    out_buf[idx++] = BRIDGE_SOF1;
    out_buf[idx++] = dir;
    memcpy(&out_buf[idx], mac, BRIDGE_MAC_LEN); idx += BRIDGE_MAC_LEN;
    out_buf[idx++] = (uint8_t)(len & 0xFF);
    out_buf[idx++] = (uint8_t)(len >> 8);
    memcpy(&out_buf[idx], payload, len);        idx += len;
    out_buf[idx++] = bridge_crc8(mac, len, payload);
    return idx;
}

// ---- Hello / debug helpers --------------------------------------------------

static struct {
    uint32_t espnow_rx;
    uint32_t espnow_tx_ok;
    uint32_t espnow_tx_err;
    uint32_t p4_rx_frames;
    uint32_t p4_rx_err;
} s_stats = {0};

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
        "Status   : ready\r\n"
        "Stats    : EN_RX=%u EN_TX_OK=%u EN_TX_ERR=%u P4_RX=%u P4_ERR=%u\r\n",
        BRIDGE_PROTOCOL_VERSION,
        transport_desc,
        C6_BRIDGE_WIFI_CHANNEL,
        WiFi.macAddress().c_str(),
        (unsigned)s_stats.espnow_rx,
        (unsigned)s_stats.espnow_tx_ok,
        (unsigned)s_stats.espnow_tx_err,
        (unsigned)s_stats.p4_rx_frames,
        (unsigned)s_stats.p4_rx_err);
}

/**
 * Encode @p text as a BRIDGE_DIR_DEBUG frame and write it via the transport.
 */
static void send_debug_frame(const char *text)
{
    static const uint8_t debug_mac[BRIDGE_MAC_LEN] = BRIDGE_DEBUG_MAC;
    uint16_t len = (uint16_t)strnlen(text, BRIDGE_MAX_PAYLOAD);
    uint8_t  frame[BRIDGE_MAX_FRAME_SIZE];
    size_t   frame_len = encode_frame(frame, BRIDGE_DIR_DEBUG,
                                      debug_mac,
                                      (const uint8_t *)text, len);
    bridge_transport_write(frame, frame_len);
}

/**
 * Respond to a '?' probe received on the bridge link.
 *
 * For UART, also writes plain ASCII so a human with a serial monitor sees it.
 * For SDIO, only the framed DEBUG packet is sent (the P4 would not parse
 * plain ASCII).
 */
static void send_hello_probe_response(void)
{
    char buf[BRIDGE_MAX_PAYLOAD];
    build_hello_text(buf, sizeof(buf));
    C6_PRINT(buf);  // USB-CDC always
#ifndef C6_BRIDGE_TRANSPORT_SDIO
    // UART only: plain ASCII is safe; P4 parser discards non-SOF0 bytes.
    extern void bridge_transport_uart_write_raw(const uint8_t *b, size_t l);
    bridge_transport_uart_write_raw((const uint8_t *)buf, strlen(buf));
#endif
    send_debug_frame(buf);
}

// ---- ESP-NOW → transport (C6 relays received frame to P4) ------------------

static void on_espnow_recv(const esp_now_recv_info_t *info,
                            const uint8_t *data, int len)
{
    s_stats.espnow_rx++;
    if (len <= 0 || len > (int)BRIDGE_MAX_PAYLOAD) {
        C6_LOG("[BRIDGE] RX: invalid len %d, dropping\n", len);
        return;
    }

    peer_add(info->src_addr);   // remember sender so we can reply

    uint8_t frame[BRIDGE_MAX_FRAME_SIZE];
    size_t frame_len = encode_frame(frame, BRIDGE_DIR_INCOMING,
                                    info->src_addr, data, (uint16_t)len);
    bridge_transport_write(frame, frame_len);
}

// ---- Frame parser (transport → ESP-NOW) ------------------------------------
// Byte-oriented state machine — works identically for UART (bytes trickle in)
// and SDIO (a complete frame arrives at once, then fed byte-by-byte here).

typedef enum {
    ST_SOF0,
    ST_SOF1,
    ST_DIR,
    ST_MAC,
    ST_LEN_LO,
    ST_LEN_HI,
    ST_DATA,
    ST_CRC,
} parse_state_t;

static parse_state_t s_rx_state    = ST_SOF0;
static uint8_t       s_rx_dir;
static uint8_t       s_rx_mac[BRIDGE_MAC_LEN];
static uint8_t       s_rx_mac_pos;
static uint16_t      s_rx_len;
static uint16_t      s_rx_data_pos;
static uint8_t       s_rx_data[BRIDGE_MAX_PAYLOAD];

static void dispatch_to_espnow(uint8_t *mac, uint8_t *data, uint16_t len)
{
    peer_add(mac);
    esp_err_t err = esp_now_send(mac, data, len);
    if (err != ESP_OK) {
        s_stats.espnow_tx_err++;
        C6_LOG("[BRIDGE] esp_now_send err=%d\n", err);
    } else {
        s_stats.espnow_tx_ok++;
    }
}

static void process_byte(uint8_t b)
{
    switch (s_rx_state) {
        case ST_SOF0:
            if (b == BRIDGE_SOF0) {
                s_rx_state = ST_SOF1;
            } else if (b == BRIDGE_DEBUG_TRIGGER) {
                send_hello_probe_response();
            }
            // All other bytes silently discarded while idle.
            break;

        case ST_SOF1:
            s_rx_state = (b == BRIDGE_SOF1) ? ST_DIR : ST_SOF0;
            break;

        case ST_DIR:
            s_rx_dir     = b;
            s_rx_mac_pos = 0;
            s_rx_state   = ST_MAC;
            break;

        case ST_MAC:
            s_rx_mac[s_rx_mac_pos++] = b;
            if (s_rx_mac_pos == BRIDGE_MAC_LEN) s_rx_state = ST_LEN_LO;
            break;

        case ST_LEN_LO:
            s_rx_len   = b;
            s_rx_state = ST_LEN_HI;
            break;

        case ST_LEN_HI:
            s_rx_len |= ((uint16_t)b << 8);
            if (s_rx_len == 0 || s_rx_len > BRIDGE_MAX_PAYLOAD) {
                C6_LOG("[BRIDGE] Bad len %u, resync\n", s_rx_len);
                s_rx_state = ST_SOF0;
            } else {
                s_rx_data_pos = 0;
                s_rx_state    = ST_DATA;
            }
            break;

        case ST_DATA:
            s_rx_data[s_rx_data_pos++] = b;
            if (s_rx_data_pos == s_rx_len) s_rx_state = ST_CRC;
            break;

        case ST_CRC: {
            uint8_t expected = bridge_crc8(s_rx_mac, s_rx_len, s_rx_data);
            if (b == expected) {
                s_stats.p4_rx_frames++;
                if (s_rx_dir == BRIDGE_DIR_OUTGOING) {
                    dispatch_to_espnow(s_rx_mac, s_rx_data, s_rx_len);
                } else if (s_rx_dir == BRIDGE_DIR_DEBUG) {
                    // Human-readable frame from P4 — log it, don't relay.
                    uint16_t safe = s_rx_len < BRIDGE_MAX_PAYLOAD
                                    ? s_rx_len
                                    : (uint16_t)(BRIDGE_MAX_PAYLOAD - 1);
                    s_rx_data[safe] = '\0';
                    C6_LOG("[BRIDGE] Debug from P4: %s\n",
                                  (char *)s_rx_data);
                } else {
                    C6_LOG("[BRIDGE] Unexpected DIR=0x%02X\n",
                                  s_rx_dir);
                }
            } else {
                s_stats.p4_rx_err++;
                C6_LOG("[BRIDGE] CRC mismatch: got 0x%02X exp 0x%02X\n",
                              b, expected);
            }
            s_rx_state = ST_SOF0;
            break;
        }
    }
}

// ---- Arduino entry points ---------------------------------------------------

void setup()
{
    // Serial = USB-CDC (ARDUINO_USB_CDC_ON_BOOT=1).
    // Serial0 = physical UART0 (pins RX=C6_BRIDGE_UART_RX / TX=C6_BRIDGE_UART_TX).
    Serial.begin(115200);

#ifdef C6_BRIDGE_TRANSPORT_SDIO
    // In SDIO builds, initialise physical UART0 immediately so that all log
    // output (including the very first line below) reaches pins 30/31 from
    // the start.  Also redirect ESP-IDF log output to the same UART.
    Serial0.begin(C6_BRIDGE_UART_BAUD, SERIAL_8N1, C6_BRIDGE_UART_RX, C6_BRIDGE_UART_TX);
    esp_log_set_vprintf(c6_esp_log_vprintf);
#endif

    C6_LOG("[BRIDGE] Booting ESP32-C6 ESP-NOW bridge...\n");

    /* Start a short timeout after which verbose serial logging will be
     * auto-disabled if the host hasn't shown activity. */
    g_c6_serial_disable_deadline = millis() + C6_BRIDGE_SERIAL_DISABLE_TIMEOUT_MS;

#ifdef C6_BRIDGE_TRANSPORT_SDIO
    C6_LOG("[BRIDGE] Transport: SDIO slave\n");
#else
    C6_LOG("[BRIDGE] Transport: UART\n");
#endif

    // Initialise the physical transport to the P4.
    bridge_transport_init();

    // Wi-Fi (required by ESP-NOW).
    WiFi.mode(WIFI_STA);
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_set_channel(C6_BRIDGE_WIFI_CHANNEL, second);
    C6_LOG("[BRIDGE] WiFi channel = %d\n", C6_BRIDGE_WIFI_CHANNEL);
    C6_LOG("[BRIDGE] C6 MAC: %s\n", WiFi.macAddress().c_str());

    // Initialise ESP-NOW.
    if (esp_now_init() != ESP_OK) {
        C6_LOG("[BRIDGE] FATAL: esp_now_init() failed\n");
        while (true) delay(1000);
    }

    peer_add(k_broadcast_mac);
    esp_now_register_recv_cb(on_espnow_recv);

    // Startup hello — always on USB-CDC, also on bridge transport.
    {
        char buf[BRIDGE_MAX_PAYLOAD];
        build_hello_text(buf, sizeof(buf));
        C6_PRINT(buf);
#ifndef C6_BRIDGE_TRANSPORT_SDIO
        // UART: plain ASCII hello so a human monitoring the wire sees it.
        extern void bridge_transport_uart_write_raw(const uint8_t *b, size_t l);
        bridge_transport_uart_write_raw((const uint8_t *)buf, strlen(buf));
#endif
        send_debug_frame(buf);
    }
}

void loop()
{
    // ---- Transport → ESP-NOW (parse incoming bridge frames) ----------------
#ifdef C6_BRIDGE_TRANSPORT_SDIO
    // SDIO: block up to 5 ms for a complete packet, feed bytes to the parser.
    uint8_t pkt_buf[BRIDGE_MAX_FRAME_SIZE];
    int     pkt_len = bridge_transport_read(pkt_buf, sizeof(pkt_buf), 5);
    if (pkt_len > 0) {
        for (int i = 0; i < pkt_len; i++) process_byte(pkt_buf[i]);
    }
#else
    // UART: drain whatever bytes are sitting in the FIFO right now.
    {
        uint8_t byte_buf[64];
        int n = bridge_transport_read(byte_buf, sizeof(byte_buf), 0);
        for (int i = 0; i < n; i++) process_byte(byte_buf[i]);
    }
#endif

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

    // ---- Periodic status/debug frames and optional ESP-NOW beacon ---------
    // Configurable intervals: status frames are sent to the P4 (DEBUG frames)
    // regardless of whether the serial console is enabled; the ESP-NOW
    // beacon is broadcast to help a listening host detect the bridge.
#ifndef C6_BRIDGE_STATUS_INTERVAL_MS
#define C6_BRIDGE_STATUS_INTERVAL_MS 10000
#endif
#ifndef C6_BRIDGE_BEACON_INTERVAL_MS
#define C6_BRIDGE_BEACON_INTERVAL_MS 3000
#endif
    static unsigned long status_deadline = 0;
    static unsigned long beacon_deadline = 0;
    unsigned long now = millis();

    if (status_deadline == 0) status_deadline = now + C6_BRIDGE_STATUS_INTERVAL_MS;
    if (now >= status_deadline) {
        char buf[BRIDGE_MAX_PAYLOAD];
        build_hello_text(buf, sizeof(buf));
        send_debug_frame(buf);
        status_deadline = now + C6_BRIDGE_STATUS_INTERVAL_MS;
    }

    if (beacon_deadline == 0) beacon_deadline = now + C6_BRIDGE_BEACON_INTERVAL_MS;
    if (now >= beacon_deadline) {
        // Small presence beacon over ESP-NOW (broadcast).  Ignore failures.
        const uint8_t beacon_payload[] = { 'B', 'E', 'A', 'C', 'O', 'N' };
        esp_now_send(k_broadcast_mac, beacon_payload, sizeof(beacon_payload));
        beacon_deadline = now + C6_BRIDGE_BEACON_INTERVAL_MS;
    }

    // Yield so the radio stack gets CPU time (SDIO loop already yields via
    // its 5 ms read timeout).
#ifndef C6_BRIDGE_TRANSPORT_SDIO
    delay(1);
#endif
}
