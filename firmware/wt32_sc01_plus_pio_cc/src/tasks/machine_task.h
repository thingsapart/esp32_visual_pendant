#ifndef __MACHINE_TASK_H__
#define __MACHINE_TASK_H__

#ifdef ASYNC_GCODE_SENDING

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/arduino_serial_wrapper.h"
#include "machine/machine_interface.h"

/**
 * @brief The machine task is responsible for managing periodic machine state
 * updates and sending non-urgent commands/messages to the machine.
 *
 * @param task_name Task name.
 * @param machine Pointer to the initialized machine_interface_t instance.
 * @param pinned_core The core to which the task is pinned to, or tskNO_AFFINITY
 * if the task has no core affinity.
 * @return true on success, false on failure.
 */
bool machine_task_run(const char *task_name, TaskHandle_t *machine_task_handle,
                      machine_interface_t *machine, BaseType_t pinned_core);

#ifdef __cplusplus
}
#endif

#endif // ASYNC_GCODE_SENDING

#endif // __MACHINE_TASK_H__