#include "arduino_serial_wrapper.h"

#include <cstdlib>  // For malloc, free (if used, prefer new/delete in C++)
#include <cstring>  // For memcpy, memset
#include <map>
#include <vector>

#if defined(ESP32_HW)

#include "Arduino.h"
#include "HardwareSerial.h"
#else
// Provide dummy types/stubs for non-ESP32 builds if needed
// Or include the RRF Sim stream header
#include "machine/rrf_machine_sim_stream.h"
typedef RRFMachineSimStream Stream;  // Use the simulator stream
#endif

#include "debug.h"

static const char *TAG = "arduino_serial_wrapper";

// --- Configuration ---
#define RING_BUFFER_SIZE \
  4096  // Size of the ring buffer for incoming serial data
#define MAX_LINE_LENGTH \
  (256 * 6)  // Maximum length of a line to buffer before calling callback
#define MAX_CALLBACKS 5      // Maximum number of callbacks per serial port
#define RRF_SIM_UART_NUM 99  // Logical UART number for the RRF simulator

// --- Ring Buffer Implementation ---
typedef struct {
  uint8_t *buffer;
  size_t head;
  size_t tail;
  size_t size;
  size_t count;
} ring_buffer_t;

static bool rb_init(ring_buffer_t *rb, size_t size) {
  rb->buffer = (uint8_t *)malloc(size);
  if (!rb->buffer) return false;
  rb->size = size;
  rb->head = 0;
  rb->tail = 0;
  rb->count = 0;
  return true;
}

static void rb_free(ring_buffer_t *rb) {
  if (rb->buffer) {
    free(rb->buffer);
    rb->buffer = NULL;
  }
}

static bool rb_is_full(const ring_buffer_t *rb) {
  return rb->count == rb->size;
}

static bool rb_is_empty(const ring_buffer_t *rb) { return rb->count == 0; }

static bool rb_push(ring_buffer_t *rb, uint8_t data) {
  if (rb_is_full(rb)) return false;  // Buffer full
  rb->buffer[rb->head] = data;
  rb->head = (rb->head + 1) % rb->size;
  rb->count++;
  return true;
}

static bool rb_pop(ring_buffer_t *rb, uint8_t *data) {
  if (rb_is_empty(rb)) return false;  // Buffer empty
  *data = rb->buffer[rb->tail];
  rb->tail = (rb->tail + 1) % rb->size;
  rb->count--;
  return true;
}

// --- Serial Port Data Structure ---
typedef struct {
  Stream *stream;  // Underlying Arduino Stream object (HardwareSerial*,
                   // RRFMachineSimStream*, etc.)
  int uart_num;    // Original UART number (-1 for Serial, RRF_SIM_UART_NUM for
                   // sim)
  bool is_hw_serial;  // Flag to indicate if it's a HardwareSerial instance
  bool owns_stream;   // Flag to indicate if we need to delete the stream object
                      // in serial_end

  ring_buffer_t rx_buffer;
  char line_buffer[MAX_LINE_LENGTH];
  size_t line_pos;

  serial_line_callback_t callbacks[MAX_CALLBACKS];
  size_t num_callbacks;
} serial_port_data_t;

// --- Global State ---
// Map the serial handle (Stream*) to its associated data
static std::map<serial_handle_t, serial_port_data_t *> g_serial_ports;
// Map the UART number back to the handle for easy lookup
static std::map<int, serial_handle_t> g_uart_num_to_handle;
// ISR-safe lookup table for port data based on UART number.
#define MAX_HW_UARTS 5 // ESP32-S3 has 3, but this provides a safe upper bound
static serial_port_data_t* g_isr_port_data[MAX_HW_UARTS] = {NULL};

// --- Forward Declarations ---
static void process_received_data(serial_port_data_t *port_data);
#if defined(ESP32_HW)
static void onReceiveGeneric(
    void *arg);  // Remove IRAM_ATTR from forward declaration
#endif

// --- Internal Helper Functions ---

// Finds the port data associated with a handle
static serial_port_data_t *find_port_data(serial_handle_t handle) {
  auto it = g_serial_ports.find(handle);
  if (it != g_serial_ports.end()) {
    return it->second;
  }
  return NULL;
}

