#ifndef __MACHINE_RESPONSE_PROC_TASK_H__
#define __MACHINE_RESPONSE_PROC_TASK_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t machine_response_proc_task_handle;

/**
 * @brief Sets up and starts the machine response processing task.
 *
 * @param serial_handle Handle to the serial port obtained from serial_init.
 * @param machine Pointer to the initialized machine_interface_t instance.
 * @param pinned_core The core to which the task is pinned to, or tskNO_AFFINITY if the task has no core affinity.
 * @return true on success, false on failure.
 */
bool machine_response_proc_task_run(serial_handle_t serial_handle, machine_interface_t *machine, BaseType_t pinned_core);

#endif // __MACHINE_RESPONSE_PROC_TASK_H__