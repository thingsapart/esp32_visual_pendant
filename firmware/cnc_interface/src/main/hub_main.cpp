// hub_main.cpp

#include "config.h"

#ifdef ESP_NOW_HUB

#include <WiFi.h>
#include <assert.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "debug.h"
#include "driver/driver_interface.hpp"
#include "driver/remote_comms_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "machine/machine_interface.h"
#include "machine/machine_remote.h"  // for message_box_t_to_payload function.
#include "machine/machine_rrf.h"
#include "tasks/machine_response_proc_task.h"
#include "tasks/machine_send_task.h"

static const char *TAG = "hub_main";

// --- Configuration ---
#define HUB_POLL_INTERVAL_MS 200
#define FULL_STATE_INTERVAL \
  20  // Send full state every nth poll (every n * interval secs)

// Replace with the display's MAC address
static const uint8_t display_mac_address[] = DISPLAY_MAC_ADDR;

// --- Global Variables ---
static machine_rrf_t *g_machine = NULL;
static machine_interface_t *g_machine_base = NULL;
static int g_full_state_counter = 0;
static uint16_t g_binary_seq_id =
    0;  // Sequence ID counter for sending binary payloads

#define PAYLOAD_MAX (ESP_NOW_MAX_DATA_LEN + 1)

#define HUB_TASK_PRIORITY (tskIDLE_PRIORITY + 1)

#ifdef ASYNC_RESPONSE_PROCESSING
#define MAX_MSG_BUFFER 0

#define RECV_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#define RECV_QUEUE_LENGTH 3

#else
#define MAX_MSG_BUFFER 20
static uint8_t message_buffer1[PAYLOAD_MAX][MAX_MSG_BUFFER];
static uint8_t message_buffer2[PAYLOAD_MAX][MAX_MSG_BUFFER];
static size_t message_lens1[MAX_MSG_BUFFER];
static size_t message_lens2[MAX_MSG_BUFFER];
static uint8_t (*message_buffer)[PAYLOAD_MAX][MAX_MSG_BUFFER];
static size_t *message_lens;

static size_t message_buffer_len;
#endif


// --- Callback Functions (for machine interface) ---
// These are called when the machine's state changes.

void on_machine_state_change(machine_interface_t *machine, void *user_data) {
  LOG_CURR_TASK();

  // Send status update message
  status_msg_t msg;
  msg.type = MSG_TYPE_STATUS;
  msg.status = machine->machine_status;
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
  LOGV(TAG, "Sending status %d", machine->machine_status);
}

void on_position_change(machine_interface_t *machine, void *user_data) {
  position_msg_t msg;
  msg.type = MSG_TYPE_POSITION;
  msg.x = machine->position[0];
  msg.y = machine->position[1];
  msg.z = machine->position[2];
  msg.wcs_x = machine->wcs_position[0];
  msg.wcs_y = machine->wcs_position[1];
  msg.wcs_z = machine->wcs_position[2];
  LOGI(TAG, "Sending position %f, %f, %f (%f, %f, %f)", machine->position[0],
       machine->position[1], machine->position[2], machine->wcs_position[0],
       machine->wcs_position[1], machine->wcs_position[2]);
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
}

void on_home_change(machine_interface_t *machine, void *user_data) {
  // Send homing status update
  homed_msg_t msg;
  msg.type = MSG_TYPE_HOMED;
  msg.x_homed = machine->axes_homed[0];
  msg.y_homed = machine->axes_homed[1];
  msg.z_homed = machine->axes_homed[2];
  LOGI(TAG, "Sending axes homed %d, %d, %d", machine->axes_homed[0],
       machine->axes_homed[1], machine->axes_homed[2]);
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
}

void on_wcs_change(machine_interface_t *machine, void *user_data) {
  // Send WCS update
  wcs_msg_t msg;
  msg.type = MSG_TYPE_WCS;
  msg.wcs = machine->wcs;
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
  LOGI(TAG, "Sending wcs changed: %d", machine->wcs);
}

