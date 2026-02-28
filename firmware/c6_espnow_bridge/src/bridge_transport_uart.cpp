/**
 * bridge_transport_uart.cpp  —  UART transport implementation
 *
 * Provides the bridge_transport_* functions for the UART path.
 * Compiled only when C6_BRIDGE_TRANSPORT_SDIO is NOT defined.
 *
 * Hardware configuration (build_flags):
 *   C6_BRIDGE_UART_NUM    – Arduino UART number (default 0)
 *   C6_BRIDGE_UART_TX     – C6 GPIO for TX → P4 RX (default 16)
 *   C6_BRIDGE_UART_RX     – C6 GPIO for RX ← P4 TX (default 17)
 *   C6_BRIDGE_UART_BAUD   – Baud rate (default 921600)
 */

#ifndef C6_BRIDGE_TRANSPORT_SDIO   // whole file is a no-op in SDIO builds

#include <Arduino.h>
#include <stdio.h>
#include "bridge_transport.h"

// ---- Default pin / port overrides ----

#ifndef C6_BRIDGE_UART_NUM
#define C6_BRIDGE_UART_NUM   0
#endif
#ifndef C6_BRIDGE_UART_TX
#define C6_BRIDGE_UART_TX    16
#endif
#ifndef C6_BRIDGE_UART_RX
#define C6_BRIDGE_UART_RX    17
#endif
#ifndef C6_BRIDGE_UART_BAUD
#define C6_BRIDGE_UART_BAUD  921600
#endif

// ---- Bridge serial instance ----

#if C6_BRIDGE_UART_NUM == 0
#  define BridgeSerial  Serial0
#else
static HardwareSerial BridgeSerial(C6_BRIDGE_UART_NUM);
#endif

// ---- Public API ----

void bridge_transport_init(void)
{
    BridgeSerial.begin(C6_BRIDGE_UART_BAUD, SERIAL_8N1,
                       C6_BRIDGE_UART_RX, C6_BRIDGE_UART_TX);
}

int bridge_transport_write(const uint8_t *buf, size_t len)
{
    size_t written = BridgeSerial.write(buf, len);
    return (int)written;
}

int bridge_transport_read(uint8_t *buf, size_t max_len, uint32_t /*timeout_ms*/)
{
    // UART path: return however many bytes are already in the HW FIFO.
    // timeout_ms is ignored here — the caller's loop() polls rapidly enough.
    int available = BridgeSerial.available();
    if (available <= 0) return 0;
    size_t to_read = (size_t)available < max_len ? (size_t)available : max_len;
    for (size_t i = 0; i < to_read; i++) {
        buf[i] = (uint8_t)BridgeSerial.read();
    }
    return (int)to_read;
}

size_t bridge_transport_describe(char *buf, size_t buf_size)
{
    return (size_t)snprintf(buf, buf_size,
        "Transport: UART%d, TX=GPIO%d, RX=GPIO%d, %d baud",
        C6_BRIDGE_UART_NUM, C6_BRIDGE_UART_TX,
        C6_BRIDGE_UART_RX, C6_BRIDGE_UART_BAUD);
}

// ---- Allow callers to access BridgeSerial for '?' probe responses ----

/**
 * @brief Write raw bytes to the bridge UART (used by hello-probe handler).
 *
 * Exposed as a weak symbol so main.cpp can call it directly when it needs to
 * echo plain-text back to a human without going through bridge_transport_write.
 */
void bridge_transport_uart_write_raw(const uint8_t *buf, size_t len)
{
    BridgeSerial.write(buf, len);
}

#endif  // !C6_BRIDGE_TRANSPORT_SDIO
