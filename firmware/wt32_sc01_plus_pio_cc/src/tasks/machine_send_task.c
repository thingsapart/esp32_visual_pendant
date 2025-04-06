#include "machine_response_proc_task.h"

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

#define DEFAULT_TASK_STACK_SIZE (1024 * 2)
#define DEFAULT_TASK_PRIORITY (tskIDLE_PRIORITY + 1) // Priority of the processing task

#define QUEUE_LENGTH 25 // How many gcode commands to queue up before dropping.

static const char *TAG = "MACHINE_SEND_TASK";

typedef struct {
    machine_interface_t *machine;
    QueueHandle_t queue;
    const char *task_name;
} machine_send_task_args_t;

// Function that will run as the FreeRTOS task calling machine_interface_setup_lookp infinitely.
void machine_send_task(void *pvParameters) {
    machine_send_task_args_t *args = (machine_send_task_args_t *) pvParameters;

    machine_interface_t *machine = args->machine;
    QueueHandle_t queue = args->queue;
    const char *task_name = args->task_name;

    free(args);

    bool abort = false;
    char gcode[MAX_GCODE_STR_LEN];

    LOGI(TAG, ">> Running machine task loop...");

    while (!abort) {
        // Block indefinitely waiting for a notification from the queue
        if (xQueueReceive(queue, gcode, portMAX_DELAY) == pdTRUE) {
            // Notification received, try to send the received gcode.
            LOGI(TAG, "Sending gcode: %s", gcode);
            machine->_send_gcode(machine, gcode);
        }
        // If xQueueReceive fails unexpectedly (shouldn't with portMAX_DELAY), loop continues
    }
    LOGI(TAG, "<< Machine Task Loop Ended?");

    // Should never reach here, but good practice to include
    LOGE(TAG, "Machine task unexpectedly exiting");

    vTaskDelete(NULL);
}

bool machine_send_task_run(const char *task_name, 
                           TaskHandle_t *machine_task_handle,
                           QueueHandle_t *queue,
                           machine_interface_t *machine,
                           BaseType_t pinned_core,
                           BaseType_t freertos_task_stack_size,
                           BaseType_t freertos_task_prio) {
    if (freertos_task_prio < 0) { freertos_task_prio = DEFAULT_TASK_PRIORITY; }
    if (freertos_task_stack_size < 0) { freertos_task_prio = DEFAULT_TASK_STACK_SIZE; }

    *queue = xQueueCreate(QUEUE_LENGTH, sizeof(char) * MAX_GCODE_STR_LEN);
    if (queue == NULL) {
        LOGE(TAG, "Failed to create serial send notification queue!");
        return false;
    }

    machine_send_task_args_t *args = (machine_send_task_args_t *) malloc(sizeof(machine_send_task_args_t));
    args->machine = machine;
    args->queue = *queue;
    args->task_name = task_name;

    BaseType_t task_created = xTaskCreatePinnedToCore(
        machine_send_task,
        task_name,                      // Task name
        freertos_task_stack_size,       // Stack depth
        args,                           // Parameter passed to the task (using global s_machine_interface instead)
        freertos_task_prio,             // Task priority
        machine_task_handle,                    // Task handle
        pinned_core
    );
    LOGI(TAG, "Creating task: %s => %p, queue %p, machine %p", task_name, *machine_task_handle, *queue, machine);

    if (task_created != pdPASS) {
        LOGE(TAG, "Failed to create machine gcode sending task (%d)!", task_created);
        vQueueDelete(*queue);      // Clean up queue
        *machine_task_handle = NULL; // Ensure handle is NULL on failure
        free(args);
        return false;
    }

    LOGI(TAG, "Machine gcode sending task started successfully.");
    return true;
}

#ifdef __cplusplus
}
#endif