// Processes data from the ring buffer, assembling lines and calling callbacks
static void process_received_data(serial_port_data_t *port_data) {
  if (!port_data) return;
  if (rb_is_empty(&port_data->rx_buffer)) return;

  uint8_t byte;
  LOGD(TAG, "Processing %d bytes from serial ring buffer for UART %d.", port_data->rx_buffer.count, port_data->uart_num);
  while (rb_pop(&port_data->rx_buffer, &byte)) {
    // Check for line buffer overflow before adding the character
    if (port_data->line_pos >= MAX_LINE_LENGTH - 1) {
      // Line too long, discard the current line buffer content and start over
      LOGW(TAG, "Serial line buffer overflow for UART %d", port_data->uart_num);
      port_data->line_pos = 0;
      // Optionally, add the current byte if it's not part of the overflowed
      // line This depends on desired behavior: discard whole long line vs.
      // split it. Let's discard the partial long line for simplicity.
      continue;  // Skip processing this byte as part of a new line yet
    }

    port_data->line_buffer[port_data->line_pos++] = (char)byte;

    if (byte == '\n') {
      port_data->line_buffer[port_data->line_pos] = '\0';  // Null-terminate

      // Call registered callbacks
      for (size_t i = 0; i < port_data->num_callbacks; ++i) {
        if (port_data->callbacks[i]) {
          port_data->callbacks[i]((serial_handle_t)port_data->stream,
                                  port_data->line_buffer, port_data->line_pos);
        }
      }
      port_data->line_pos = 0;  // Reset line buffer
    }
  }
}

#if defined(ESP32_HW)
// Generic onReceive callback for HardwareSerial
// IMPORTANT: This runs in ISR context on ESP32. Keep it short and fast.
// Avoid blocking calls, memory allocation, or complex logic.
static void onReceiveGeneric(void *arg) {  // Removed IRAM_ATTR
  serial_port_data_t *port_data = (serial_port_data_t *)arg;
  if (!port_data || !port_data->is_hw_serial) return;  // Should not happen

  HardwareSerial *hw_serial = static_cast<HardwareSerial *>(port_data->stream);
  LOGV(TAG, "=> RECV/READ hw serial %p", hw_serial);

  // Read all available bytes from HW FIFO into the ring buffer
  while (hw_serial->available()) {
    uint8_t byte = hw_serial->read();
    // Try to push to ring buffer. If it fails (full), data is lost.
    LOGV(TAG, "=> RECV/READ - COUNT: %d", byte);
    if (!rb_push(&port_data->rx_buffer, byte)) {
      // Ring buffer overflow handling (optional: log, count errors, etc.)
      // For now, we just lose the byte.
      // Consider increasing RING_BUFFER_SIZE if this happens frequently.
      // Note: Logging directly from ISR is generally unsafe.
    }
  }
  // Data is now in the ring buffer. Processing (line detection, callbacks)
  // should happen outside the ISR, e.g., in serial_process_input() called from
  // loop/task, or triggered by a semaphore/queue from the ISR if immediate
  // processing is needed.
  // --- Let's process directly here for simplicity, BUT BEWARE OF ISR
  // CONSTRAINTS --- This is generally okay if callbacks are short and
  // non-blocking. If callbacks are complex, use a task/queue mechanism.
  //
#ifdef PROCESS_HW_SERIAL_IN_ISR
   process_received_data(port_data);
#endif
}

#define ONRECV(N)                                     \
   void onReceive##N(void) {                          \
     if (N < MAX_HW_UARTS) {                          \
       onReceiveGeneric(g_isr_port_data[N]);          \
     }                                                \
  }

void onReceiveM1(void) {
  serial_handle_t handle = get_serial_handle(-1);
  serial_port_data_t *arg = find_port_data(handle);
  onReceiveGeneric(arg);
}
ONRECV(0)
ONRECV(1)
ONRECV(2)
ONRECV(3)
ONRECV(4)

#endif  // ESP32_HW

