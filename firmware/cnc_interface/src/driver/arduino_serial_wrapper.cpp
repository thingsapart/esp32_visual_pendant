#include "arduino_serial_wrapper.h"

#include <cstring>  // memcpy, memset

#if defined(ESP32_HW)
#include "Arduino.h"
#include "HardwareSerial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "soc/uart_reg.h"  // UART_RX_FILT_REG, UART_GLITCH_FILT_*, UART_GLITCH_FILT_EN
#else
#include <mutex>
#endif

#ifdef RRF_SIM
#include "machine/rrf_machine_sim_stream.h"
#endif

#ifndef ESP32_HW
typedef RRFMachineSimStream Stream;
#endif

#define UI_DEBUG_LOCAL_LEVEL D_ERROR
#include "debug.h"

static const char *TAG = "arduino_serial_wrapper";

// --- Configuration ---
// Buffer given to HardwareSerial::setRxBufferSize().  This is the sole RX
// buffer between the UART FIFO and our line assembly — the secondary SPSC
// ring buffer was removed (IDF/Arduino already provides a thread-safe one).
#define HW_RX_BUF_SIZE     4096
#ifdef APP_PENDANT
// Pendant only uses the standard log serial port; serial_process_input() is
// never called on pendant so line_buffer is never written.  Use a minimal
// size to avoid wasting ~32 KB of internal SRAM on targets like ESP32-P4.
#define MAX_LINE_LENGTH    64
#define MAX_PORTS          1    // Only the log serial port on pendant.
#else
#define MAX_LINE_LENGTH    8192  // M409 d5 for 5-axis + 9 WCS can exceed 4096 B
#define MAX_PORTS          4     // Flat table: 1 log serial + up to 3 machine UARTs
#endif
#define MAX_CALLBACKS      5     // Callbacks per serial port
#define MAX_HW_UARTS       5     // ESP32-S3 has UART0-2; keep some head-room
#define RRF_SIM_UART_NUM   99    // Logical UART number for the RRF sim

// No secondary ring buffer — the IDF/Arduino HardwareSerial internal ring
// buffer (sized via setRxBufferSize) is the only RX buffer.  Line assembly
// reads directly from stream->read() in process_received_data(), which is
// called from serial_process_input() → machine_interface_drain_rx() on every
// machine task loop iteration.  HardwareSerial::read() uses xRingbufferReceive
// internally, which is SMP-safe across cores.

// --- Serial Port Data Structure ---
typedef struct {
  Stream *stream;
  int uart_num;       // Logical UART number (-1 for log Serial, RRF_SIM_UART_NUM for sim)
  bool is_hw_serial;
  bool owns_stream;   // True if we allocated the Stream and must delete it in serial_end
  bool in_use;        // True if this slot is occupied

  char line_buffer[MAX_LINE_LENGTH];
  size_t line_pos;
  bool line_overflow;  // True while discarding bytes of an overflowed line

  // Serialize writes; uncontended except for the CNC UART during burst sends.
#if defined(ESP32_HW)
  SemaphoreHandle_t write_mutex;
#else
  std::mutex *write_mutex;
#endif

  serial_line_callback_t callbacks[MAX_CALLBACKS];
  size_t num_callbacks;
} serial_port_data_t;

// ---------------------------------------------------------------------------
// Global state — flat fixed-size table instead of std::map.
//
// WHY: std::map uses heap allocations and its internal traversal is NOT safe
// to call from the UART event task (a driver task that can run while flash
// cache is disabled, e.g. during NVS writes at boot).  A flat array is
// cache-line friendly and can be searched with a simple loop from any context.
//
// Thread-safety contract:
//  - g_port_table entries are written ONLY at init time (serial_init /
//    add_standard_serial / serial_end), which must not be called concurrently.
//  - After init, individual fields are only written by their owner task
//    (ring buffer producer/consumer, write_mutex holder).
//  - g_isr_port_data[] and g_log_port_data are written once at init and then
//    only read — safe from any task/ISR context.
// ---------------------------------------------------------------------------
static serial_port_data_t g_port_table[MAX_PORTS];
static int g_port_count = 0;  // Number of active (in_use) entries

// Cached pointer for the log serial (uart_num == -1), set by add_standard_serial.
// Used by log_writer_task and default_serial_write to avoid a table scan.
static serial_port_data_t *g_log_port_data = NULL;

