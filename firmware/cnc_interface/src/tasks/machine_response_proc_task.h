#include "config.h"

#ifdef ASYNC_RESPONSE_PROCESSING

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "machine/machine_interface.h"

#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#else
#include "compat/queue.h"
#include "compat/threads.h"
#endif

int machine_response_proc_task_data_ready(
#ifdef ESP32_HW
    QueueHandle_t task_event_queue,
#else
    gcode_queue_t* task_event_queue,
#endif
    const uint8_t* data, size_t len, bool from_isr);

bool machine_response_proc_task_run(const char* task_name,
                                    machine_interface_t* machine,
#ifdef ESP32_HW
                                    TaskHandle_t* task_handle,
                                    QueueHandle_t* queue, BaseType_t pinned_core,
                                    size_t stack_size
#else
                                    thrd_t* task_handle, gcode_queue_t* queue
#endif
);

#ifdef __cplusplus
}
#endif

#endif  // ASYNC_RESPONSE_PROCESSING
