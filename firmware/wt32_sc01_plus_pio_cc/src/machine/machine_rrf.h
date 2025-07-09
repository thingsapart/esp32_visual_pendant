#ifndef MACHINE_RRF_H
#define MACHINE_RRF_H

#include "cJSON.h"

#include "config.h"

#include "driver/arduino_serial_wrapper.h"
#include "machine_interface.h"

#ifdef MACHINE_POLL_INTERVAL
#define READ_TIMEOUT_MS (MACHINE_POLL_INTERVAL * 5 / 4)
#else
#define READ_TIMEOUT_MS 100
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  // --- RRF-Specific Constants ---

  // Mapping of RRF status codes to MachineStatus enum
  // (This could also be a lookup table, but an enum is cleaner)
  typedef enum
  {
    RRF_STATUS_C = MACHINE_STATUS_INITIALIZING,
    RRF_STATUS_F = MACHINE_STATUS_FLASHING_FIRMWARE,
    RRF_STATUS_H = MACHINE_STATUS_EMERGENCY_HALTED,
    RRF_STATUS_O = MACHINE_STATUS_OFF,
    RRF_STATUS_D = MACHINE_STATUS_PAUSED_DEC,
    RRF_STATUS_R = MACHINE_STATUS_PAUSED_RESUME,
    RRF_STATUS_S = MACHINE_STATUS_PAUSED,
    RRF_STATUS_M = MACHINE_STATUS_SIMULATING,
    RRF_STATUS_P = MACHINE_STATUS_RUNNING,
    RRF_STATUS_T = MACHINE_STATUS_TOOL_CHANGING,
    RRF_STATUS_B = MACHINE_STATUS_BUSY,
  } rrf_status_t;
  // --- RRF Machine Structure ---

  typedef struct
  {
    machine_interface_t base; // Inherit from machine_interface_t

    // RRF-Specific Data
    serial_handle_t uart;
    bool connected;
    const char *input_sel;
    int input_idx;
    int message_box_last_dismissed_seq;
    // Add other RRF-specific data here (e.g., network info, job details)
  } machine_rrf_t;

  // --- Function Prototypes ---

  machine_rrf_t *machine_rrf_create(int rrf_serial_num, uint16_t sleep_ms,
                                    int tx_pin, int rx_pin);
  machine_rrf_t *machine_rrf_init(machine_rrf_t *self, int rrf_serial_num,
                                  uint16_t sleep_ms, int tx_pin, int rx_pin);

  void machine_rrf_destroy(machine_rrf_t *self);
  void machine_rrf_deinit(machine_rrf_t *self);
  void machine_rrf_task_loop_iter(
      machine_rrf_t *self); // Override the base class version

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

#endif // MACHINE_RRF_H
