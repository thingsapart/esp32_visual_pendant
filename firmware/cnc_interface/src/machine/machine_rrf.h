#ifndef MACHINE_RRF_H
#define MACHINE_RRF_H

#include "cJSON.h"
#include "config.h"
#include "driver/arduino_serial_wrapper.h"
#include "driver/dwc_http_client_wrapper.h"  // For DWC mode
#include "machine_interface.h"

// ---------------------------------------------------------------------------
// RRF-private I/O types
// These are implementation details of the RRF driver.  They are NOT exposed
// via machine_interface_t.  After each M409 batch the driver converts them
// into the generalized mc_io_channel_t[] via _rrf_rebuild_io_channels().
// ---------------------------------------------------------------------------

typedef struct {
  char  *name;
  float  actual_speed;    ///< Actual PWM 0.0–1.0
  float  requested_speed; ///< Requested PWM 0.0–1.0
  bool   thermostatic;    ///< Managed by temperature controller
  int8_t rpm;             ///< Actual RPM from tacho, or -1
} rrf_fan_t;

typedef enum {
  RRF_HEATER_OFF     = 0,
  RRF_HEATER_STANDBY = 1,
  RRF_HEATER_ACTIVE  = 2,
  RRF_HEATER_FAULT   = 3,
  RRF_HEATER_TUNING  = 4,
} rrf_heater_state_t;

typedef struct {
  char              *name;
  float              current_temp;   ///< Current °C (from associated sensor)
  float              active_temp;    ///< Active setpoint °C
  float              standby_temp;   ///< Standby setpoint °C
  rrf_heater_state_t state;
} rrf_heater_t;

typedef struct {
  char  *name;        ///< Sensor name (from M308 A"name")
  float  temperature; ///< Latest reading °C
  char  *type;        ///< "thermistor", "pt1000", … (strdup-owned)
  int8_t state;       ///< 0=ok, 1=shortcircuit, 2=opencircuit, …
} rrf_temp_sensor_t;

typedef struct {
  char  *name;
  float  value; ///< Normalised 0.0–1.0 (1.0 = high/triggered)
} rrf_gpin_t;

typedef struct {
  char  *name;
  float  pwm; ///< Current PWM duty 0.0–1.0
} rrf_gpout_t;

typedef enum {
  RRF_ENDSTOP_NONE      = 0,
  RRF_ENDSTOP_TRIGGERED = 1,
  RRF_ENDSTOP_NOT_TRIG  = 2,
} rrf_endstop_state_t;

typedef struct {
  char               *name;
  rrf_endstop_state_t high; ///< High-end (max travel) switch state
  rrf_endstop_state_t low;  ///< Low-end  (min travel) switch state
} rrf_endstop_t;

typedef enum {
  RRF_PROBE_NO_READING = 0,
  RRF_PROBE_NOT_TRIG   = 1,
  RRF_PROBE_TRIGGERED  = 2,
} rrf_probe_state_t;

typedef struct {
  char             *name;
  float             value; ///< Normalised ADC value 0.0–1.0
  rrf_probe_state_t state;
} rrf_probe_ex_t;

#ifdef MACHINE_POLL_INTERVAL
#define READ_TIMEOUT_MS (MACHINE_POLL_INTERVAL * 5 / 4)
#else
#define READ_TIMEOUT_MS 100
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Unified RRF/DWC Machine Structure ---

// Forward declare the struct so the function pointers can use it
typedef struct machine_rrf_t machine_rrf_t;

// Define the transport function pointer types
typedef void (*rrf_transport_send_gcode_fn)(machine_rrf_t *self,
                                            const char *gcode);
typedef void (*rrf_transport_poll_state_fn)(machine_rrf_t *self,
                                            uint32_t poll_state);
typedef void (*rrf_transport_set_connected_fn)(machine_rrf_t *self,
                                               bool connect);
typedef void (*rrf_transport_list_files_fn)(machine_rrf_t *self,
                                            const char *path);
typedef void (*rrf_transport_deinit_fn)(machine_rrf_t *self);
typedef void (*rrf_transport_proc_state_resp_fn)(machine_interface_t *self,
                                                 void *data, size_t len);