// --- Log TX async writer (standard Serial / log port only) ---
// A small FreeRTOS StreamBuffer decouples producers (any task calling LOGI etc.)
// from Serial.write() which can block on HWCDC builds without setTxTimeoutMs(0).
// The dedicated writer task runs at idle+1 priority so it never preempts real work.
#if defined(ESP32_HW)
static StreamBufferHandle_t g_log_tx_sb = NULL;
static TaskHandle_t g_log_writer_task_handle = NULL;
static const size_t LOG_TX_BUFFER_SIZE = 4096;   // 4 KB prevents drops during WiFi-reconnect bursts
static const size_t LOG_TX_COPY_CHUNK  = 256;
static const uint32_t LOG_WRITER_STACK = 2048;
static const UBaseType_t LOG_WRITER_PRIO = tskIDLE_PRIORITY + 1;
static volatile size_t g_log_dropped_count = 0;
static volatile bool g_serial_ended = false;

static void log_writer_task(void *arg) {
  (void)arg;
  uint8_t tmp[LOG_TX_COPY_CHUNK];
  for (;;) {
    size_t received = xStreamBufferReceive(g_log_tx_sb, tmp, sizeof(tmp), portMAX_DELAY);
    if (received == 0) continue;

    if (g_serial_ended) {
      // Clean shutdown — drain and discard any remaining buffered log data.
      __atomic_fetch_add(&g_log_dropped_count, received, __ATOMIC_RELAXED);
      while ((received = xStreamBufferReceive(g_log_tx_sb, tmp, sizeof(tmp), 0)) > 0)
        __atomic_fetch_add(&g_log_dropped_count, received, __ATOMIC_RELAXED);
      continue;
    }
    // Wait until a USB CDC host is connected before writing.  On HWCDC builds
    // setTxTimeoutMs(0) means write() returns 0 immediately when no host is
    // open, so we'd discard the data anyway — instead hold the batch until
    // Serial is ready.  The 4 KB stream buffer is the effective holdover for
    // pre-connection boot log output.
    while (!g_serial_ended && !Serial)
      vTaskDelay(pdMS_TO_TICKS(50));
    if (g_serial_ended) continue;  // shutdown arrived while waiting

    // Write directly using the cached log port pointer — no map lookup needed.
    serial_port_data_t *port = g_log_port_data;
    for (;;) {
      if (!port || !port->stream) break;
      if (port->write_mutex &&
          xSemaphoreTake(port->write_mutex, pdMS_TO_TICKS(50)) != pdPASS) {
        // Mutex timeout — drop this batch (log serial should be uncontended).
        __atomic_fetch_add(&g_log_dropped_count, received, __ATOMIC_RELAXED);
      } else {
        size_t written = port->stream->write(tmp, received);
        if (port->write_mutex) xSemaphoreGive(port->write_mutex);
        if (written < received)
          __atomic_fetch_add(&g_log_dropped_count, received - written, __ATOMIC_RELAXED);
      }
      if (!Serial) break;
      received = xStreamBufferReceive(g_log_tx_sb, tmp, sizeof(tmp), 0);
      if (received == 0) break;
    }
  }
}

static void ensure_log_tx_initialized() {
  if (g_log_tx_sb) return;
  g_log_tx_sb = xStreamBufferCreate(LOG_TX_BUFFER_SIZE, 1);
  if (!g_log_tx_sb) {
    LOGE(TAG, "Failed to create log TX stream buffer");
    return;
  }
  BaseType_t r = xTaskCreate(log_writer_task, "log_writer",
                             LOG_WRITER_STACK / sizeof(StackType_t),
                             NULL, LOG_WRITER_PRIO, &g_log_writer_task_handle);
  if (r != pdPASS) {
    LOGE(TAG, "Failed to create log writer task");
    vStreamBufferDelete(g_log_tx_sb);
    g_log_tx_sb = NULL;
    g_log_writer_task_handle = NULL;
  }
}
#endif

// --- Forward Declarations ---
static void process_received_data(serial_port_data_t *port_data);

// --- Internal Helpers ---

// Find the port entry for a given Stream* handle.
// Linear scan over MAX_PORTS — fast enough (≤8 iterations) from any task.
static serial_port_data_t *find_port_data(serial_handle_t handle) {
  for (int i = 0; i < MAX_PORTS; ++i) {
    if (g_port_table[i].in_use && (serial_handle_t)g_port_table[i].stream == handle)
      return &g_port_table[i];
  }
  return NULL;
}

// Find the port entry for a given logical UART number.
static serial_port_data_t *find_port_by_uart(int uart_num) {
  for (int i = 0; i < MAX_PORTS; ++i) {
    if (g_port_table[i].in_use && g_port_table[i].uart_num == uart_num)
      return &g_port_table[i];
  }
  return NULL;
}

