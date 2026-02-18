/**
 * main.cpp  —  ESP32-C6 ESP-NOW ↔ UART Bridge
 *
 * Receives ESP-NOW frames from the wireless hub and forwards them over UART to
 * the ESP32-P4 main chip using the bridge_protocol frame format.
 *
 * Receives UART frames from the P4 and transmits them via ESP-NOW.
 *
 * Build-time configuration (set via platformio.ini build_flags):
 *   C6_BRIDGE_UART_NUM    – Arduino UART number to use for bridge (default 1)
 *   C6_BRIDGE_UART_TX     – C6 GPIO for TX → P4 RX (default 6)
 *   C6_BRIDGE_UART_RX     – C6 GPIO for RX ← P4 TX (default 7)
 *   C6_BRIDGE_UART_BAUD   – Baud rate (default 921600)
 *   C6_BRIDGE_WIFI_CHANNEL – ESP-NOW channel (default 1)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "bridge_protocol.h"

// ---- Default configuration overridable via build_flags ----

#ifndef C6_BRIDGE_UART_NUM
#define C6_BRIDGE_UART_NUM    1
#endif

#ifndef C6_BRIDGE_UART_TX
#define C6_BRIDGE_UART_TX     6
#endif

#ifndef C6_BRIDGE_UART_RX
#define C6_BRIDGE_UART_RX     7
#endif

#ifndef C6_BRIDGE_UART_BAUD
#define C6_BRIDGE_UART_BAUD   921600
#endif

#ifndef C6_BRIDGE_WIFI_CHANNEL
#define C6_BRIDGE_WIFI_CHANNEL 1
#endif

// ---- Globals ----

/**
 * Bridge UART to P4.  With ARDUINO_USB_CDC_ON_BOOT=1, Arduino maps:
 *   Serial  → USB-CDC (console)
 *   Serial0 → UART0   (physical pins, used for bridge)
 * We alias Serial0 so the rest of the code reads naturally.
 */
#if C6_BRIDGE_UART_NUM == 0
#define BridgeSerial Serial0
#else
static HardwareSerial BridgeSerial(C6_BRIDGE_UART_NUM);
#endif

static const uint8_t k_broadcast_mac[BRIDGE_MAC_LEN] =
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- Low-level helpers (used by debug helpers too) --------------------------

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
 * @brief Encode one frame into @p out_buf.
 * @return Total frame length in bytes.
 */
static size_t encode_frame(uint8_t *out_buf, uint8_t dir,
                            const uint8_t *mac, const uint8_t *payload,
                            uint16_t len)
{
    size_t idx = 0;
    out_buf[idx++] = BRIDGE_SOF0;
    out_buf[idx++] = BRIDGE_SOF1;
    out_buf[idx++] = dir;
    memcpy(&out_buf[idx], mac, BRIDGE_MAC_LEN);
    idx += BRIDGE_MAC_LEN;
    out_buf[idx++] = (uint8_t)(len & 0xFF);
    out_buf[idx++] = (uint8_t)(len >> 8);
    memcpy(&out_buf[idx], payload, len);
    idx += len;
    out_buf[idx++] = bridge_crc8(mac, len, payload);
    return idx;
}

// ---- Debug / hello helpers --------------------------------------------------

/**
 * Send a plain-text info dump to the given stream.  Used both for the USB-CDC
 * console (Serial) and as the body of BRIDGE_DIR_DEBUG frames.
 * @p buf / @p buf_size: caller-provided scratch buffer for the string.
 * Returns the number of bytes written into buf (excluding NUL).
 */
static size_t build_hello_text(char *buf, size_t buf_size)
{
    return (size_t)snprintf(buf, buf_size,
        "\r\n"
        "=== C6 ESP-NOW Bridge ===\r\n"
        "Protocol version : %d\r\n"
        "Build            : " __DATE__ " " __TIME__ "\r\n"
        "Bridge UART      : UART%d, TX=GPIO%d, RX=GPIO%d, %d baud\r\n"
        "WiFi channel     : %d\r\n"
        "C6 MAC           : %s\r\n"
        "Status           : ready\r\n"
        "Send '?' for this info again\r\n"
        "=========================\r\n",
        BRIDGE_PROTOCOL_VERSION,
        C6_BRIDGE_UART_NUM, C6_BRIDGE_UART_TX, C6_BRIDGE_UART_RX,
        C6_BRIDGE_UART_BAUD,
        C6_BRIDGE_WIFI_CHANNEL,
        WiFi.macAddress().c_str());
}

/**
 * Encode @p text as a BRIDGE_DIR_DEBUG frame and write it to BridgeSerial.
 * The P4 will parse it, log the payload text, and discard it without calling
 * the application recv callback.
 */
static void send_debug_frame(const char *text)
{
    static const uint8_t debug_mac[BRIDGE_MAC_LEN] = BRIDGE_DEBUG_MAC;
    uint16_t len = (uint16_t)strnlen(text, BRIDGE_MAX_PAYLOAD);
    uint8_t  frame[BRIDGE_MAX_FRAME_SIZE];
    size_t   frame_len = encode_frame(frame, BRIDGE_DIR_DEBUG,
                                      debug_mac,
                                      (const uint8_t *)text, len);
    BridgeSerial.write(frame, frame_len);
}

/**
 * Respond to a '?' probe received on @p stream with a plain-text dump.
 * Also sends the same info as a BRIDGE_DIR_DEBUG frame toward the P4 so it
 * appears in the P4's log too.
 */
