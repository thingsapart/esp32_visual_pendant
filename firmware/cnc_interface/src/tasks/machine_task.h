#ifndef MACHINE_TASK_H
#define MACHINE_TASK_H

#include "config.h"
#ifdef ASYNC_GCODE_SENDING

#ifdef __cplusplus
extern "C" {
#endif

#include "machine/machine_interface.h"

#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include "compat/threads.h"
#endif

bool machine_task_run(const char *task_name, machine_interface_t *machine,
#ifdef ESP32_HW
                      TaskHandle_t *machine_task_handle, BaseType_t pinned_core
#else
                      thrd_t *machine_task_handle
#endif
);

#ifdef __cplusplus
}
#endif

#endif  // ASYNC_GCODE_SENDING
#endif  // MACHINE_TASK_H
