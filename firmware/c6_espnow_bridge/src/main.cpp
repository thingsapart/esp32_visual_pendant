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

#include "bridge_protocol.h"
#include "bridge_transport.h"

// ---- Common default ----

#ifndef C6_BRIDGE_WIFI_CHANNEL
#define C6_BRIDGE_WIFI_CHANNEL 1
#endif

// ---- Globals ----

static const uint8_t k_broadcast_mac[BRIDGE_MAC_LEN] =
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

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
        Serial.printf("[BRIDGE] esp_now_add_peer failed: %d\n", err);
        return false;
    }
    Serial.printf("[BRIDGE] Added peer %02X:%02X:%02X:%02X:%02X:%02X\n",
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

static size_t build_hello_text(char *buf, size_t buf_size)
{
    char transport_desc[80];
    bridge_transport_describe(transport_desc, sizeof(transport_desc));

    return (size_t)snprintf(buf, buf_size,
        "\r\n"
        "=== C6 ESP-NOW Bridge ===\r\n"
        "Protocol version : %d\r\n"
        "Build            : " __DATE__ " " __TIME__ "\r\n"
        "%s\r\n"
        "WiFi channel     : %d\r\n"
        "C6 MAC           : %s\r\n"
        "Status           : ready\r\n"
        "Send '?' for this info again\r\n"
        "=========================\r\n",
        BRIDGE_PROTOCOL_VERSION,
        transport_desc,
        C6_BRIDGE_WIFI_CHANNEL,
        WiFi.macAddress().c_str());
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
    Serial.print(buf);  // USB-CDC always
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
    if (len <= 0 || len > (int)BRIDGE_MAX_PAYLOAD) {
        Serial.printf("[BRIDGE] RX: invalid len %d, dropping\n", len);
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
        Serial.printf("[BRIDGE] esp_now_send err=%d\n", err);
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
                Serial.printf("[BRIDGE] Bad len %u, resync\n", s_rx_len);
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
                if (s_rx_dir == BRIDGE_DIR_OUTGOING) {
                    dispatch_to_espnow(s_rx_mac, s_rx_data, s_rx_len);
                } else if (s_rx_dir == BRIDGE_DIR_DEBUG) {
                    // Human-readable frame from P4 — log it, don't relay.
                    uint16_t safe = s_rx_len < BRIDGE_MAX_PAYLOAD
                                    ? s_rx_len
                                    : (uint16_t)(BRIDGE_MAX_PAYLOAD - 1);
                    s_rx_data[safe] = '\0';
                    Serial.printf("[BRIDGE] Debug from P4: %s\n",
                                  (char *)s_rx_data);
                } else {
                    Serial.printf("[BRIDGE] Unexpected DIR=0x%02X\n",
                                  s_rx_dir);
                }
            } else {
                Serial.printf("[BRIDGE] CRC mismatch: got 0x%02X exp 0x%02X\n",
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
    // USB-CDC console (with ARDUINO_USB_CDC_ON_BOOT=1 Serial = USB-CDC).
    Serial.begin(115200);
    Serial.println("[BRIDGE] Booting ESP32-C6 ESP-NOW bridge...");

#ifdef C6_BRIDGE_TRANSPORT_SDIO
    Serial.println("[BRIDGE] Transport: SDIO slave");
#else
    Serial.println("[BRIDGE] Transport: UART");
#endif

    // Initialise the physical transport to the P4.
    bridge_transport_init();

    // Wi-Fi (required by ESP-NOW).
    WiFi.mode(WIFI_STA);
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_set_channel(C6_BRIDGE_WIFI_CHANNEL, second);
    Serial.printf("[BRIDGE] WiFi channel = %d\n", C6_BRIDGE_WIFI_CHANNEL);
    Serial.printf("[BRIDGE] C6 MAC: %s\n", WiFi.macAddress().c_str());

    // Initialise ESP-NOW.
    if (esp_now_init() != ESP_OK) {
        Serial.println("[BRIDGE] FATAL: esp_now_init() failed");
        while (true) delay(1000);
    }

    peer_add(k_broadcast_mac);
    esp_now_register_recv_cb(on_espnow_recv);

    // Startup hello — always on USB-CDC, also on bridge transport.
    {
        char buf[BRIDGE_MAX_PAYLOAD];
        build_hello_text(buf, sizeof(buf));
        Serial.print(buf);
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
        if (ch == BRIDGE_DEBUG_TRIGGER) {
            char buf[BRIDGE_MAX_PAYLOAD];
            build_hello_text(buf, sizeof(buf));
            Serial.print(buf);
        }
    }

    // Yield so the radio stack gets CPU time (SDIO loop already yields via
    // its 5 ms read timeout).
#ifndef C6_BRIDGE_TRANSPORT_SDIO
    delay(1);
#endif
}
