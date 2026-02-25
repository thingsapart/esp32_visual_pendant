#define UI_DEBUG_LOCAL_LEVEL D_ERROR
#include "debug.h"

#include "config.h"
#include "machine_response_proc_task.h"

#ifdef ASYNC_GCODE_SENDING

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#else
#include "compat/threads.h"
#endif

#include "machine/machine_interface.h"

#define DEFAULT_TASK_STACK_SIZE (1024 * 2)
#define DEFAULT_TASK_PRIORITY \
  (tskIDLE_PRIORITY + 1)  // Priority of the processing task

#define QUEUE_LENGTH 25  // How many gcode commands to queue up before dropping.

static const char *TAG = "MACHINE_SEND_TASK";

typedef struct {
  machine_interface_t *machine;
#ifdef ESP32_HW
  QueueHandle_t queue;
#else
  gcode_queue_t *queue;
#endif
  const char *task_name;
} machine_send_task_args_t;

// Function that will run as the FreeRTOS task calling
// machine_interface_setup_lookp infinitely.
//
// TASK ROLE: Output Pipeline (TX) + Polling (Heartbeat)
// This task decouples the high-speed UI from the low-speed or blocking physical transport.
// It also acts as the "Metronome" for the machine interface, triggering state updates.
//
// 1. DWC/HTTP: Essential. Network calls are blocking.
// 2. Serial: Prevents UI stutter if the UART TX buffer fills up.
// 3. Remote/ESP-NOW: Serializes outgoing packets.
void machine_send_task(void *pvParameters) {
  machine_send_task_args_t *args = (machine_send_task_args_t *)pvParameters;

  machine_interface_t *machine = args->machine;
#ifdef ESP32_HW
  QueueHandle_t queue = args->queue;
#else
  gcode_queue_t *queue = args->queue;
#endif
  const char *task_name = args->task_name;

  free(args);

  bool abort = false;
  char gcode[MAX_GCODE_STR_LEN];

  LOGI(TAG, ">> Running machine send+poll task loop (%s)...", task_name);

  // Poll Timing setup
  TickType_t last_poll_time = xTaskGetTickCount();
  uint32_t procrate_ms = machine->procrate_ms > 0 ? machine->procrate_ms : 100;
  TickType_t poll_interval_ticks = pdMS_TO_TICKS(procrate_ms);
  size_t poll_counter = 0;

  // Defaults if not defined elsewhere
  #ifndef MACHINE_POLL_EVERY_NTH_INTERVAL
  #define MACHINE_POLL_EVERY_NTH_INTERVAL 1
  #endif

  while (!abort) {
    TickType_t now = xTaskGetTickCount();
    TickType_t time_since_poll = now - last_poll_time;
    TickType_t ticks_to_wait = 0;

    // Calculate how long we can block waiting for G-code before we must poll again
    if (time_since_poll < poll_interval_ticks) {
        ticks_to_wait = poll_interval_ticks - time_since_poll;
    } else {
        ticks_to_wait = 0; // Poll is overdue, don't wait
    }

#ifdef ESP32_HW
    // Block waiting for a notification from the queue or timeout
    BaseType_t ret = xQueueReceive(queue, gcode, ticks_to_wait);
    if (ret == pdTRUE)
#else
    // Native simulation doesn't support timeout on pop easily in this shim
    // We assume non-blocking or short sleep for sim
    bool ret = gcode_queue_pop(queue, gcode);
    if (!ret) {
        usleep(10 * 1000); // minimal sleep to prevent CPU hogging in sim
    }
    if (ret)
#endif
    {
      // 1. Handle Outgoing G-Code
      LOGV(TAG, "Sending gcode: %s", gcode);
      machine->_send_gcode(machine, gcode);
    }

    // 2. Handle Polling / Machine Update
    // We check time again because sending gcode might have taken time (blocking IO)
    now = xTaskGetTickCount();
    if ((now - last_poll_time) >= poll_interval_ticks) {
        if (poll_counter++ % MACHINE_POLL_EVERY_NTH_INTERVAL == 0) {
          // Ask the machine interface if it's ok to poll now (backoff may skip
          // polling when no responses have been received).
          if (machine_interface_should_poll(machine)) {
            // This triggers _update_machine_state -> _serial_poll_state_impl -> queues M409
            machine_interface_task_loop_iter(machine);
          } else {
            LOGD(TAG, "Skipping poll due to backoff policy");
          }
        }
        
        // Advance last_poll_time to maintain steady cadence, but don't fall behind if blocked long
        if (now - last_poll_time > (poll_interval_ticks * 2)) {
            last_poll_time = now; // We lagged significantly, reset base time
        } else {
            last_poll_time += poll_interval_ticks;
        }
    }
  }
  LOGI(TAG, "<< Machine Task Loop Ended?");

  // Should never reach here, but good practice to include
  LOGE(TAG, "Machine task unexpectedly exiting");

#ifdef ESP32_HW
  vTaskDelete(NULL);
#endif
  return;
}