static void send_hello(Stream &stream)
{
    char buf[BRIDGE_MAX_PAYLOAD];
    build_hello_text(buf, sizeof(buf));
    stream.print(buf);
    send_debug_frame(buf);  // also notify P4
}

// ---- ESP-NOW → UART ----

static void on_espnow_recv(const esp_now_recv_info_t *info,
                            const uint8_t *data, int len)
{
    if (len <= 0 || len > (int)BRIDGE_MAX_PAYLOAD) {
        Serial.printf("[BRIDGE] RX: invalid len %d, dropping\n", len);
        return;
    }

    // Opportunistically register sender as a peer so we can reply.
    peer_add(info->src_addr);

    uint8_t frame[BRIDGE_MAX_FRAME_SIZE];
    size_t frame_len = encode_frame(frame, BRIDGE_DIR_INCOMING,
                                    info->src_addr, data, (uint16_t)len);
    BridgeSerial.write(frame, frame_len);
}

// ---- UART → ESP-NOW parser ----

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

static parse_state_t s_rx_state   = ST_SOF0;
static uint8_t       s_rx_dir;
static uint8_t       s_rx_mac[BRIDGE_MAC_LEN];
static uint8_t       s_rx_mac_pos;
static uint16_t      s_rx_len;
static uint16_t      s_rx_data_pos;
static uint8_t       s_rx_data[BRIDGE_MAX_PAYLOAD];

static void dispatch_to_espnow(uint8_t *mac, uint8_t *data, uint16_t len)
{
    peer_add(mac);   // guaranteed present before sending
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
                // '?' typed on the bridge wire — respond with plain text
                // (no binary framing).  The P4's parser ignores non-0xAB bytes.
                send_hello(BridgeSerial);
            }
            // All other bytes are silently discarded while idle — normal.
            break;

        case ST_SOF1:
            s_rx_state = (b == BRIDGE_SOF1) ? ST_DIR : ST_SOF0;
            break;

        case ST_DIR:
            s_rx_dir    = b;
            s_rx_mac_pos = 0;
            s_rx_state  = ST_MAC;
            break;

        case ST_MAC:
            s_rx_mac[s_rx_mac_pos++] = b;
            if (s_rx_mac_pos == BRIDGE_MAC_LEN) s_rx_state = ST_LEN_LO;
            break;

        case ST_LEN_LO:
            s_rx_len  = b;
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
                    // Human-readable info frame from P4 — log it, ignore it.
                    s_rx_data[s_rx_len < BRIDGE_MAX_PAYLOAD
                               ? s_rx_len : BRIDGE_MAX_PAYLOAD - 1] = '\0';
                    Serial.printf("[BRIDGE] Debug from P4: %s\n",
                                  (char *)s_rx_data);
                } else {
                    Serial.printf("[BRIDGE] Unexpected DIR=0x%02X from P4\n",
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

// ---- Arduino entry points ----

void setup()
{
    // USB-CDC console (with ARDUINO_USB_CDC_ON_BOOT=1, Serial = USB-CDC).
    Serial.begin(115200);
    Serial.println("[BRIDGE] Booting ESP32-C6 ESP-NOW bridge...");

    // Bridge UART to P4.
    BridgeSerial.begin(C6_BRIDGE_UART_BAUD, SERIAL_8N1,
                       C6_BRIDGE_UART_RX, C6_BRIDGE_UART_TX);

    // Wi-Fi (required by ESP-NOW).
    WiFi.mode(WIFI_STA);
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_set_channel(C6_BRIDGE_WIFI_CHANNEL, second);
    Serial.printf("[BRIDGE] WiFi channel = %d\n", C6_BRIDGE_WIFI_CHANNEL);

    // Print own MAC so users can configure the hub peer list.
    Serial.printf("[BRIDGE] C6 MAC: %s\n", WiFi.macAddress().c_str());

    // ESP-NOW init.
    if (esp_now_init() != ESP_OK) {
        Serial.println("[BRIDGE] FATAL: esp_now_init() failed");
        while (true) delay(1000);
    }

    // Pre-register broadcast peer.
    peer_add(k_broadcast_mac);

    esp_now_register_recv_cb(on_espnow_recv);

    // -- Startup hello ---------------------------------------------------------
    // Send plain-text hello on BOTH outputs so that a human with a serial
    // monitor connected to EITHER port can verify the firmware is running and
    // the right UART is configured.
    //
    //   BridgeSerial  (UART0, GPIO16/17, 921600 baud)
    //     → confirms the wire-jumped bridge UART works end-to-end
    //   Serial        (USB-CDC, 115200 baud)
    //     → confirms the C6 is alive even before the bridge wire is connected
    {
        char buf[BRIDGE_MAX_PAYLOAD];
        build_hello_text(buf, sizeof(buf));
        BridgeSerial.print(buf);  // primary: the wire that will carry live data
        Serial.print(buf);        // secondary: USB-CDC debug console
    }
}

void loop()
{
    // Drain the bridge UART, feeding bytes into the frame parser.
    int avail = BridgeSerial.available();
    while (avail-- > 0) {
        process_byte((uint8_t)BridgeSerial.read());
    }

    // Handle '?' typed into the USB-CDC console (Serial) by a human.
    // Respond with plain text on Serial + a DEBUG frame toward the P4.
    while (Serial.available() > 0) {
        uint8_t ch = (uint8_t)Serial.read();
        if (ch == BRIDGE_DEBUG_TRIGGER) {
            send_hello(Serial);
        }
        // All other console input is silently ignored.
    }

    // Yield briefly so the radio stack gets CPU time.
    delay(1);
}