void on_feed_change(machine_interface_t *machine, void *user_data) {
  feed_msg_t msg;
  msg.type = MSG_TYPE_FEED;
  msg.feed = machine->feed;
  msg.feed_req = machine->feed_req;
  msg.feed_multiplier = machine->feed_multiplier;
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
  LOGI(TAG, "Sending feed changed: %f/%f (x%f)", machine->feed,
       machine->feed_req, machine->feed_multiplier);
}

void on_sensors_change(machine_interface_t *machine, void *user_data) {
  // TODO
}

void on_dialogs_change(machine_interface_t *machine, void *user_data) {
  if (machine->message_box) {
    LOGI(TAG, "Dialog change detected.");

    LOGI(TAG, "Message box present, attempting to serialize and send.");
    size_t payload_size;
    void *msg_box_payload =
        message_box_t_to_payload(machine->message_box, &payload_size);
    if (msg_box_payload) {
      LOGI(TAG, "Serialized message box to %u bytes.", payload_size);
      remote_wrapper_send_fragmented_message(display_mac_address,
                          MSG_SUB_TYPE_MESSAGE_BOX, (const uint8_t*)msg_box_payload, payload_size);
      free(msg_box_payload);
    } else {
      LOGE(TAG, "Failed to serialize message box.");
    }
  }
}

void on_spindles_tools_change(machine_interface_t *machine, void *user_data) {
  spindles_tools_msg_t msg;
  msg.type = MSG_TYPE_SPINDLES_TOOLS;
  if (machine->spindles) {
    msg.rpm = machine->spindles->rpm;
  } else {
    msg.rpm = 0;
  }
  msg.tool = "";  // machine->tool;
  // we should not send string data directly over the air, it's inefficient.
  // so we're sending only the length, for this example.
  remote_wrapper_send(
      display_mac_address, (uint8_t *)&msg,
      sizeof(msg.type) + sizeof(msg.rpm) + sizeof(size_t) /* tool len */);
  LOGI(TAG, "Sending tool/spindle changed: rpm %d / %s", msg.rpm, msg.tool);
}

void on_files_changed(machine_interface_t *mach, void *user_data,
                      const char *path, const char **files) {
  LOGI(TAG, "Files change detected.");

  size_t idx = MAX_FILE_LISTS;
  for (size_t i = 0; i < MAX_FILE_LISTS; i++) {
    if (strcmp(mach->filelists[i].fdir, path) == 0) {
      idx = i;
      break;
    }
  }

  if (idx >= MAX_FILE_LISTS) {
    LOGE(TAG, "Cannot find file list for directory %s.", path);
    return;
  }

  LOGI(TAG, "Files present, attempting to serialize and send.");

  // Compute size of all strings + header.
  file_list_payload_t *header;
  size_t total_size = sizeof(*header);
  total_size += strlen(path) + 1;
  size_t num_files = 0;
  size_t i = 0;
  while (mach->filelists[idx].files[i] != NULL) {
    total_size += strlen(mach->filelists[idx].files[i]) + 1;
    ++num_files;
    ++i;
  }

  uint8_t buf[total_size];
  memset(buf, 0, sizeof(buf));

  header = (file_list_payload_t *)buf;
  header->total_size = total_size;
  header->num_files = num_files;
  char *offset = (char *)header + sizeof(*header);
  memcpy(offset, path, strlen(path) + 1);
  offset += strlen(path) + 1;

  i=0;
  while (mach->filelists[idx].files[i] != NULL) {
    size_t sz = strlen(mach->filelists[idx].files[i]) + 1;
    memcpy(offset, mach->filelists[idx].files[i], sz);
    offset += sz;
    ++i;
  }

  LOGI(TAG, "Serialized file list to %u bytes.", total_size);
  if (!remote_wrapper_send_fragmented_message(display_mac_address, MSG_SUB_TYPE_FILE_LIST, buf, total_size)) {
    LOGI(TAG, "Failed to send binary file list to %u bytes.", total_size);
  }
}