typedef struct machine_rrf_t {
  machine_interface_t base;

  // Transport-specific state is kept in a union
  union {
    struct {
      serial_handle_t uart;
    } serial;
    struct {
      dwc_http_handle_t http_client;
      char *host;
      char *password;
    } dwc;
  } transport_state;

  // Pointers to the transport implementation functions
  rrf_transport_send_gcode_fn _send_gcode_impl;
  rrf_transport_poll_state_fn _poll_state_impl;
  rrf_transport_set_connected_fn _set_connected_impl;
  rrf_transport_list_files_fn _list_files_impl;
  rrf_transport_deinit_fn _deinit_impl;
  rrf_transport_proc_state_resp_fn _proc_state_resp_impl;

  // --- Common RRF State ---
  bool connected;
  const char *input_sel;
  int input_idx;
  int message_box_last_dismissed_seq;
  int current_tool_idx;

  // --- Connection health tracking ---
  uint32_t last_response_ms;          // millis() timestamp of last successful JSON parse
  int      consecutive_parse_failures; // count of consecutive parse failures

  // --- RRF private I/O state (converted to mc_io_channel_t[] after each batch) ---
  rrf_fan_t         *rrf_fans;         size_t num_rrf_fans;
  rrf_heater_t      *rrf_heaters;      size_t num_rrf_heaters;
  rrf_temp_sensor_t *rrf_temp_sensors; size_t num_rrf_temp_sensors;
  rrf_gpin_t        *rrf_gpins;        size_t num_rrf_gpins;
  rrf_gpout_t       *rrf_gpouts;       size_t num_rrf_gpouts;
  rrf_endstop_t     *rrf_endstops;     size_t num_rrf_endstops;
  rrf_probe_ex_t    *rrf_probes_ex;    size_t num_rrf_probes_ex;

  // --- Poll back-off tracking (serial only) ---
  // Throttles M409 spam when the controller is busy (e.g. executing a probe
  // or long-running macro).  Only 5 bytes (+padding) of RAM.
  uint32_t last_poll_sent_ms;          // millis() when last poll cycle was actually sent
  uint32_t last_disconnect_probe_ms;   // millis() when the last reconnect probe was sent
  uint8_t  unanswered_polls;           // consecutive poll cycles without a response
  uint8_t  poll_skip_target;           // cycles to skip per allowed poll (0=no throttle)
  uint8_t  poll_skip_counter;          // counts toward poll_skip_target; reset on each allowed poll
  uint32_t throttle_start_ms;          // millis() when throttle first engaged (0=not active)
  // When millis() < long_running_end_ms the hub suppresses the disconnect
  // detection so that a probing or tool-change sequence is not interrupted by
  // the hub declaring the machine gone while the CNC is executing the macro.
  // Refreshed on every probe() call; expires automatically after PROBE_GRACE_PERIOD_MS.
  uint32_t long_running_end_ms;        // millis() deadline; 0 = not in long-running op

  // --- Serial CRC-16 line numbering (serial transport only) ---
  // Incremented for every line sent; reset to 1 on (re-)connect.
  // Used when RRF_SERIAL_CRC16 is enabled in machine_rrf.c.
  uint32_t serial_line_number;
} machine_rrf_t;

// --- Function Prototypes ---

// New initializers
machine_rrf_t *machine_rrf_create_serial(int rrf_serial_num, uint16_t sleep_ms,
                                         int tx_pin, int rx_pin);
machine_rrf_t *machine_rrf_create_dwc(const char *host, const char *password,
                                      uint16_t sleep_ms);

machine_rrf_t *machine_rrf_init_serial(machine_rrf_t *self, int rrf_serial_num,
                                       uint16_t sleep_ms, int tx_pin,
                                       int rx_pin);
machine_rrf_t *machine_rrf_init_dwc(machine_rrf_t *self, const char *host,
                                    const char *password, uint16_t sleep_ms);

void machine_rrf_destroy(machine_rrf_t *self);
void machine_rrf_deinit(machine_rrf_t *self);

bool machine_rrf_parse_m409_response(machine_rrf_t *self, cJSON *json_obj);

#ifdef ASYNC_RESPONSE_PROCESSING
#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#else
#include "compat/queue.h"
#endif

bool machine_rrf_setup_response_processing_task(machine_rrf_t *self,
#ifdef ESP32_HW
                                                QueueHandle_t task_event_queue
#else
                                                gcode_queue_t *task_event_queue
#endif
);

#endif

#ifdef __cplusplus
}
#endif

#endif  // MACHINE_RRF_H
