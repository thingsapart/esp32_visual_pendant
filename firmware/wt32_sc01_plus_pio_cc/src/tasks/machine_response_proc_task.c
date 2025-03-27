#include "machine_response_proc_task.h"

TaskHandle_t machine_response_proc_task_handle = NULL;

static const char *TAG = "MACHINE_RESP_PROC_TASK";

#include "debug.h"

#include "machine/machine_interface.h"

// TODO: 

// * add run(...) setup routine, that takes serial_handle_t from arduino_serial_wrapper.h and a machine_interface_t *, sets up serial_register_line_callback for that serial handle, sets up a global freertos "serial-received" queue and starts the machine_response_proc_task.

// * add a new FIFO ring-buffer that is used to store data from arduino_serial_wrapper serial_register_line_callback callback:
//   * it has up to N (define, say 20 for now) max slots that share the underlying buffer, the slots are also user in circular fashion to store up to N strings.
//   * the buffer keeps track of current used min and max slots, (say N = 10, then start=3, end=7) means that slots 3-7 are used, but start = 3 and end =12 are also valid because (12 % 10 = 2, which is < 3) so they can all fit,
//   * when a new line is received from serial, the code increments the end slot, if `end % N < start` and the underlying buffer has enough free space, the code copies the line to the buffer free section of the buffer and saves pointer to start of string in buffer + length in a slot,
//   *   but if the buffer does not have enough space to store the line in ring-buffer (or no slots are available), the first occupied slot (at start) is freed and this is repeated util enough space is freed.

// * update `machine_response_proc_task`:
//   * block immediately on the global "serial-received" queue and wait for new data available in the queue,
//   * when new data is available:
//     * call machine_interface_process_machine_state_response() with passed in machine_interface_t instance and the new serial data available from the queue.

// * add new that implements serial_line_callback_t from arduino_serial_wrapper.h:
//   * it would be called from IRQ so should be safe and quick to finish,
//   * it will append and copy the line to the above FIFO ring-buffer,
//   * notify the global "serial-received" queue of the new data,
//   * and immediately return/.

void machine_response_proc_task(void *args) {
    machine_interface_t *machine = (machine_interface_t *);
    LOGI(TAG, ">> Starting machine task...");

    // TODO: implement blocking and queue processing from above.

    // Should never reach here, but good practice to include
    LOGW(TAG, "MachineState Processing Task terminating unexpectedly...");

    vTaskDelete(NULL);
}