void on_log_message_received(machine_interface_t *machine, void *user_data, const char *message) {
    size_t len = strlen(message) + 1; // Include null terminator

    LOGI(TAG, "Broadcasting log message: %s", message);

    // If message is short enough for a single packet, use the simple format
    if (len <= (REMOTE_COMMS_DATA_MAX - offsetof(log_msg_t, message))) {
        size_t total_len = offsetof(log_msg_t, message) + len;
        log_msg_t* msg = (log_msg_t*)malloc(total_len);
        if (msg) {
            msg->type = MSG_TYPE_LOG_MESSAGE;
            strcpy(msg->message, message);
            remote_wrapper_send(display_mac_address, (const uint8_t*)msg, total_len);
            free(msg);
        }
    } else {
        // Otherwise, use the robust fragmentation logic
        remote_wrapper_send_fragmented_message(display_mac_address, MSG_SUB_TYPE_LOG_MESSAGE, (const uint8_t*)message, len);
    }
}

void on_connected_change(machine_interface_t *machine, void *user_data) {
  _df(0, "[%s] Machine connection state changed: %s", TAG,
      machine->is_connected(machine) ? "connected" : "disconnected");
}

// --- Process client commands ---

static void process_send_gcode_cmd(const uint8_t *data, int data_len) {
  if (data_len < sizeof(send_gcode_cmd_t)) {
    LOGE(TAG, "Invalid send_gcode command length");
    return;
  }
  send_gcode_cmd_t *cmd = (send_gcode_cmd_t *)data;
  if (data_len <
      sizeof(send_gcode_cmd_t) + cmd->len) {  // Check for complete data
    LOGE(TAG, "Invalid gcode command length 2");
    return;
  }
  // Ensure null termination of the G-code string, even with flexible array
  // member
  char gcode[cmd->len + 1];  // temporary buffer on the stack.
  memcpy(gcode, cmd->gcode, cmd->len);
  gcode[cmd->len] = '\0';
  LOGI(TAG, "Received G-code command: %s", gcode);
  machine_interface_send_gcode(g_machine_base, gcode, 0);  // Pass gcode string
}

static void process_move_cont_cmd(const uint8_t *data, int data_len) {
  if (data_len != sizeof(move_cont_cmd_t)) {
    LOGE(TAG, "Invalid move cont len");
    return;
  }
  move_cont_cmd_t *cmd = (move_cont_cmd_t *)data;
  LOGI(TAG, "Move cont: %c, %f, %d", cmd->axis, cmd->feed, cmd->direction);
  g_machine_base->move_continuous(g_machine_base, cmd->axis, cmd->feed,
                                  cmd->direction);
}

static void process_move_cont_stop_cmd(const uint8_t *data, int data_len) {
  if (data_len != sizeof(move_cont_stop_cmd_t)) {
    LOGE(TAG, "Invalid move cont stop len");
    return;
  }
  LOGI(TAG, "Move cont stop");
  g_machine_base->move_continuous_stop(g_machine_base);
}
static void process_move_cmd(const uint8_t *data, int data_len) {
  if (data_len != sizeof(move_cmd_t)) {
    LOGE(TAG, "Invalid move len");
    return;
  }
  move_cmd_t *cmd = (move_cmd_t *)data;
  LOGI(TAG, "Move : %c, %f, %f", cmd->axis, cmd->feed, cmd->value);
  g_machine_base->move(g_machine_base, cmd->axis, cmd->feed, cmd->value);
}
static void process_home_all_cmd(const uint8_t *data, int data_len) {
  if (data_len != sizeof(home_all_cmd_t)) {
    LOGE(TAG, "Invalid home_all len");
    return;
  }
  LOGI(TAG, "Home all");
  g_machine_base->home_all(g_machine_base);
}

