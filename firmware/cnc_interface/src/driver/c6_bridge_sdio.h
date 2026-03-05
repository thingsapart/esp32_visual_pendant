/**
 * c6_bridge_sdio.h  —  P4-side SDIO host transport for the C6 ESP-NOW bridge
 *
 * The ESP32-P4 acts as the SDIO host; the companion ESP32-C6 runs the
 * c6_espnow_bridge firmware compiled with -D C6_BRIDGE_TRANSPORT_SDIO and
 * acts as the SDIO slave.
 *
 * This module is the physical transport backend used by remote_comms_wrapper.c
 * when REMOTE_COMMS_C6_SDIO_BRIDGE is defined.  It exposes a three-function
 * API that mirrors the existing UART bridge integration:
 *
 *   c6_sdio_bridge_init()    — configure SDMMC host, enumerate the C6 slave
 *   c6_sdio_bridge_write()   — send one bridge frame to the C6 (→ ESP-NOW TX)
 *   c6_sdio_bridge_read()    — receive one bridge frame from the C6 (ESP-NOW RX →)
 *
 * The frame format is the same bridge_protocol.h format used by the UART link,
 * so the frame parser in remote_comms_wrapper.c is reused unchanged.
 *
 * ─── Pin defaults (override via build_flags) ──────────────────────────────
 *
 * P4 SDMMC host → C6 SDIO slave:
 *
 *   P4 GPIO (C6_SDIO_CLK_PIN)  = SDIO CLK  ←→  C6 GPIO19 (CLK, fixed)
 *   P4 GPIO (C6_SDIO_CMD_PIN)  = SDIO CMD  ←→  C6 GPIO18 (CMD, fixed)
 *   P4 GPIO (C6_SDIO_D0_PIN)   = SDIO D0   ←→  C6 GPIO20 (D0,  fixed)
 *   P4 GPIO (C6_SDIO_D1_PIN)   = SDIO D1   ←→  C6 GPIO21 (D1,  fixed, 4-bit)
 *   P4 GPIO (C6_SDIO_D2_PIN)   = SDIO D2   ←→  C6 GPIO22 (D2,  fixed, 4-bit)
 *   P4 GPIO (C6_SDIO_D3_PIN)   = SDIO D3   ←→  C6 GPIO23 (D3,  fixed, 4-bit)
 *
 * These GPIOs are all connected on-board for JC8012P4A1 and similar Guition
 * P4 + C6 boards.  No external wiring is required.
 *
 * Configurable tuning knobs (build_flags):
 *
 *   C6_SDIO_CLK_PIN        – P4 GPIO for SDIO CLK   (default 43)
 *   C6_SDIO_CMD_PIN        – P4 GPIO for SDIO CMD   (default 44)
 *   C6_SDIO_D0_PIN         – P4 GPIO for SDIO D0    (default 39)
 *   C6_SDIO_D1_PIN         – P4 GPIO for SDIO D1    (default 40)
 *   C6_SDIO_D2_PIN         – P4 GPIO for SDIO D2    (default 41)
 *   C6_SDIO_D3_PIN         – P4 GPIO for SDIO D3    (default 42)
 *   C6_SDIO_BUS_WIDTH      – 1 or 4                 (default 4)
 *   C6_SDIO_HOST_SLOT      – SDMMC slot index        (default SDMMC_HOST_SLOT_1)
 *   C6_SDIO_FREQ_KHZ       – bus frequency in kHz   (default 20000 = 20 MHz)
 *   C6_SDIO_PKT_SIZE       – maximum packet bytes    (default BRIDGE_MAX_FRAME_SIZE)
 *
 * Dependencies (add to idf_component.yml or components/):
 *   esp_serial_slave_link  (Espressif standard component, part of ESP-IDF 5.x)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the SDMMC host and enumerate the C6 SDIO slave.
 *
 * Performs the full SDIO host bring-up:
 *   1. Configure SDMMC host and slot GPIO.
 *   2. Call sdmmc_card_init() to enumerate and configure the slave.
 *   3. Initialise the ESSL (ESP Serial Slave Link) device on function 1.
 *   4. Perform the ESSL handshake (essl_init) so the slave knows the host is
 *      ready to send/receive data.
 *   5. Starts a FreeRTOS receive task that reads incoming frames and delivers
 *      them to the registered callback (set via the recv_cb parameter in
 *      remote_comms_wrapper_init).
 *
 * Must be called before any send or receive operations.
 *
 * @return true on success, false on any fatal error.
 */
bool c6_sdio_bridge_init(void);

/**
 * @brief Send one bridge frame to the C6 (to be transmitted via ESP-NOW).
 *
 * @param buf  Complete bridge_protocol.h frame bytes (including SOF, MAC, CRC).
 * @param len  Frame length in bytes (1–BRIDGE_MAX_FRAME_SIZE).
 * @return  true on success, false if the send failed or timed out.
 */
bool c6_sdio_bridge_write(const uint8_t *buf, size_t len);

/**
 * @brief Receive one bridge frame from the C6 (ESP-NOW packet received by C6).
 *
 * Blocks for up to @p timeout_ms milliseconds.
 *
 * @param buf         Output buffer, must be ≥ C6_SDIO_PKT_SIZE bytes.
 * @param max_len     Size of @p buf.
 * @param out_len     Set to the number of bytes received on success.
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @return  true if a frame was received, false on timeout or error.
 */
bool c6_sdio_bridge_read(uint8_t *buf, size_t max_len,
                          size_t *out_len, uint32_t timeout_ms);

/**
 * @brief Deinitialise the SDMMC host and free resources.
 *
 * Safe to call even if init was never completed.
 */
void c6_sdio_bridge_deinit(void);

/**
 * @brief Query whether the SDIO bridge appears initialized and ready.
 *
 * Returns true if the ESSL device and card structures are present.
 */
bool c6_sdio_bridge_is_ready(void);

#ifdef __cplusplus
}
#endif
