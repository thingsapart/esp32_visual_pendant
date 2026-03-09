#pragma once
#include <stdint.h>

#define BRIDGE_SOF0             ((uint8_t)0xAB)
#define BRIDGE_SOF1             ((uint8_t)0xCD)

#define BRIDGE_DIR_INCOMING     ((uint8_t)0x01)  // C6 -> P4 Data
#define BRIDGE_DIR_OUTGOING     ((uint8_t)0x02)  // P4 -> C6 Data
#define BRIDGE_DIR_CONTROL      ((uint8_t)0x03)  // Control frame (bidirectional)
#define BRIDGE_DIR_DEBUG        ((uint8_t)0x44)

#define BRIDGE_CTRL_INIT        0x01
#define BRIDGE_CTRL_ADD_PEER    0x02
#define BRIDGE_CTRL_DEL_PEER    0x03
#define BRIDGE_CTRL_TX_STATUS   0x04

#define BRIDGE_PROTOCOL_VERSION 2
#define BRIDGE_DEBUG_TRIGGER    ((uint8_t)'?')
#define BRIDGE_DEBUG_MAC        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

#define BRIDGE_MAC_LEN          6
#define BRIDGE_MAX_PAYLOAD      250

#define BRIDGE_HEADER_SIZE      (1 + 1 + 1 + BRIDGE_MAC_LEN + 2)
#define BRIDGE_FOOTER_SIZE      1
#define BRIDGE_FRAME_OVERHEAD   (BRIDGE_HEADER_SIZE + BRIDGE_FOOTER_SIZE)
#define BRIDGE_MAX_FRAME_SIZE   (BRIDGE_FRAME_OVERHEAD + BRIDGE_MAX_PAYLOAD)

static inline uint8_t bridge_crc8(const uint8_t mac[BRIDGE_MAC_LEN], uint16_t len, const uint8_t *data) {
    uint8_t crc = 0;
    for (int i = 0; i < BRIDGE_MAC_LEN; i++) crc ^= mac[i];
    crc ^= (uint8_t)(len & 0xFF); crc ^= (uint8_t)(len >> 8);
    for (uint16_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}