static void process_home_cmd(const uint8_t *data, int data_len) {
  if (data_len < sizeof(home_cmd_t)) {  // at least
    LOGE(TAG, "Invalid home command length");
    return;
  }
  home_cmd_t *cmd = (home_cmd_t *)data;

  if (data_len <
      sizeof(home_cmd_t) + cmd->axes_len) {  // Check for complete data
    LOGE(TAG, "Invalid home command length. incomplete data.");
    return;
  }

  char axes[cmd->axes_len + 1];
  memcpy(axes, cmd->axes, cmd->axes_len);
  axes[cmd->axes_len] = '\0';
  LOGI(TAG, "Received home command, axes = %s", axes);
  delay(200);

  g_machine_base->home(g_machine_base, axes);
}

static void process_set_wcs_cmd(const uint8_t *data, int data_len) {
  if (data_len != sizeof(set_wcs_cmd_t)) {
    LOGE(TAG, "Invalid set wcs len");
    return;
  }
  set_wcs_cmd_t *cmd = (set_wcs_cmd_t *)data;
  LOGI(TAG, "Set wcs %d", cmd->wcs);
  g_machine_base->set_wcs(g_machine_base, cmd->wcs);
}

static void process_set_wcs_zero_cmd(const uint8_t *data, int data_len) {
  if (data_len < sizeof(set_wcs_zero_cmd_t)) {
    LOGE(TAG, "Invalid set_wcs_zero command length");
    return;
  }
  set_wcs_zero_cmd_t *cmd = (set_wcs_zero_cmd_t *)data;
  if (data_len <
      sizeof(set_wcs_zero_cmd_t) + cmd->axes_len) {  // Check for complete data
    LOGE(TAG, "Invalid set_wcs_zero command length. incomplete data.");
    return;
  }
  char axes[cmd->axes_len + 1];
  memcpy(axes, cmd->axes, cmd->axes_len);
  axes[cmd->axes_len] = '\0';
  LOGI(TAG, "Received wcs zero command. wcs=%d, axes=%s", cmd->wcs, axes);

  g_machine_base->set_wcs_zero(g_machine_base, cmd->wcs, axes);
}

static void process_next_wcs_cmd(const uint8_t *data, int data_len) {
  if (data_len != sizeof(next_wcs_cmd_t)) {
    LOGE(TAG, "Invalid next_wcs len");
    return;
  }
  LOGI(TAG, "Next wcs");
  g_machine_base->next_wcs(g_machine_base);
}

static void process_run_macro_cmd(const uint8_t *data, int data_len) {
  if (data_len < sizeof(run_macro_cmd_t)) {
    LOGE(TAG, "Invalid run_macro command length");
    return;
  }
  run_macro_cmd_t *cmd = (run_macro_cmd_t *)data;
  if (data_len <
      sizeof(run_macro_cmd_t) + cmd->len) {  // Check for complete data
    LOGE(TAG, "Invalid run_macro command length. incomplete data.");
    return;
  }
  char macro_name[cmd->len + 1];
  memcpy(macro_name, cmd->macro_name, cmd->len);
  macro_name[cmd->len] = '\0';

  LOGI(TAG, "Received run_macro command: %s", macro_name);
  g_machine_base->run_macro(g_machine_base, macro_name);
}
static void process_start_job_cmd(const uint8_t *data, int data_len) {
  if (data_len < sizeof(start_job_cmd_t)) {
    LOGE(TAG, "Invalid start_job command length");
    return;
  }
  start_job_cmd_t *cmd = (start_job_cmd_t *)data;
  if (data_len <
      sizeof(start_job_cmd_t) + cmd->len) {  // Check for complete data
    LOGE(TAG, "Invalid start_job command length. incomplete data.");
    return;
  }
  char job_name[cmd->len + 1];
  memcpy(job_name, cmd->job_name, cmd->len);
  job_name[cmd->len] = '\0';

  LOGI(TAG, "Received start_job command: %s", job_name);
  g_machine_base->start_job(g_machine_base, job_name);
}

