#ifndef __MACHINE_SEND_TASK_H__
#define __MACHINE_SEND_TASK_H__

#ifdef ASYNC_GCODE_SENDING

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/arduino_serial_wrapper.h"
#include "machine/machine_interface.h"

bool machine_send_task_run(const char *task_name, 
                           TaskHandle_t *machine_task_handle,
                           QueueHandle_t *queue,
                           machine_interface_t *machine,
                           BaseType_t pinned_core,
                           BaseType_t freertos_task_stack_size,
                           BaseType_t freertos_task_prio);

#ifdef __cplusplus
}
#endif

#endif // ASYNC_GCODE_SENDING

#endif // __MACHINE_SEND_TASK_H__