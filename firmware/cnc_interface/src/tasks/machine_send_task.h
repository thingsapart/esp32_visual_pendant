#ifndef __MACHINE_SEND_TASK_H__
#define __MACHINE_SEND_TASK_H__

#include "config.h"
#ifdef ASYNC_GCODE_SENDING

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <threads.h>

#include "compat/queue.h"
#endif

#include "driver/arduino_serial_wrapper.h"
#include "machine/machine_interface.h"

bool machine_send_task_run(const char *task_name, machine_interface_t *machine,
#ifdef ESP32_HW
                           TaskHandle_t *machine_task_handle,
                           QueueHandle_t *queue, BaseType_t pinned_core,
                           BaseType_t freertos_task_stack_size,
                           BaseType_t freertos_task_prio
#else
                           thrd_t *machine_task_handle, gcode_queue_t *queue
#endif
);

#ifdef __cplusplus
}
#endif

#endif  // ASYNC_GCODE_SENDING

#endif  // __MACHINE_SEND_TASK_H__