static void process_list_files_cmd(const uint8_t *data, int data_len) {
  if (data_len < sizeof(list_files_cmd_t)) {
    LOGE(TAG, "Invalid list_files command length");
    return;
  }
  list_files_cmd_t *cmd = (list_files_cmd_t *)data;

  if (data_len <
      sizeof(list_files_cmd_t) + cmd->len) {  // Check for complete data
    LOGE(TAG, "Invalid list_files command length.  incomplete data.");
    return;
  }
  char path[cmd->len + 1];
  memcpy(path, cmd->path, cmd->len);
  path[cmd->len] = '\0';
  LOGI(TAG, "List files: %s", path);
  g_machine_base->list_files(g_machine_base, path);
}

static void process_probe_cmd(const uint8_t *data, int data_len) {
  if (data_len < sizeof(probe_cmd_t)) {
    LOGE(TAG, "Invalid probe command length");
    return;
  }
  probe_cmd_t *cmd = (probe_cmd_t *)data;

  if (data_len < sizeof(probe_cmd_t) + cmd->len) {  // Check for complete data.
    LOGE(TAG, "Invalid probe command length. incomplete data.");
    return;
  }

  char gcode[cmd->len + 1];
  memcpy(gcode, cmd->gcode, cmd->len);
  gcode[cmd->len] = '\0';

  LOGI(TAG, "Received probe command: %s", gcode);
  g_machine_base->probe(g_machine_base, gcode);
}

// --- ESP-NOW Callbacks ---

void on_remote_data_sent(const uint8_t *mac_addr, int status, void *user_data) {
  LOGV(TAG, "ESP-NOW send status: %s", status == 0 ? "success" : "fail");
  if (status != 0) {
    LOGW(TAG, "ESP-NOW send status: fail");
  }
}

void process_message(const uint8_t *data, const size_t data_len) {
  // Process incoming commands from the display
  uint8_t command_type = data[0];  // get the command type
  switch (command_type) {
    case CMD_TYPE_SEND_GCODE:
      LOGI(TAG, "<SEND GCODE>");
      process_send_gcode_cmd(data, data_len);
      break;
    case CMD_TYPE_MOVE_CONT:
      LOGI(TAG, "<MOVE_CONT>");
      process_move_cont_cmd(data, data_len);
      break;
    case CMD_TYPE_MOVE_CONT_STOP:
      LOGI(TAG, "<MOVE_CONT_STOP>");
      process_move_cont_stop_cmd(data, data_len);
      break;
    case CMD_TYPE_MOVE:
      LOGI(TAG, "<MOVE>");
      process_move_cmd(data, data_len);
      break;
    case CMD_TYPE_HOME_ALL:
      LOGI(TAG, "<HOME_ALL>");
      process_home_all_cmd(data, data_len);
      break;
    case CMD_TYPE_HOME:
      LOGI(TAG, "<HOME_1>");
      process_home_cmd(data, data_len);
      break;
    case CMD_TYPE_SET_WCS:
      LOGI(TAG, "<SET_WCS>");
      process_set_wcs_cmd(data, data_len);
      break;
    case CMD_TYPE_SET_WCS_ZERO:
      LOGI(TAG, "<WCS_ZERO>");
      process_set_wcs_zero_cmd(data, data_len);
      break;
    case CMD_TYPE_NEXT_WCS:
      LOGI(TAG, "<NEXT_WCS>");
      process_next_wcs_cmd(data, data_len);
      break;
    case CMD_TYPE_RUN_MACRO:
      LOGI(TAG, "<RUN_MACRO>");
      process_run_macro_cmd(data, data_len);
      break;
    case CMD_TYPE_START_JOB:
      LOGI(TAG, "<RUN_JOB>");
      process_start_job_cmd(data, data_len);
      break;
    case CMD_TYPE_LIST_FILES:
      LOGI(TAG, "<LIST_FILES>");
      process_list_files_cmd(data, data_len);
      break;
    case CMD_TYPE_PROBE:
      LOGI(TAG, "<PROBE_CMD>");
      process_probe_cmd(data, data_len);
      break;

    default:
      LOGW(TAG, "Unknown command type: %d", command_type);
      break;
  }
}

#ifndef ASYNC_RESPONSE_PROCESSING

