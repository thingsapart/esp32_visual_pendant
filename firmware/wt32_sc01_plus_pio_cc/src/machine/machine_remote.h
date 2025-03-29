// machine_remote.h
#ifndef MACHINE_REMOTE_H
#define MACHINE_REMOTE_H

#include "machine_interface.h"
#include "driver/remote_comms_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ASYNC_RESPONSE_PROCESSING

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#endif

#define MAX_MSG_BUFFERED 10

typedef struct {
    machine_interface_t base;
    // Add any remote-specific data here
    uint8_t hub_mac_address[6]; // Store the hub's MAC address
    bool hub_mac_received;

#ifdef ASYNC_RESPONSE_PROCESSING
    QueueHandle_t proc_task_event_queue; 
#else
    remote_msg_t msg_buffer[MAX_MSG_BUFFERED];
    size_t msg_buffer_msg_len[MAX_MSG_BUFFERED];
    size_t msg_buf_len;
#endif
} machine_interface_remote_t;

machine_interface_remote_t *machine_interface_remote_create(const uint8_t *hub_mac);
machine_interface_remote_t *machine_interface_remote_init(machine_interface_remote_t *self, const uint8_t *hub_mac);

void machine_interface_remote_destroy(machine_interface_remote_t *self);
void machine_interface_remote_deinit(machine_interface_remote_t *self);
void machine_interface_remote_process_messages(machine_interface_remote_t *self);

#ifdef ASYNC_RESPONSE_PROCESSING
bool machine_remote_setup_response_processing_task(machine_interface_remote_t *self, QueueHandle_t task_event_queue);
#endif


#ifdef __cplusplus
}
#endif

#endif // MACHINE_REMOTE_H