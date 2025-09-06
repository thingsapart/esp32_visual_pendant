// machine_remote.h
#ifndef MACHINE_REMOTE_H
#define MACHINE_REMOTE_H

#include "config.h"
#include "driver/remote_comms_wrapper.h"
#include "machine_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ASYNC_RESPONSE_PROCESSING
#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#else
#include "compat/queue.h"
#endif
#endif

#define MAX_MSG_BUFFERED 10

typedef struct {
  machine_interface_t base;
  // Add any remote-specific data here
  uint8_t hub_mac_address[6];  // Store the hub's MAC address
  bool hub_mac_received;

  remote_msg_t msg_buffer[MAX_MSG_BUFFERED];
  size_t msg_buffer_msg_len[MAX_MSG_BUFFERED];
  size_t msg_buf_len;
#ifdef ASYNC_RESPONSE_PROCESSING
#ifdef ESP32_HW
  QueueHandle_t proc_task_event_queue;
#else
  gcode_queue_t *proc_task_event_queue;
#endif
#endif

} machine_interface_remote_t;

machine_interface_remote_t *machine_interface_remote_create(
    const uint8_t *hub_mac);
machine_interface_remote_t *machine_interface_remote_init(
    machine_interface_remote_t *self, const uint8_t *hub_mac);

void machine_interface_remote_destroy(machine_interface_remote_t *self);
void machine_interface_remote_deinit(machine_interface_remote_t *self);
void machine_interface_remote_process_messages(
    machine_interface_remote_t *self);

#ifdef ASYNC_RESPONSE_PROCESSING
bool machine_remote_setup_response_processing_task(
    machine_interface_remote_t *self,
#ifdef ESP32_HW
    QueueHandle_t task_event_queue
#else
    gcode_queue_t *task_event_queue
#endif
);
#endif

void *message_box_t_to_payload(const message_box_t *msg_box, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif  // MACHINE_REMOTE_H
