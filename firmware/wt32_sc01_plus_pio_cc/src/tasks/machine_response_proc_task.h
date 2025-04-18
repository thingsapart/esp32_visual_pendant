#ifndef __MACHINE_RESPONSE_PROC_TASK_H__
#define __MACHINE_RESPONSE_PROC_TASK_H__

#ifdef ASYNC_RESPONSE_PROCESSING

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The idea of this task is two-fold:
 * 1. a quick way to receive line-ready callbacks from arduino_serial_wrapper
 * ISR and store them in a ring-buffer-line structure without doing any real
 * processing from the ISR call,
 * 2. a task that is mostly suspended until a line is received from serial which
 * then processes the line and parses machine state from serial data in a task.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/arduino_serial_wrapper.h"
#include "machine/machine_interface.h"

/**
 * The machine processing task handles processing received machine state data.
 * It parses the received data and usually updates the internal machine state
 * mode.
 *
 * task_name: Task name.
 * machine_task_handle: <out> handle of created task.
 * queue: <out> handle of created queue.
 * serial_handle: Handle to the serial port obtained from serial_init.
 * machine: Pointer to the initialized machine_interface_t instance.
 * pinned_core: The core to which the task is pinned to, or tskNO_AFFINITY if
 * the task has no core affinity. returns true on success, false on failure.
 */
bool machine_response_proc_task_run(const char *task_name,
                                    TaskHandle_t *task_handle,
                                    QueueHandle_t *queue,
                                    machine_interface_t *machine,
                                    BaseType_t pinned_core);

/**
 * Notify task of available new data.
 *
 * task_event_queue: tasks' event queue.
 * data: data received.
 * len: size of data.
 * from_isr: use ISR-aware queue handling when called from ISR.
 */
void machine_response_proc_task_data_ready(QueueHandle_t task_event_queue,
                                       const char *data, size_t len, bool from_isr);

#ifdef __cplusplus
}
#endif

#endif // ASYNC_RESPONSE_PROCESSING

#endif // __MACHINE_RESPONSE_PROC_TASK_H__