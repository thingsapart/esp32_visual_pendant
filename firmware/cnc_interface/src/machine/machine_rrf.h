#ifndef MACHINE_RRF_H
#define MACHINE_RRF_H

#include "cJSON.h"
#include "config.h"
#include "driver/arduino_serial_wrapper.h"
#include "driver/dwc_http_client_wrapper.h"  // For DWC mode
#include "machine_interface.h"

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