// --- Public C Functions ---
extern "C" {

serial_handle_t serial_init(int uart_num, unsigned long baud,
                            serial_config_t config, int8_t rx_pin,
                            int8_t tx_pin) {
  Stream *serial_stream = NULL;
  bool is_hw = false;
  bool owns_stream = false;  // Do we need to delete the stream later?

  // Check if already initialized
  if (g_uart_num_to_handle.count(uart_num)) {
    LOGW(TAG, "Serial port %d already initialized.", uart_num);
    return g_uart_num_to_handle[uart_num];
  }

#if defined(ESP32_HW)
  if (uart_num == -1 && rx_pin == -1) {  // Standard Serial (usually UART0)
    serial_stream = &Serial;
    is_hw = false;  // Serial is typically not HardwareSerial in this wrapper's logic
    
    // Check if Serial is connected. For ESP32-S3 CDC, this avoids hanging
    // if the serial monitor isn't open. We wait for a short period.
    unsigned long start_time = millis();
    while (!Serial && (millis() - start_time < 500)) {
        delay(10); // Wait up to 500ms
    }

    // Now, check if Serial was already begun externally or if it became available.
    // If it's still not available, we can begin it, but it might not be usable.
    if (!Serial) { 
        LOGI(TAG, "Standard Serial not connected, initializing anyway.");
        Serial.setRxBufferSize(RING_BUFFER_SIZE / 2);
        Serial.begin(baud);
    } else {
        LOGI(TAG, "Standard Serial already initialized or connected.");
    }
  } else {  // HardwareSerial UART 1 or 2 etc.
    HardwareSerial *hw_serial = new HardwareSerial(uart_num < 0 ? 0 : uart_num);
    if (!hw_serial) {
      LOGE(TAG, "Failed to allocate HardwareSerial for UART %d", uart_num);
      return NULL;
    }
    hw_serial->setRxBufferSize(RING_BUFFER_SIZE / 2);  // Set buffer size
    hw_serial->begin(baud, config, rx_pin, tx_pin);
    // Set FIFO threshold to trigger onReceive frequently (e.g., for every byte)
    // This depends on the ESP-IDF version / Arduino core.
    hw_serial->setRxFIFOFull(100);  // Example, check specific API for your core
    hw_serial->setRxTimeout(1);
    serial_stream = hw_serial;
    is_hw = true;
    owns_stream = true;  // We created it, we own it.
    LOGI(TAG, "HardwareSerial UART %d initialized.", uart_num);
  }
#else  // Non-ESP32 (e.g., RRF Sim)
  if (uart_num == RRF_SIM_UART_NUM) {
    serial_stream = new RRFMachineSimStream(uart_num);
    if (!serial_stream) {
      _d(0, "Failed to allocate RRFMachineSimStream");
      return NULL;
    }
    is_hw = false;
    owns_stream = true;
    LOGV(TAG, "RRFMachineSimStream initialized.");
  } else {
    LOGE(TAG, "Serial port %d not supported on this platform.", uart_num);
    return NULL;  // Not supported
  }
#endif

  // Allocate and initialize port data structure
  serial_port_data_t *port_data = new serial_port_data_t;
  if (!port_data) {
    LOGE(TAG, "Failed to allocate serial_port_data_t for UART %d", uart_num);
    if (owns_stream && serial_stream) delete serial_stream;
    return NULL;
  }
  memset(port_data, 0, sizeof(serial_port_data_t));  // Zero out the struct

  if (!rb_init(&port_data->rx_buffer, RING_BUFFER_SIZE)) {
    LOGE(TAG, "Failed to allocate ring buffer for UART %d", uart_num);
    delete port_data;
    if (owns_stream && serial_stream) delete serial_stream;
    return NULL;
  }

  port_data->stream = serial_stream;
  port_data->uart_num = uart_num;
  port_data->is_hw_serial = is_hw;
  port_data->owns_stream = owns_stream;
  port_data->line_pos = 0;
  port_data->num_callbacks = 0;

  serial_handle_t handle = (serial_handle_t)serial_stream;
  g_serial_ports[handle] = port_data;
  g_uart_num_to_handle[uart_num] = handle;

#if defined(ESP32_HW)
  // Register the onReceive callback if it's a HardwareSerial
  if (is_hw) {
    HardwareSerial *hw_serial = static_cast<HardwareSerial *>(serial_stream);
    // Pass port_data as the argument to the callback
    if (uart_num >= 0 && uart_num < MAX_HW_UARTS) {
        g_isr_port_data[uart_num] = port_data;
    }

    if (uart_num == 0) {
      hw_serial->onReceive(onReceive0, false);
      LOGI(TAG, "OnReceive 0 %p", port_data);
    } else if (uart_num == 1) {
      hw_serial->onReceive(onReceive1, false);
      LOGI(TAG, "OnReceive 1 %p", port_data);
    } else if (uart_num == 2) {
      hw_serial->onReceive(onReceive2, false);
      LOGI(TAG, "OnReceive 2 %p", port_data);
    } else if (uart_num == 3) {
      hw_serial->onReceive(onReceive3, false);
      LOGI(TAG, "OnReceive 3 %p", port_data);
    } else if (uart_num == 4) {
      hw_serial->onReceive(onReceive4, false);
      LOGI(TAG, "OnReceive 4 %p", port_data);
    }
    LOGV(TAG, "onReceive callback registered for UART \"%d\"...", uart_num);
  }
#endif

  LOGI(TAG, "Serial port %d (handle %p) successfully initialized.", uart_num,
       handle);
  return handle;
}

void serial_end(serial_handle_t handle) {
  if (!handle) return;

  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data) {
    LOGI(TAG, "serial_end: Handle %p not found.", handle);
    return;
  }

  LOGI(TAG, "Ending serial port %d (handle %p)...", port_data->uart_num,
       handle);

  // Unregister callback and stop hardware serial if applicable
#if defined(ESP32_HW)
  if (port_data->is_hw_serial) {
    HardwareSerial *hw_serial =
        static_cast<HardwareSerial *>(port_data->stream);
    hw_serial->onReceive(NULL);  // Unregister callback
    // Only call end() if we own the stream (i.e., not standard Serial unless we
    // explicitly initialized it) or if it's a dynamically created
    // HardwareSerial instance. Ending standard Serial might break other things
    // (like USB CDC). Be careful.
    if (port_data->owns_stream) {
      LOGI(TAG, "Calling end() for owned HardwareSerial UART %d",
           port_data->uart_num);
      hw_serial->end();
    }
  }
#endif

  // Clean up internal data structures
  rb_free(&port_data->rx_buffer);

  // Remove from maps
  g_serial_ports.erase(handle);
  g_uart_num_to_handle.erase(port_data->uart_num);

  // Clear from ISR-safe table
  if (port_data->uart_num >= 0 && port_data->uart_num < MAX_HW_UARTS) {
      g_isr_port_data[port_data->uart_num] = NULL;
  }

  // Delete the stream object *if* we allocated it
  if (port_data->owns_stream && port_data->stream) {
    LOGI(TAG, "Deleting owned stream object for UART %d", port_data->uart_num);
    delete port_data
        ->stream;  // This deletes HardwareSerial or RRFMachineSimStream
  }

  // Delete the port data struct itself
  delete port_data;

  LOGI(TAG, "Serial port ended successfully.");
}

