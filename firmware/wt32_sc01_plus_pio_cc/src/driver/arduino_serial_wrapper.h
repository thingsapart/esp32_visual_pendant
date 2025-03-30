#ifndef ARDUINO_SERIAL_WRAPPER_H
#define ARDUINO_SERIAL_WRAPPER_H

#include <stdbool.h>
#include <stddef.h> // For size_t
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Define a handle type for the serial port. This is an opaque pointer,
// typically pointing to the underlying Stream object (HardwareSerial*,
// RRFMachineSimStream*, etc.).
typedef void *serial_handle_t;

// Callback function type for received lines
// The 'line' buffer is valid only during the callback execution. Copy it if
// needed later. The line includes the newline character if present in the
// original data stream before truncation.
typedef void (*serial_line_callback_t)(serial_handle_t handle, const char *line,
                                       size_t len);

// Configuration enum (remains the same)
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

/**
 * @brief Initializes a hardware serial port (UART).
 *
 * @param uart_num The UART number (e.g., 0, 1, 2 for ESP32). Use -1 for the
 * standard Serial (USB/UART0).
 * @param baud Baud rate.
 * @param config Configuration (e.g., CFG_SERIAL_8N1).
 * @param rx_pin RX pin number.
 * @param tx_pin TX pin number.
 * @return A handle to the initialized serial port, or NULL on failure.
 */
serial_handle_t serial_init(int uart_num, unsigned long baud,
                            serial_config_t config, int8_t rx_pin,
                            int8_t tx_pin);

/**
 * @brief Ends communication on a serial port and releases resources.
 *
 * @param handle The handle of the serial port to close.
 */
void serial_end(serial_handle_t handle);

/**
 * @brief Writes data to the serial port.
 *
 * @param handle The handle of the serial port.
 * @param buffer Pointer to the data buffer.
 * @param size Number of bytes to write.
 * @return Number of bytes actually written.
 */
size_t serial_write(serial_handle_t handle, const uint8_t *buffer, size_t size);

/**
 * @brief Registers a callback function to be called when a full line (ending in
 * '\n') is received. Multiple callbacks can be registered for the same handle.
 *
 * @param handle The handle of the serial port.
 * @param callback The function pointer to the callback.
 * @return true if registration was successful, false otherwise (e.g., max
 * callbacks reached).
 */
bool serial_register_line_callback(serial_handle_t handle,
                                   serial_line_callback_t callback);

/**
 * @brief Unregisters a previously registered line callback function.
 *
 * @param handle The handle of the serial port.
 * @param callback The function pointer to the callback to remove.
 * @return true if the callback was found and removed, false otherwise.
 */
bool serial_unregister_line_callback(serial_handle_t handle,
                                     serial_line_callback_t callback);

/**
 * @brief Initializes the standard serial port (Serial/UART0) without returning
 * a handle. Use get_serial_handle(-1) to get its handle later if needed.
 *
 * @param baud Baud rate.
 * @param config Configuration.
 * @param rx_pin RX pin number.
 * @param tx_pin TX pin number.
 */
void init_standard_serial(unsigned long baud, serial_config_t config,
                          int8_t rx_pin, int8_t tx_pin);

/**
 * @brief Adds the standard serial port (Serial/UART0) to the managed list
 * without initializing pins/baud. Assumes Serial.begin() might be called
 * elsewhere. Callbacks can still be registered. Use get_serial_handle(-1) to
 * get its handle.
 */
void add_standard_serial();

#ifdef RRF_SIM
/**
 * @brief Adds a simulated RRF serial port for testing.
 * Use get_serial_handle(RRF_SIM_UART_NUM) to get its handle.
 * @return The logical UART number assigned to the simulator (e.g., 99).
 */
int add_rrf_sim_serial();
#endif

/**
 * @brief Retrieves the handle for a serial port given its UART number.
 *
 * @param uart_num The UART number (-1 for standard Serial, RRF_SIM_UART_NUM for
 * simulator).
 * @return The serial handle, or NULL if not found/initialized.
 */
serial_handle_t get_serial_handle(int uart_num);

/**
 * @brief Convenience function to write to the default serial port (handle for
 * uart_num -1).
 *
 * @param buf Buffer to write.
 * @param len Length of buffer.
 */
void default_serial_write(const uint8_t *buf, size_t len);

/**
 * @brief Processes incoming data from the ring buffer for non-interrupt driven
 * ports (like RRF Sim). This should be called periodically in the main loop or
 * a dedicated task for such ports. For ESP32 HardwareSerial, processing happens
 * automatically via onReceive.
 *
 * @param handle The handle of the serial port to process.
 */
void serial_process_input(serial_handle_t handle);

// The extern "C" block opened at the top is closed here
#ifdef __cplusplus
}
#endif // __cplusplus

#endif // ARDUINO_SERIAL_WRAPPER_H
