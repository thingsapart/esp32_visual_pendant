
#include "machine_response_proc_task.h"

#include "config.h"

#ifdef ASYNC_GCODE_SENDING

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdlib.h> 
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "debug.h"
#include "machine/machine_interface.h"

#define TASK_STACK_SIZE 1024 * 6
#define TASK_PRIORITY (tskIDLE_PRIORITY + 1) // Priority of the processing task

static const char *TAG = "MACHINE_TASK";

// Function that will run as the FreeRTOS task calling machine_interface_setup_lookp infinitely.
void machine_task(void *pvParameters) {
    machine_interface_t *s_machine_interface = (machine_interface_t *) pvParameters;

    LOGI(TAG, ">> Running machine task loop...");
    // Call the setup loop function (this will run indefinitely)
    machine_interface_setup_loop(s_machine_interface);
    LOGI(TAG, "<< Machine Task Loop Ended?");

    // Should never reach here, but good practice to include
    _d(2, "Machine task unexpectedly exiting");
    vTaskDelete(NULL);
}

bool machine_task_run(const char *task_name, TaskHandle_t *machine_task_handle, machine_interface_t *machine, BaseType_t pinned_core) {
    if (*machine_task_handle != NULL) {
        LOGE(TAG, "Task already running!");
        return false;
    }
    if (machine == NULL) {
        LOGE(TAG, "Invalid machine interface provided.");
        return false;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        machine_task,
        "Machine",                      // Task name
        TASK_STACK_SIZE,                // Stack depth
        machine,                        // Parameter passed to the task (using global s_machine_interface instead)
        TASK_PRIORITY,                  // Task priority
        machine_task_handle,           // Task handle
        pinned_core
    );

    if (task_created != pdPASS) {
        LOGE(TAG, "Failed to create machine response processing task!");
        machine_task_handle = NULL; // Ensure handle is NULL on failure
        return false;
    }

    LOGI(TAG, "Machine response processing task started successfully.");
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // ASYNC_GCODE_SENDING