// Allocate a free slot from the flat table.
static serial_port_data_t *alloc_port_slot() {
  for (int i = 0; i < MAX_PORTS; ++i) {
    if (!g_port_table[i].in_use) {
      memset(&g_port_table[i], 0, sizeof(serial_port_data_t));
      g_port_table[i].in_use = true;
      return &g_port_table[i];
    }
  }
  return NULL;
}

// Drain stream->read() directly, assemble lines and dispatch callbacks.
// Works for both HardwareSerial (thread-safe IDF ring buffer internally) and
// plain Stream types (RRF sim, etc.).
static void process_received_data(serial_port_data_t *port_data) {
  if (!port_data || !port_data->stream) return;

  int byte_in;
  while ((byte_in = port_data->stream->read()) != -1) {
    uint8_t byte = (uint8_t)byte_in;
    // Fast-path discard when this line already overflowed: drop bytes until
    // the closing newline arrives, then resume normal line assembly.
    if (port_data->line_overflow) {
      if (byte == '\n') {
        port_data->line_overflow = false;
        port_data->line_pos = 0;
      }
      continue;
    }

    // Detect overflow: line too long for the buffer.  Enter overflow state
    // so the rest of this line is cleanly discarded on subsequent bytes.
    if (port_data->line_pos >= MAX_LINE_LENGTH - 1) {
      LOGE(TAG, "Serial line buffer overflow for UART %d (pos=%u, max=%d) - discarding until next newline",
           port_data->uart_num, (unsigned)port_data->line_pos, MAX_LINE_LENGTH);
      port_data->line_overflow = true;
      port_data->line_pos = 0;
      if (byte == '\n') port_data->line_overflow = false;  // newline ends the overlong line immediately
      continue;
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
#endif  // ESP32_HW

// --- Public C Functions ---
extern "C" {

serial_handle_t serial_init(int uart_num, unsigned long baud,
                            serial_config_t config, int8_t rx_pin,
                            int8_t tx_pin) {
  // Guard double-init: check flat table
  if (find_port_by_uart(uart_num)) {
    LOGW(TAG, "Serial port %d already initialized.", uart_num);
    return (serial_handle_t)find_port_by_uart(uart_num)->stream;
  }

  Stream *serial_stream = NULL;
  bool is_hw = false;
  bool owns_stream = false;

#ifdef RRF_SIM
  if (uart_num == RRF_SIM_UART_NUM) {
    serial_stream = new RRFMachineSimStream(uart_num);
    if (!serial_stream) { LOGE(TAG, "Failed to allocate RRFMachineSimStream"); return NULL; }
    is_hw = false;
    owns_stream = true;
  } else
#endif
  {
#if defined(ESP32_HW)
    if (uart_num == -1 && rx_pin == -1) {
      // Standard Serial (USB-CDC or UART0): already begun by mcu_setup().
      serial_stream = &Serial;
      is_hw = false;
      unsigned long t0 = millis();
      while (!Serial && (millis() - t0 < 500)) delay(10);
      if (!Serial) {
        Serial.setRxBufferSize(HW_RX_BUF_SIZE);
        Serial.begin(baud);
      }
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)
      Serial.setTxTimeoutMs(0);
#else
      Serial.setDebugOutput(false);
#endif
    } else {
      int hw_uart_num = (uart_num < 0) ? 0 : uart_num;
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || (ARDUINO_USB_CDC_ON_BOOT == 0)
      if (hw_uart_num == 0) hw_uart_num = 1;  // Avoid stomping log UART0
#endif
      HardwareSerial *hw = new HardwareSerial(hw_uart_num);
      if (!hw) { LOGE(TAG, "OOM HardwareSerial UART %d", uart_num); return NULL; }
      hw->setRxBufferSize(HW_RX_BUF_SIZE);  // sole RX buffer — no secondary copy
      hw->begin(baud, config, rx_pin, tx_pin);
      hw->setRxFIFOFull(100);
      hw->setRxTimeout(1);

      // Enable the UART hardware RX glitch filter on chips that have the
      // dedicated UART_RX_FILT_REG (ESP32-S3 and newer Espressif SoCs).
      // On the original ESP32 the macro is not defined and this block compiles
      // out entirely — the original ESP32 UART lacks a data-path glitch filter.
      //
      // The filter rejects pulses shorter than GLITCH_FILT APB clock cycles.
      // Default at reset: length field = 8 cycles, but filter DISABLED.
      // At 80 MHz APB, 16 cycles = 200 ns.  A valid UART bit at 115200 baud is
      // ~8.7 µs (43 000× longer), so only genuine EMI spikes are suppressed.
#ifdef UART_RX_FILT_REG
      REG_WRITE(UART_RX_FILT_REG(hw_uart_num),
                (16u << UART_GLITCH_FILT_S) | UART_GLITCH_FILT_EN);
#endif

      serial_stream = hw;
      is_hw = true;
      owns_stream = true;
    }
#else
    LOGE(TAG, "Serial port %d not supported on this platform.", uart_num);
    return NULL;
#endif
  }

  serial_port_data_t *port_data = alloc_port_slot();
  if (!port_data) {
    LOGE(TAG, "Port table full — cannot init UART %d (MAX_PORTS=%d)", uart_num, MAX_PORTS);
    if (owns_stream) delete serial_stream;
    return NULL;
  }

  port_data->stream       = serial_stream;
  port_data->uart_num     = uart_num;
  port_data->is_hw_serial = is_hw;
  port_data->owns_stream  = owns_stream;

#if defined(ESP32_HW)
  port_data->write_mutex = xSemaphoreCreateMutex();
  if (!port_data->write_mutex) {
    LOGE(TAG, "OOM write_mutex UART %d", uart_num);
    port_data->in_use = false;
    if (owns_stream) delete serial_stream;
    return NULL;
  }
#else
  port_data->write_mutex = new std::mutex();
#endif

  serial_handle_t handle = (serial_handle_t)serial_stream;

#if defined(ESP32_HW)
  // No onReceive callback registered — bytes are drained by polling via
  // serial_process_input() → machine_interface_drain_rx() on every task
  // iteration.  HardwareSerial's IDF ring buffer is the only RX buffer.
  LOGI(TAG, "Serial port %d (handle %p) initialized.", uart_num, handle);
  return handle;
}
#endif  // ESP32_HW

void serial_end(serial_handle_t handle) {
  if (!handle) return;

  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data) {
    LOGW(TAG, "serial_end: handle %p not found.", handle);
    return;
  }

  LOGI(TAG, "Ending serial port %d (handle %p)...", port_data->uart_num, handle);

#if defined(ESP32_HW)
  if (port_data->is_hw_serial) {
    HardwareSerial *hw = static_cast<HardwareSerial *>(port_data->stream);
    if (port_data->owns_stream) hw->end();
  }
  if (g_log_port_data == port_data) g_log_port_data = NULL;
#endif

  if (port_data->owns_stream && port_data->stream)
    delete port_data->stream;

#if defined(ESP32_HW)
  if (port_data->write_mutex) { vSemaphoreDelete(port_data->write_mutex); port_data->write_mutex = NULL; }
#else
  if (port_data->write_mutex) { delete port_data->write_mutex; port_data->write_mutex = NULL; }
#endif

  port_data->in_use = false;
  LOGI(TAG, "Serial port ended successfully.");
}

size_t serial_write(serial_handle_t handle, const uint8_t *buffer, size_t size) {
  if (!handle) {
    printf("%.*s", (int)size, (const char *)buffer);
    return size;
  }
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data || !port_data->stream) {
    LOGE(TAG, "serial_write: invalid handle %p", handle);
    return 0;
  }

#if defined(ESP32_HW)
  if (port_data->write_mutex) {
    if (xSemaphoreTake(port_data->write_mutex, pdMS_TO_TICKS(200)) != pdPASS) {
      LOGW(TAG, "serial_write: TX mutex timeout UART %d, dropping %u bytes",
           port_data->uart_num, (unsigned)size);
      return 0;
    }
  }
#else
  if (port_data->write_mutex) port_data->write_mutex->lock();
#endif

  size_t written = port_data->stream->write(buffer, size);

#if defined(ESP32_HW)
  if (port_data->write_mutex) xSemaphoreGive(port_data->write_mutex);
#else
  if (port_data->write_mutex) port_data->write_mutex->unlock();
#endif
  return written;
}

size_t serial_write_atomic(serial_handle_t handle, const uint8_t *buffer, size_t size, bool flush) {
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data || !port_data->stream) return 0;

#if defined(ESP32_HW)
  if (port_data->write_mutex) {
    if (xSemaphoreTake(port_data->write_mutex, pdMS_TO_TICKS(200)) != pdPASS) {
      LOGW(TAG, "serial_write_atomic: TX mutex timeout UART %d, dropping %u bytes",
           port_data->uart_num, (unsigned)size);
      return 0;
    }
  }
#else
  if (port_data->write_mutex) port_data->write_mutex->lock();
#endif

  size_t written = port_data->stream->write(buffer, size);
  if (flush) port_data->stream->flush();

#if defined(ESP32_HW)
  if (port_data->write_mutex) xSemaphoreGive(port_data->write_mutex);
#else
  if (port_data->write_mutex) port_data->write_mutex->unlock();
#endif

  return written;
}

