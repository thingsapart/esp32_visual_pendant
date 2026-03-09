/**
 * c6_bridge_sdio.h  —  P4-side SDIO host transport for the C6 ESP-NOW bridge
 *                       v2 clean-room redesign
 *
 * Architecture:
 *   A single dedicated FreeRTOS task ("sdio_bus") owns ALL ESSL operations.
 *   It interleaves non-blocking reads and TX-queue drains in a tight loop.
 *   No mutex is needed — no other task ever touches the ESSL handle.
 *
 *   TX: any task calls c6_sdio_bridge_write() which pushes into a FreeRTOS
 *       queue (non-blocking, never stalls the caller).
 *   RX: the bus task reads frames and pushes them into an RX queue that
 *       c6_sdio_bridge_read() drains.
 *
 *   This design eliminates the shared bus mutex that previously caused
 *   deadlocks and priority inversions between the bridged_sdio_rx_task
 *   and the remote_send_task.
 *
 * Pin defaults (override via build_flags):
 *   C6_SDIO_CLK_PIN  (default 43)   C6_SDIO_CMD_PIN  (default 44)
 *   C6_SDIO_D0_PIN   (default 39)   C6_SDIO_D1_PIN   (default 40)
 *   C6_SDIO_D2_PIN   (default 41)   C6_SDIO_D3_PIN   (default 42)
 *   C6_SDIO_BUS_WIDTH (1 or 4, default 4)
 *   C6_SDIO_FREQ_KHZ (default 20000)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise SDMMC host, enumerate C6 slave, perform ESSL handshake,
 * and start the internal bus task.
 * @return true on success.
 */
bool c6_sdio_bridge_init(void);

/**
 * Enqueue a bridge frame for transmission to the C6.  Non-blocking.
 * @return true if queued, false if queue full or not initialised.
 */
bool c6_sdio_bridge_write(const uint8_t *buf, size_t len);

/**
 * Receive one bridge frame from the C6.  Blocks up to timeout_ms.
 * @return true if a frame was received.
 */
bool c6_sdio_bridge_read(uint8_t *buf, size_t max_len,
                          size_t *out_len, uint32_t timeout_ms);

void c6_sdio_bridge_deinit(void);
bool c6_sdio_bridge_is_ready(void);

#ifdef __cplusplus
}
#endif