size_t serial_write(serial_handle_t handle, const uint8_t *buffer,
                    size_t size) {
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data || !port_data->stream) {
    // Special case: if handle is NULL, maybe write to default stdout?
    if (!handle) {
      // This matches the previous behavior for NULL handle in non-ESP32 write
      printf("%.*s", (int)size, (const char *)buffer);
      return size;
    }
    LOGE(TAG, "serial_write: Invalid handle %p", handle);
    return 0;
  }
  Serial.print("[I][SERIAL]TX UART");
  Serial.print(port_data->uart_num);
  Serial.print(", size ");
  Serial.print(size);
  Serial.print((char*)buffer); //
  return port_data->stream->write(buffer, size);
}

bool serial_flush(serial_handle_t handle) {
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data || !port_data->stream || !handle) {
    return false;
  }

  port_data->stream->flush();

  return true;
}

bool serial_register_line_callback(serial_handle_t handle,
                                   serial_line_callback_t callback) {
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data || !callback) {
    LOGE(TAG, "serial_register_line_callback: Invalid handle or callback.");
    return false;
  }

  if (port_data->num_callbacks >= MAX_CALLBACKS) {
    LOGW(TAG,
         "serial_register_line_callback: Max callbacks reached for handle %p.",
         handle);
    return false;
  }

  // Check if already registered
  for (size_t i = 0; i < port_data->num_callbacks; ++i) {
    if (port_data->callbacks[i] == callback) {
      LOGW(TAG, "serial_register_line_callback: Callback already registered.");
      return true;  // Already registered
    }
  }

  port_data->callbacks[port_data->num_callbacks++] = callback;
  LOGI(TAG, "Callback registered for handle %p (total %d)", handle,
       port_data->num_callbacks);
  return true;
}

bool serial_unregister_line_callback(serial_handle_t handle,
                                     serial_line_callback_t callback) {
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data || !callback) {
    LOGW(TAG, "serial_unregister_line_callback: Invalid handle or callback.");
    return false;
  }

  for (size_t i = 0; i < port_data->num_callbacks; ++i) {
    if (port_data->callbacks[i] == callback) {
      // Shift remaining callbacks down
      for (size_t j = i; j < port_data->num_callbacks - 1; ++j) {
        port_data->callbacks[j] = port_data->callbacks[j + 1];
      }
      port_data->num_callbacks--;
      port_data->callbacks[port_data->num_callbacks] = NULL;  // Clear last slot
      LOGI(TAG, "Callback unregistered for handle %p (total %d)", handle,
           port_data->num_callbacks);
      return true;
    }
  }

  LOGW(TAG,
       "serial_unregister_line_callback: Callback not found for handle %p.",
       handle);
  return false;
}