bool serial_flush(serial_handle_t handle) {
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data || !port_data->stream || !handle) {
    return false;
  }

  // Ensure flush is serialized with writes
#if defined(ESP32_HW)
  if (port_data->write_mutex) {
    if (xSemaphoreTake(port_data->write_mutex, pdMS_TO_TICKS(200)) != pdPASS) {
      LOGW(TAG, "serial_flush: mutex timeout UART %d", port_data->uart_num);
      return false;
    }
  }
#else
  if (port_data->write_mutex) port_data->write_mutex->lock();
#endif

  port_data->stream->flush();

#if defined(ESP32_HW)
  if (port_data->write_mutex) xSemaphoreGive(port_data->write_mutex);
#else
  if (port_data->write_mutex) port_data->write_mutex->unlock();
#endif

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

// Registers the global Serial object (already begun by mcu_setup()) into the
// port table so logging and callbacks work without re-initializing the UART.
void add_standard_serial() {
  if (find_port_by_uart(-1)) {
    LOGW(TAG, "Standard serial already registered.");
    return;
  }

#if defined(ESP32_HW)
  serial_port_data_t *port_data = alloc_port_slot();
  if (!port_data) {
    LOGE(TAG, "Port table full — cannot register standard serial");
    return;
  }

  port_data->stream       = &Serial;
  port_data->uart_num     = -1;
  port_data->is_hw_serial = false;
  port_data->owns_stream  = false;

  port_data->write_mutex = xSemaphoreCreateMutex();
  if (!port_data->write_mutex) {
    LOGE(TAG, "OOM write_mutex for standard serial");
    port_data->in_use = false;
    return;
  }

  // Cache for ISR-safe and log_writer_task access (no map lookup needed).
  g_log_port_data = port_data;

  // Start the async log writer task AFTER the port is in the table.
  ensure_log_tx_initialized();
#else
  LOGW(TAG, "add_standard_serial: not supported on this platform.");
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
  serial_port_data_t *port = find_port_by_uart(uart_num);
  return port ? (serial_handle_t)port->stream : NULL;
}

