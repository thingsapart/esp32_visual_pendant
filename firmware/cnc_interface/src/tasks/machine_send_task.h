#ifndef MACHINE_SEND_TASK_H
#define MACHINE_SEND_TASK_H

#include "config.h"
#ifdef ASYNC_GCODE_SENDING

#ifdef __cplusplus
extern "C" {
#endif

#include "machine/machine_interface.h"

#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#else
#include "compat/threads.h"
#include "compat/queue.h"
#endif

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
);


#ifdef __cplusplus
}
#endif

#endif // ASYNC_GCODE_SENDING
#endif // MACHINE_SEND_TASK_H