// Initializes standard serial without specific pins (assumes defaults or
// already set)
void add_standard_serial() {
  int uart_num = -1;
  if (g_uart_num_to_handle.count(uart_num)) {
    LOGW(TAG, "Standard serial already added/initialized.");
    return;  // Already managed
  }

#if defined(ESP32_HW)
  Stream *serial_stream = &Serial;
  serial_handle_t handle = (serial_handle_t)serial_stream;

  // Assume Serial might be initialized externally, just manage it
  serial_port_data_t *port_data = new serial_port_data_t;
  if (!port_data) {
    LOGE(TAG, "Failed to allocate serial_port_data_t for standard serial");
    return;
  }
  memset(port_data, 0, sizeof(serial_port_data_t));

  if (!rb_init(&port_data->rx_buffer, RING_BUFFER_SIZE)) {
      LOGE(TAG,  "Failed to allocate ring buffer for standard serial");
      delete port_data;
      return;
  }

  port_data->stream = serial_stream;
  port_data->uart_num = uart_num;
  port_data->is_hw_serial = false;  // Assume standard Serial is not HardwareSerial
  port_data->owns_stream = false;   // We don't own the global Serial object
  port_data->line_pos = 0;
  port_data->num_callbacks = 0;

  g_serial_ports[handle] = port_data;
  g_uart_num_to_handle[uart_num] = handle;

  // Populate the ISR-safe lookup table if it's a valid hardware UART
  if (uart_num >= 0 && uart_num < MAX_HW_UARTS) {
      g_isr_port_data[uart_num] = port_data;
  }

  // Register the onReceive callback
  // HardwareSerial* hw_serial = static_cast<HardwareSerial*>(serial_stream);
  // hw_serial->onReceive(onReceiveM1, port_data);
  // LOGI(TAG, "Standard Serial added for management. onReceive callback
  // registered.");

#else
  LOGW(TAG, "add_standard_serial: Not supported on this platform.");
#endif
}

// Initializes standard serial with specific parameters
void init_standard_serial(unsigned long baud, serial_config_t config,
                          int8_t rx_pin, int8_t tx_pin) {
  // Just call serial_init with uart_num -1
  serial_init(-1, baud, config, rx_pin, tx_pin);
}

#ifdef RRF_SIM
int add_rrf_sim_serial() {
  // Just call serial_init with the simulator UART number
  serial_handle_t handle =
      serial_init(RRF_SIM_UART_NUM, 0, CFG_SERIAL_8N1, -1,
                  -1);  // Baud/config/pins irrelevant for sim
  return handle ? RRF_SIM_UART_NUM
                : -1;  // Return num on success, -1 on failure
}
#endif

serial_handle_t get_serial_handle(int uart_num) {
  auto it = g_uart_num_to_handle.find(uart_num);
  if (it != g_uart_num_to_handle.end()) {
    return it->second;
  }
#ifdef ESP32_HW
  if (Serial) {
      String msg = "get_serial_handle: Handle for UART not found: " + String(uart_num);
      Serial.println(msg);
  }
#endif
  return NULL;
}

void default_serial_write(const uint8_t *buf, size_t len) {
  serial_handle_t handle = get_serial_handle(-1);
  if (handle) {
    serial_write(handle, buf, len);
    serial_flush(handle);
  } else {
    // Fallback if standard serial wasn't initialized/added
    // printf("%.*s", (int)len, (const char *)buf);
#ifdef ESP32_HW
    if (Serial) {
        Serial.write("Unable to find standard serial");
        Serial.flush();
    }
#endif
    //_d(0, "default_serial_write: Standard serial handle not found, writing to
    //"
    //      "printf.");
  }
}

// Process input manually (needed for non-onReceive streams like RRF Sim)
void serial_process_input(serial_handle_t handle) {
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data) {
    LOGW(TAG, "serial_process_input: Handle %p not found.", handle);
    return;
  }

#ifdef PROCESS_HW_SERIAL_IN_ISR
  // For HardwareSerial on ESP32, data is processed in onReceive.
  // Only process manually if it's not a HW serial port using onReceive.
  if (port_data->is_hw_serial) {
    // Optional: Could add a check here if onReceive failed or buffer got full,
    // and try processing again, but generally not needed if onReceive works.
    return;
  }
#endif

  // Manually read from stream into ring buffer for non-HW/non-onReceive ports
  if (port_data->stream) {
    while (port_data->stream->available()) {
      int byte_int = port_data->stream->read();
      if (byte_int != -1) {
        if (!rb_push(&port_data->rx_buffer, (uint8_t)byte_int)) {
          LOGE(TAG, "serial_process_input: Ring buffer full for UART %d",
               port_data->uart_num);
          break;  // Stop reading if buffer is full
        }
      } else {
        break;  // No more data or error
      }
    }
  }

  // Process whatever is in the ring buffer now
  process_received_data(port_data);
}

}  // extern "C"

