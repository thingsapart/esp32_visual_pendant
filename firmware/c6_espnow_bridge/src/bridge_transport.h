/**
 * bridge_transport.h  —  Transport abstraction for the C6 ESP-NOW bridge
 *
 * Hides the physical link (UART or SDIO) behind a minimal three-function API.
 * main.cpp always calls bridge_transport_init / bridge_transport_write /
 * bridge_transport_read regardless of which transport is compiled in.
 *
 * Select transport at build time (platformio.ini build_flags):
 *   -D C6_BRIDGE_TRANSPORT_SDIO   →  SDIO slave  (bridge_transport_sdio.cpp)
 *   (default / no flag)           →  UART        (bridge_transport_uart.cpp)
 *
 * The UART and SDIO implementations live in separate .cpp files; only one is
 * compiled per build environment via build_src_filter or simple #ifdef guards
 * inside each file.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the physical transport.
 *
 * Called once from setup().  Performs all GPIO / peripheral configuration.
 * For the UART transport, also writes a plain-text hello banner so any human
 * with a serial monitor sees the bridge is alive; the SDIO transport logs to
 * USB-CDC only (the P4 host performs its own handshake).
 */
void bridge_transport_init(void);

/**
 * @brief Write @p len bytes from @p buf to the host (P4).
 *
 * UART: queued into the hardware FIFO; returns immediately.
 * SDIO: enqueues a DMA buffer; waits for the TX to complete before returning.
 *
 * @return  Number of bytes written on success, negative errno on failure.
 */
int  bridge_transport_write(const uint8_t *buf, size_t len);

/**
 * @brief Read up to @p max_len bytes from the host into @p buf.
 *
 * UART: copies whatever is already in the HW FIFO (non-blocking).
 * SDIO: returns one full packet (one bridge frame) per call with a
 *       @p timeout_ms wait; returns 0 if nothing arrived before the timeout.
 *
 * @param buf         Destination buffer (caller-allocated, ≥ max_len bytes).
 * @param max_len     Maximum bytes to copy.
 * @param timeout_ms  How long to wait for data before returning 0.
 * @return  Bytes copied (0 = nothing ready), negative errno on error.
 */
int  bridge_transport_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms);

/**
 * @brief Append a human-readable description of the active transport into buf.
 *
 * Used by the hello-banner code to report which transport + pins are in use.
 * The string is NUL-terminated and at most (buf_size - 1) chars long.
 *
 * @return  Number of characters written (excluding NUL).
 */
size_t bridge_transport_describe(char *buf, size_t buf_size);

/**
 * @brief Signal that the P4 host ESSL layer is confirmed ready.
 *
 * For the SDIO transport: must be called the first time bridge_transport_read()
 * returns a positive byte count.  Until this is called,
 * bridge_transport_write() returns -1 immediately (no-op) to prevent queueing
 * frames into the SDIO slave send-queue before the host has completed
 * essl_init().  If the slave queues data before essl_init, those DMA
 * descriptors become invisible to the host after the ESSL counter reset and
 * the send-queue semaphore stalls at 0 permanently.
 *
 * For the UART transport this is a no-op (UART is always ready).
 */
void bridge_transport_set_host_ready(void);

/**
 * @brief Returns true once bridge_transport_set_host_ready() has been called.
 * Always returns true for the UART transport.
 */
bool bridge_transport_is_host_ready(void);

#ifdef __cplusplus
}
#endif
