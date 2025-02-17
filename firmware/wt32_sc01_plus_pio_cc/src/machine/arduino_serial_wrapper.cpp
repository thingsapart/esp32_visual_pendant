#include "arduino_serial_wrapper.h"

#include "Arduino.h"
#include "HardwareSerial.h" // Include the Arduino HardwareSerial header
#include <map>

// Use a static std::map to store the HardwareSerial instances.
// This is necessary because HardwareSerial is a C++ class, and we
// need a way to manage instances from C.  The key is the UART number.
static std::map<int, Stream*> serial_instances;

void add_standard_serial() {
    serial_instances[-1] = &Serial;
}

serial_handle_t serial_init(uint8_t uart_num, unsigned long baud, serial_config_t config, int8_t rx_pin, int8_t tx_pin) {
    // Check if an instance already exists for this UART number
    if (serial_instances.count(uart_num) > 0) {
        return serial_instances[uart_num]; // Return existing instance
    }

    // Create a new HardwareSerial instance.  Note: We're using 'new' here,
    // which means we rely on serial_end() to be called to avoid a memory leak.
    HardwareSerial* serial = new HardwareSerial(uart_num);

    // Begin the serial communication
    serial->begin(baud, config, rx_pin, tx_pin);

    // Store the instance in the map
    serial_instances[uart_num] = serial;

    // Return the pointer as an opaque handle
    return (serial_handle_t)serial;
}

void serial_end(serial_handle_t handle) {
    if (!handle) return;

    HardwareSerial* serial = (HardwareSerial*)handle;
    serial->end();

    // Remove the instance from the map and delete it
    for (auto const& [num, instance] : serial_instances) {
        if (instance == serial) {
            serial_instances.erase(num);
            break;
        }
    }
    delete serial;
}

size_t serial_write(serial_handle_t handle, const uint8_t *buffer, size_t size) {
    if (!handle) return 0;
    Stream* serial = (Stream*)handle;
    return serial->write(buffer, size);
}

int serial_read(serial_handle_t handle) {
    if (!handle) return -1;
    Stream* serial = (Stream*)handle;
    return serial->read();
}

size_t serial_available(serial_handle_t handle) {
    if (!handle) return 0;
    Stream* serial = (Stream*)handle;
    return serial->available();
}

size_t serial_available_for_write(serial_handle_t handle) {
    if (!handle) return 0;
    Stream* serial = (Stream*)handle;
    return serial->availableForWrite();
}

int serial_peek(serial_handle_t handle) {
    if (!handle) return -1;
    Stream* serial = (Stream*)handle;
    return serial->peek();
}

void serial_flush(serial_handle_t handle) {
    if (!handle) return;
    Stream* serial = (Stream*)handle;
    serial->flush();
}

size_t serial_read_bytes(serial_handle_t handle, uint8_t *buffer, size_t length) {
    if (!handle) return 0;
    Stream* serial = (Stream*)handle;
    return serial->readBytes(buffer, length);
}

char *serial_read_line(serial_handle_t handle) {
    if (!handle) return NULL;
    Stream* serial = (Stream*)handle;
    String sres = serial->readStringUntil('\n');
    size_t len = sres.length();
    const char *read = sres.c_str();
    char *ret = (char *) malloc(len + 1);
    strncpy(ret, read, len + 1);
    return ret;
}

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) a > b ? a : b

size_t serial_read_line_buf(serial_handle_t handle, char *buf, size_t len, long timeout_ms) {
    if (!handle) return 0;
    Stream* serial = (Stream*)handle;
    serial->setTimeout(timeout_ms);
    String sres = serial->readStringUntil('\n');
    size_t slen = sres.length();
    const char *read = sres.c_str();
    strncpy(buf, read, min(slen + 1, len - 1));
    buf[slen] = '\0';
    serial->setTimeout(0);
    return slen;
}

serial_handle_t get_serial_handle(int uart_num) {
    return serial_instances[uart_num];
}