void default_serial_write(const uint8_t *buf, size_t len) {
#ifdef ESP32_HW
  // Do NOT gate on !Serial here.  On HWCDC builds (CDC-on-boot), !Serial means
  // no USB host open yet — but setTxTimeoutMs(0) is already set so writes are
  // non-blocking.  Crucially, the log_writer_task WAITS for Serial before
  // dequeuing, so the 4 KB stream buffer acts as a holdover for pre-connection
  // boot output.  Gating here would silently throw away everything logged
  // before the user opens a terminal.
  if (g_serial_ended) return;
#endif
  // Use the pre-cached log port pointer — avoids any table scan in hot path.
#if defined(ESP32_HW)
  if (g_log_tx_sb) {
    size_t sent = xStreamBufferSend(g_log_tx_sb, buf, len, 0);
    if (sent < len)
      __atomic_fetch_add(&g_log_dropped_count, (size_t)(len - sent), __ATOMIC_RELAXED);
    return;
  }
  // StreamBuffer not yet initialized (very early boot) — drop silently.
  __atomic_fetch_add(&g_log_dropped_count, (size_t)len, __ATOMIC_RELAXED);
#else
  serial_handle_t handle = get_serial_handle(-1);
  if (handle) serial_write(handle, buf, len);
#endif
}

// Drain pending bytes from the serial stream and dispatch complete lines.
// Safe to call from any task; reading is through Stream::read() which uses
// the IDF ring buffer internally (thread-safe across cores on ESP32 family).
void serial_process_input(serial_handle_t handle) {
  serial_port_data_t *port_data = find_port_data(handle);
  if (!port_data) {
    LOGW(TAG, "serial_process_input: Handle %p not found.", handle);
    return;
  }
  process_received_data(port_data);
}

// Returns the cumulative count of log bytes dropped since boot (StreamBuffer
// full or Serial unavailable).  Useful for diagnose log congestion.
uint32_t serial_get_log_drop_count(void) {
#if defined(ESP32_HW)
  return (uint32_t)__atomic_load_n(&g_log_dropped_count, __ATOMIC_RELAXED);
#else
  return 0;
#endif
}

}  // extern "C"