void process_buffered_messages() {
  // Execute remote commands.
  size_t msgbuf_len = message_buffer_len;
  uint8_t (*buf)[PAYLOAD_MAX][MAX_MSG_BUFFER] = message_buffer;
  size_t *lens = message_lens;

  message_buffer_len = 0;
  if (message_buffer == &message_buffer2) {
    message_buffer = &message_buffer1;
    message_lens = &message_lens1[0];
  } else {
    message_buffer = &message_buffer2;
    message_lens = &message_lens2[0];
  }

  LOGI(TAG, ">>  Buffered messages: %d", msgbuf_len);
  for (size_t i = 0; i < msgbuf_len; ++i) {
    size_t data_len = lens[i];
    uint8_t *data_buf = &(*buf[i][0]);
    LOGI(TAG, "Processing buffered message: %d", i);
    process_message(data_buf, data_len);
  }
}

void on_remote_data_recv(const uint8_t *mac_addr, const uint8_t *data,
                         int data_len, void *user_data) {
  LOGI(TAG, "Remote message received (len %d)", data_len);

  size_t msgbuf_len = message_buffer_len;
  ++message_buffer_len;
  if (msgbuf_len >= MAX_MSG_BUFFER) {
    LOGW(TAG, "Message buffer full or processing in progress...");
    return;
  }

  uint8_t *cmd = &(*message_buffer)[msgbuf_len][0];
  memcpy(cmd, data, data_len);
  cmd[data_len] = '\0';
  message_lens[msgbuf_len] = data_len;

  _df(0, "[%s] Received ESP-NOW data from " MACSTR ", len: %d, data: \"%s\"",
      TAG, MAC2STR(mac_addr), data_len, cmd);
}

#else

static QueueHandle_t recv_queue = NULL;
static TaskHandle_t recv_task_handle = NULL;

void remote_recv_task(void *args) {
  bool abort = false;

  uint8_t msg[PAYLOAD_MAX + 2];

  while (!abort) {
    // Block indefinitely waiting for a notification from the queue
    if (xQueueReceive(recv_queue, msg, portMAX_DELAY) == pdTRUE) {
      const uint8_t *data_buf = &msg[1];
      const uint8_t data_len = msg[0];

      LOGI(TAG, "Processing buffered message: %d", data_len);
      LOGI(TAG, "Q MSG: %u:%u:%u:%u:%u", msg[0], msg[1], msg[2], msg[3],
           msg[5]);
      process_message(data_buf, data_len);
    }
    // If xQueueReceive fails unexpectedly (shouldn't with portMAX_DELAY), loop
    // continues
  }
  LOGI(TAG, "<< Machine Task Loop Ended?");

  // Should never reach here, but good practice to include
  LOGE(TAG, "Machine task unexpectedly exiting");

  vTaskDelete(NULL);
}

void process_buffered_messages() {}

void on_remote_data_recv(const uint8_t *mac_addr, const uint8_t *data,
                         int data_len, void *user_data) {
  uint8_t buf[PAYLOAD_MAX + 2];

  memset(buf, 0, PAYLOAD_MAX + 2);
  buf[0] = data_len;
  memcpy(buf + 1, data, data_len);
  LOGI(TAG, "MSG: %d/%u, %u:%u:%u:%u:%u => %u:%u:%u:%u:%u:%u", data_len,
       data_len, data[0], data[1], data[2], data[3], data[4], buf[0], buf[1],
       buf[2], buf[3], buf[4], buf[5]);

  if (recv_queue != NULL) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // BaseType_t result = xQueueSendToBackFromISR(recv_queue, buf,
    // &xHigherPriorityTaskWoken);
    BaseType_t result = xQueueSendToBack(recv_queue, buf, 0);
    if (result != pdTRUE) {
      LOGW(TAG, "Remote recv task queue full.");
    }
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  } else {
    LOGW(TAG, "Cannot queue from on_remote_data_recv => queue NULL");
  }
}