bool machine_send_task_run(const char *task_name, machine_interface_t *machine,
#ifdef ESP32_HW
                           TaskHandle_t *machine_task_handle,
                           QueueHandle_t *queue, BaseType_t pinned_core,
                           BaseType_t freertos_task_stack_size,
                           BaseType_t freertos_task_prio
#else
                           thrd_t *machine_task_handle, gcode_queue_t *queue,
                           size_t stack_size
#endif
) {
#ifdef ESP32_HW
  if (freertos_task_prio < 0) {
    freertos_task_prio = DEFAULT_TASK_PRIORITY;
  }
  if (freertos_task_stack_size < 0) {
    freertos_task_stack_size = DEFAULT_TASK_STACK_SIZE;
  }
  // TODO: consider moving to higher up so it is clear the queue is owned by
  // higher level processing
  *queue = xQueueCreate(QUEUE_LENGTH, sizeof(char) * MAX_GCODE_STR_LEN);
#else
  gcode_queue_init(queue);
#endif
  if (*queue == NULL) {
    LOGE(TAG, "Failed to create serial send notification queue (OOM?)!");
    return false;
  }

  machine_send_task_args_t *args =
      (machine_send_task_args_t *)malloc(sizeof(machine_send_task_args_t));
  args->machine = machine;
#ifdef ESP32_HW
  args->queue = *queue;
#else
  args->queue = queue;
#endif
  args->task_name = task_name;

#ifdef ESP32_HW
  BaseType_t task_created =
      xTaskCreatePinnedToCore(machine_send_task,
                              task_name,                 // Task name
                              freertos_task_stack_size,  // Stack depth
                              args,  // Parameter passed to the task (using
                                     // global s_machine_interface instead)
                              freertos_task_prio,   // Task priority
                              machine_task_handle,  // Task handle
                              pinned_core);
#else
  int task_created = thrd_create(machine_task_handle, machine_send_task, args);
#endif
  LOGI(TAG, "Creating task: %s => %p, queue %p, machine %p", task_name,
       *machine_task_handle, *queue, machine);

#ifdef ESP32_HW
  if (task_created != pdPASS)
#else
  if (task_created != thrd_success)
#endif
  {
    LOGE(TAG, "Failed to create machine gcode sending task (%d)!",
         task_created);
#ifdef ESP32_HW
    // TODO: if we move up the create we would want to move up the delete too so
    // we then manage it fully there.
    if (*queue != NULL) vQueueDelete(*queue);  // Clean up queue (guard: xQueueCreate may have failed)
    *queue = NULL;
    if (machine_task_handle != NULL) *machine_task_handle = NULL;  // Ensure handle is NULL on failure
#endif
    free(args);
    return false;
  }

  LOGI(TAG, "Machine gcode sending task started successfully.");
  return true;
}

#ifdef __cplusplus
}
#endif

#endif  // ASYNC_GCODE_SENDING
