
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

#include "debug.h"
#include "machine/machine_interface.h"

#define TASK_STACK_SIZE 1024 * 6
#define TASK_PRIORITY (tskIDLE_PRIORITY + 1)  // Priority of the processing task

static const char *TAG = "MACHINE_TASK";

// Function that will run as the FreeRTOS task calling
// machine_interface_setup_lookp infinitely.
int machine_task(void *pvParameters) {
  machine_interface_t *s_machine_interface =
      (machine_interface_t *)pvParameters;

  LOGI(TAG, ">> Running machine task loop...");
  // Call the setup loop function (this will run indefinitely)
  machine_interface_setup_loop(s_machine_interface);
  LOGI(TAG, "<< Machine Task Loop Ended?");

  // Should never reach here, but good practice to include
  _d(2, "Machine task unexpectedly exiting");
#ifdef ESP32_HW
  vTaskDelete(NULL);
#endif
  return 0;
}

bool machine_task_run(const char *task_name, machine_interface_t *machine,
#ifdef ESP32_HW
                      TaskHandle_t *machine_task_handle, BaseType_t pinned_core
#else
                      thrd_t *machine_task_handle
#endif
) {
#ifdef ESP32_HW
  if (*machine_task_handle != NULL) {
    LOGE(TAG, "Task already running!");
    return false;
  }
#endif
  if (machine == NULL) {
    LOGE(TAG, "Invalid machine interface provided.");
    return false;
  }
#ifdef ESP32_HW
  BaseType_t task_created =
      xTaskCreatePinnedToCore(machine_task,
                              "Machine",        // Task name
                              TASK_STACK_SIZE,  // Stack depth
                              machine,  // Parameter passed to the task (using
                                        // global s_machine_interface instead)
                              TASK_PRIORITY,        // Task priority
                              machine_task_handle,  // Task handle
                              pinned_core);

  if (task_created != pdPASS)
#else

  if (thrd_create(machine_task_handle, machine_task, machine) != thrd_success)
#endif
  {
    LOGE(TAG, "Failed to create machine response processing task!");
#ifdef ESP32_HW
    machine_task_handle = NULL;  // Ensure handle is NULL on failure
#endif
    return false;
  }
  LOGI(TAG, "Machine response processing task started successfully.");
  return true;
}

#ifdef __cplusplus
}
#endif

#endif  // ASYNC_GCODE_SENDING