void remote_recv_task_run() {
  recv_queue = xQueueCreate(RECV_QUEUE_LENGTH, PAYLOAD_MAX);
  if (recv_queue == NULL) {
    LOGE(TAG, "Failed to create remote command received task queue!");
    return;
  }

  xTaskCreatePinnedToCore(remote_recv_task, "remote_recv_task", 6 * 1024, NULL,
                          5, &recv_task_handle, TASK_MACHINE_STATE_PROC_CORE);
}

#endif

unsigned int ctr = 0;
void machine_poll_send_task_iter() {
  // Send queued g-code commands.
  machine_interface_process_gcode_q(&g_machine->base);

  // Poll the new machine state.
  machine_interface_task_loop_iter(&g_machine->base);

  const unsigned int iter = ctr++ % 10;
  machine_interface_t *mach = &g_machine->base;
  if (iter == 1) {
    on_machine_state_change(mach, mach);
  } else if (iter == 2) {
    on_position_change(mach, mach);
  } else if (iter == 3) {
    on_home_change(mach, mach);
  } else if (iter == 4) {
    on_wcs_change(mach, mach);
  } else if (iter == 5) {
    on_feed_change(mach, mach);
  } else if (iter == 6) {
    on_sensors_change(mach, mach);
  } else if (iter == 7) {
    on_dialogs_change(mach, mach);
  } else if (iter == 8) {
    on_spindles_tools_change(mach, mach);
  } else {
    // Send keep-alive message
    keep_alive_msg_t keep_alive_msg;
    keep_alive_msg.type = MSG_TYPE_KEEP_ALIVE;
    remote_wrapper_send(display_mac_address, (uint8_t *)&keep_alive_msg,
                        sizeof(keep_alive_msg));
  }

  LOGV(TAG, "Tick...");
}

void hub_task(void *pvParameters) {
#ifndef ASYNC_RESPONSE_PROCESSING
  message_buffer_len = 0;
  message_buffer = &message_buffer1;
  message_lens = &message_lens1[0];
#endif

  // Initialize ESP-NOW
  if (!remote_wrapper_init(on_remote_data_recv, on_remote_data_sent, NULL)) {
    _df(2, "[%s] Failed to initialize ESP-NOW", TAG);
    vTaskDelete(NULL);
    return;
  }

  // Add the display as a peer
  if (!remote_wrapper_add_peer(display_mac_address)) {
    _df(2, "[%s] Failed to add display as peer", TAG);
    vTaskDelete(NULL);
    return;
  }

  LOGI(TAG, "Initilized hub_task... DONE");
  // Main loop
  while (1) {
    // Process buffered commands, possibly altering machine state.
    process_buffered_messages();
    // Read out new machine state and send to clients.
    machine_poll_send_task_iter();

    vTaskDelay(pdMS_TO_TICKS(HUB_POLL_INTERVAL_MS));
  }
}

void setup_machine_interface() {
  // Initialize the machine interface
  g_machine = machine_rrf_create(0, HUB_POLL_INTERVAL_MS, MACH_UART_PIN_TX,
                                 MACH_UART_PIN_RX);  // Use UART 0
  if (!g_machine) {
    _df(2, "[%s] Failed to create machine interface", TAG);
    vTaskDelete(NULL);
    return;
  }
  g_machine_base = &g_machine->base;
  LOGI(TAG, ">> MACH_RRF %p", g_machine);
  LOGI(TAG, ">> MACH %p", g_machine_base);
  LOGI(TAG, ">> SPINDLE %p", g_machine_base->spindles);

  // Register callbacks
  machine_interface_t *mach = &g_machine->base;
  machine_interface_add_state_change_cb(mach, mach, on_machine_state_change);
  machine_interface_add_pos_changed_cb(mach, mach, on_position_change);
  machine_interface_add_home_changed_cb(mach, mach, on_home_change);
  machine_interface_add_wcs_changed_cb(mach, mach, on_wcs_change);
  machine_interface_add_feed_changed_cb(mach, mach, on_feed_change);
  machine_interface_add_sensors_changed_cb(mach, mach, on_sensors_change);
  machine_interface_add_dialogs_changed_cb(mach, mach, on_dialogs_change);
  machine_interface_add_spindles_tools_changed_cb(mach, mach,
                                                  on_spindles_tools_change);
  machine_interface_add_connected_changed_cb(mach, mach, on_connected_change);
  machine_interface_add_log_message_cb(mach, mach, on_log_message_received);


  remote_recv_task_run();
}

