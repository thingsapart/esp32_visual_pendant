/**
 * bridge_protocol.h
 *
 * Shared byte-level frame format used on the UART link between the ESP32-C6
 * bridge and the ESP32-P4 main chip.
 *
 * Frame layout (all fields little-endian):
 *
 *  Offset  Size  Field
 *  ------  ----  -----
 *  0       1     SOF0  = 0xAB
 *  1       1     SOF1  = 0xCD
 *  2       1     DIR   — 0x01 = C6→P4 (received from ESP-NOW)
 *                       0x02 = P4→C6 (to send via ESP-NOW)
 *  3       6     MAC   — source MAC (incoming) or destination MAC (outgoing)
 *                        Use broadcast FF:FF:FF:FF:FF:FF to broadcast.
 *  9       2     LEN   — payload byte count (uint16_t LE), 1–250
 *  11      LEN   DATA  — raw ESP-NOW payload bytes
 *  11+LEN  1     CRC8  — XOR of MAC[0..5] ^ LEN_LO ^ LEN_HI ^ DATA[0..LEN-1]
 *
 * Total overhead per frame: 12 bytes.
 * Maximum frame size:       12 + 250 = 262 bytes.
 */

#pragma once
#include <stdint.h>

#define BRIDGE_SOF0             ((uint8_t)0xAB)
#define BRIDGE_SOF1             ((uint8_t)0xCD)

#define BRIDGE_DIR_INCOMING     ((uint8_t)0x01)  /**< C6 → P4: ESP-NOW payload received */
#define BRIDGE_DIR_OUTGOING     ((uint8_t)0x02)  /**< P4 → C6: ESP-NOW payload to send  */
/**
 * Debug / hello frames.  DIR byte = 'D' (0x44) — valid framed message,
 * but neither side passes the payload to the application recv callback.
 * Instead the payload is a human-readable NUL-terminated ASCII string that
 * both sides log to their console.  Ignored gracefully if unexpected.
 */
#define BRIDGE_DIR_DEBUG        ((uint8_t)0x44)

/** Bump this whenever the frame layout changes. */
#define BRIDGE_PROTOCOL_VERSION 1

/**
 * Single-byte interactive probe.  When the parser is idle (waiting for SOF0)
 * and receives this byte it responds with a plain-text info dump — no binary
 * framing — so it can be typed in any serial monitor at the right baud rate.
 * Plain text is silently discarded by the peer's frame parser (non-0xAB bytes
 * never advance the state machine).
 */
#define BRIDGE_DEBUG_TRIGGER    ((uint8_t)'?')

/** Null MAC used as the address field inside debug frames. */
#define BRIDGE_DEBUG_MAC        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

#define BRIDGE_MAC_LEN          6
#define BRIDGE_MAX_PAYLOAD      244   /**< ESP-NOW max data len */

/** Number of bytes before the variable-length payload (SOF+DIR+MAC+LEN). */
#define BRIDGE_HEADER_SIZE      (1 + 1 + 1 + BRIDGE_MAC_LEN + 2)
/** 1 byte CRC after the payload. */
#define BRIDGE_FOOTER_SIZE      1
/** Total per-frame overhead. */
#define BRIDGE_FRAME_OVERHEAD   (BRIDGE_HEADER_SIZE + BRIDGE_FOOTER_SIZE)
/** Maximum total frame size. */
#define BRIDGE_MAX_FRAME_SIZE   (BRIDGE_FRAME_OVERHEAD + BRIDGE_MAX_PAYLOAD)

/**
 * @brief Compute the CRC8 (XOR) checksum for a frame.
 *
 * @param mac     6-byte MAC address field.
 * @param len     Payload length (raw uint16_t value, not yet serialised).
 * @param data    Pointer to payload bytes.
 * @return        CRC byte to append / compare.
 */
static inline uint8_t bridge_crc8(const uint8_t mac[BRIDGE_MAC_LEN],
                                   uint16_t len,
                                   const uint8_t *data)
{
    uint8_t crc = 0;
    for (int i = 0; i < BRIDGE_MAC_LEN; i++) crc ^= mac[i];
    crc ^= (uint8_t)(len & 0xFF);
    crc ^= (uint8_t)(len >> 8);
    for (uint16_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}
