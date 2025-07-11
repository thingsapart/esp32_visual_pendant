#ifndef __MACHINE_TASK_H__
#define __MACHINE_TASK_H__

#include "config.h"

#ifdef ASYNC_GCODE_SENDING

#ifdef __cplusplus
extern "C"
{
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
    bool machine_task_run(const char *task_name,
                          machine_interface_t *machine,
#ifdef ESP32_HW
                          TaskHandle_t *machine_task_handle,
                          BaseType_t pinned_core
#else
                      thrd_t *machine_task_handle
#endif
    );
#ifdef __cplusplus
}
#endif

#endif // ASYNC_GCODE_SENDING

#endif // __MACHINE_TASK_H__