TaskHandle_t hub_task_handle = NULL;

void setup_hub_tasks() {
  xTaskCreatePinnedToCore(hub_task, "hub_task", 12 * 1024, NULL,
                          HUB_TASK_PRIORITY, &hub_task_handle, 0);
}

TaskHandle_t machine_rrf_proc_task_handle = NULL;
QueueHandle_t machine_rrf_proc_queue = NULL;

TaskHandle_t machine_send_task_handle = NULL;
QueueHandle_t machine_send_queue = NULL;

void setup() {
  mcu_setup();
  mcu_startup();

  setup_machine_interface();

  bool abort = false;

#ifdef ASYNC_RESPONSE_PROCESSING
  /*
   * Currently also handled by hub_task.
  LOGI(TAG, "Creating RRF Machine Task... ");
  if (!abort && machine_task_run("MachineRRF", &machine_rrf_task,
  &g_machine_base, TASK_MACHINE_CORE)) { LOGI(TAG, "DONE\n"); } else { LOGE(TAG,
  "\nFAIL: Could not create Machine Task: error"); abort = true;
  }
  */

  LOGI(TAG, "Creating RRF Machine State Processing Task... ");
  if (!abort && machine_response_proc_task_run("MachineRRFProc", g_machine_base,
                                               &machine_rrf_proc_task_handle,
                                               &machine_rrf_proc_queue,
                                               TASK_MACHINE_CORE)) {
    if (!machine_rrf_setup_response_processing_task(g_machine,
                                                    machine_rrf_proc_queue)) {
      LOGE(TAG, "Failed to set up even processing queue for RRF task");
    }
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "FAIL: Could not create RRF Machine Processing Task: error");
    abort = true;
  }
#endif

#ifdef ASYNC_GCODE_SENDING
  LOGI(TAG, "Creating Machine GCode Sending Task... ");
  if (!abort && machine_send_task_run("MachineSendTask", g_machine_base,
                                      &machine_send_task_handle,
                                      &machine_send_queue, TASK_MACHINE_CORE,
                                      2 * 1024, tskIDLE_PRIORITY + 1)) {
    g_machine->base.gcode_queue = machine_send_queue;
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "FAIL: Could not create Machine GCode Sending Task: error");
    abort = true;
  }

  LOGI(TAG, "Creating Remote Receive Processing Task... ");
  if (!abort) {
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "FAIL: Could not create RRF Machine Processing Task: error");
    abort = true;
  }

  LOGI(
      TAG,
      "Tasks:\n\n  RECV: %d\n  RRF-PROC: %d\n  MACH SEND: %d\n  HUB TASK: %d\n",
      (int)recv_task_handle, (int)machine_rrf_proc_task_handle, (int)machine_send_task_handle,
      (int)hub_task_handle);
#endif

#ifndef USE_ARDINO_SETUP_LOOP
  setup_hub_tasks();
  LOGI(
      TAG,
      "Tasks:\n\n  RECV: %d\n  RRF-PROC: %d\n  MACH SEND: %d\n  HUB TASK: %d\n",
      (int)recv_task_handle, (int)machine_rrf_proc_task_handle, (int)machine_send_task_handle,
      (int)hub_task_handle);
#else
  LOGI(TAG, "Machine loaded..\n");
#endif
}

void loop() {
#ifndef USE_ARDINO_SETUP_LOOP
  // Don't need to loop here.
  // Let FreeRTOS machine poll/hub tasks handle their own loops.
  vTaskDelete(NULL);
#else
  // Process buffered commands, possibly altering machine state.
  process_buffered_messages();
  // Read out new machine state and send to clients.
  machine_poll_send_task_iter();

  delay(HUB_POLL_INTERVAL_MS - 10);
#endif
}
#endif
