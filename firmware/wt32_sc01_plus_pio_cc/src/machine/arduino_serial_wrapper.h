#ifndef ARDUINO_SERIAL_WRAPPER_H
#define ARDUINO_SERIAL_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

// Define a handle type for the serial port.  This is an opaque pointer.
typedef void* serial_handle_t;

typedef enum serial_config_t {
  CFG_SERIAL_5N1 = 0x8000010,
  CFG_SERIAL_6N1 = 0x8000014,
  CFG_SERIAL_7N1 = 0x8000018,
  CFG_SERIAL_8N1 = 0x800001c,
  CFG_SERIAL_5N2 = 0x8000030,
  CFG_SERIAL_6N2 = 0x8000034,
  CFG_SERIAL_7N2 = 0x8000038,
  CFG_SERIAL_8N2 = 0x800003c,
  CFG_SERIAL_5E1 = 0x8000012,
  CFG_SERIAL_6E1 = 0x8000016,
  CFG_SERIAL_7E1 = 0x800001a,
  CFG_SERIAL_8E1 = 0x800001e,
  CFG_SERIAL_5E2 = 0x8000032,
  CFG_SERIAL_6E2 = 0x8000036,
  CFG_SERIAL_7E2 = 0x800003a,
  CFG_SERIAL_8E2 = 0x800003e,
  CFG_SERIAL_5O1 = 0x8000013,
  CFG_SERIAL_6O1 = 0x8000017,
  CFG_SERIAL_7O1 = 0x800001b,
  CFG_SERIAL_8O1 = 0x800001f,
  CFG_SERIAL_5O2 = 0x8000033,
  CFG_SERIAL_6O2 = 0x8000037,
  CFG_SERIAL_7O2 = 0x800003b,
  CFG_SERIAL_8O2 = 0x800003f
} serial_config_t;

// Function prototypes
serial_handle_t serial_init(uint8_t uart_num, unsigned long baud, serial_config_t config, int8_t rx_pin, int8_t tx_pin);
void serial_end(serial_handle_t handle);
size_t serial_write(serial_handle_t handle, const uint8_t *buffer, size_t size);
int serial_read(serial_handle_t handle);
size_t serial_available(serial_handle_t handle);
size_t serial_available_for_write(serial_handle_t handle);
int serial_peek(serial_handle_t handle);
void serial_flush(serial_handle_t handle);
size_t serial_read_bytes(serial_handle_t handle, uint8_t *buffer, size_t length);

void add_standard_serial();

serial_handle_t get_serial_handle(int uart_num);

// Alloc's a string, caller is responsible for freeing it.
char *serial_read_line(serial_handle_t handle);
size_t serial_read_line_buf(serial_handle_t handle, char *buf, size_t len, long timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // ARDUINO_SERIAL_WRAPPER_H