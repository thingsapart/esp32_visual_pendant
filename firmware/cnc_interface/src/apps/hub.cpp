// hub_main.cpp

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL D_INFO
#define UI_DEBUG_LOCAL_LEVEL D_INFO
#include "debug.h"

#include "config.h"

#ifdef ESP_NOW_HUB

#include <WiFi.h>
#include <assert.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <string.h>


#include "driver/driver_interface.hpp"
#include "driver/remote_comms_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "led_status.h"
#include "machine/machine_interface.h"
#include "machine/machine_remote.h"  // for message_box_t_to_payload function.
#include "machine/machine_rrf.h"
#include "tasks/machine_response_proc_task.h"
#include "tasks/machine_send_task.h"
#include "driver/task_registry.h"

static const char *TAG = "hub_main";

// --- Configuration ---
// Default poll interval for hub loop (ms).
// 50 ms gives ~20 Hz position updates and roughly halves command relay latency.
#define HUB_POLL_INTERVAL_MS 120
#define FULL_STATE_INTERVAL \
  48  // Send full state every nth poll (every n * 50 ms ≈ 2.4 s)

// Print a machine state summary every N ticks (N * HUB_POLL_INTERVAL_MS ms)
#define STATE_LOG_INTERVAL_TICKS 60  // ~3 s at 50 ms base interval

// Replace with the display's MAC address
static const uint8_t display_mac_address[] = DISPLAY_MAC_ADDR;

// --- Log coalescing configuration ---
#define LOG_COALESCE_MS 30
#define LOG_BATCH_CAP 1024

static char log_batch_buf[LOG_BATCH_CAP];
static size_t log_batch_len = 0;
static esp_timer_handle_t log_batch_timer = NULL;
static SemaphoreHandle_t log_batch_lock = NULL;

static void log_batch_timer_cb(void *arg) {
  // Flush batched log messages
  if (!log_batch_lock) return;
  if (xSemaphoreTake(log_batch_lock, pdMS_TO_TICKS(10)) != pdTRUE) return;
  if (log_batch_len > 0) {
    size_t len = log_batch_len + 1; // include NUL
    led_status_sending();
    if (len <= (REMOTE_COMMS_DATA_MAX - offsetof(log_msg_t, message))) {
      size_t total_len = offsetof(log_msg_t, message) + len;
      log_msg_t* msg = (log_msg_t*)malloc(total_len);
      if (msg) {
        msg->type = MSG_TYPE_LOG_MESSAGE;
        memcpy(msg->message, log_batch_buf, log_batch_len);
        msg->message[log_batch_len] = '\0';
        remote_wrapper_send(display_mac_address, (const uint8_t*)msg, total_len);
        free(msg);
      }
    } else {
      remote_wrapper_send_fragmented_message(display_mac_address, MSG_SUB_TYPE_LOG_MESSAGE, (const uint8_t*)log_batch_buf, log_batch_len + 1);
    }
    log_batch_len = 0;
    log_batch_buf[0] = '\0';
  }
  xSemaphoreGive(log_batch_lock);
}

// --- Global Variables ---
static machine_rrf_t *g_machine = NULL;
static machine_interface_t *g_machine_base = NULL;
static int g_full_state_counter = 0;
static uint16_t g_binary_seq_id =
    0;  // Sequence ID counter for sending binary payloads
static bool g_sent_filelists_to_display = false;

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
  static size_t i = 0;
  if (i++ % 50 == 0) {
    LOG_CURR_TASK();
  }

  // Send status update message
  led_status_sending();
  status_msg_t msg;
  msg.type = MSG_TYPE_STATUS;
  msg.status = machine->machine_status;
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
}

void on_position_change(machine_interface_t *machine, void *user_data) {
  led_status_sending();
  position_msg_t msg;
  msg.type = MSG_TYPE_POSITION;
  msg.x = machine->position[0];
  msg.y = machine->position[1];
  msg.z = machine->position[2];
  msg.wcs_x = machine->wcs_position[0];
  msg.wcs_y = machine->wcs_position[1];
  msg.wcs_z = machine->wcs_position[2];
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
}

void on_home_change(machine_interface_t *machine, void *user_data) {
  // Send homing status update
  led_status_sending();
  homed_msg_t msg;
  msg.type = MSG_TYPE_HOMED;
  msg.x_homed = machine->axes_homed[0];
  msg.y_homed = machine->axes_homed[1];
  msg.z_homed = machine->axes_homed[2];
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
}

void on_wcs_change(machine_interface_t *machine, void *user_data) {
  // Send WCS update
  led_status_sending();
  wcs_msg_t msg;
  msg.type = MSG_TYPE_WCS;
  msg.wcs = machine->wcs;
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
}

void on_feed_change(machine_interface_t *machine, void *user_data) {
  led_status_sending();
  feed_msg_t msg;
  msg.type = MSG_TYPE_FEED;
  msg.feed = machine->feed;
  msg.feed_req = machine->feed_req;
  msg.feed_multiplier = machine->feed_multiplier;
  remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
}

void on_sensors_change(machine_interface_t *machine, void *user_data) {
  if (!machine) return;

  // Serialize io_channels[] into an io_channels_payload_hdr_t + io_channel_wire_t[]
  // and broadcast to the pendant via MSG_SUB_TYPE_IO_CHANNELS.

  // Take local snapshots of the two fields that _rrf_rebuild_io_channels can
  // update from the async proc task concurrently.  Reading the pointer and the
  // count separately (without a lock) can yield a torn view; the NULL guard
  // below defends against the window where num_io_channels was already set to
  // the new non-zero value but io_channels hasn't been assigned yet (or was
  // just freed).
  mc_io_channel_t *channels = machine->io_channels;
  size_t n = machine->num_io_channels;
  if (n > 0 && !channels) {
    // Transient race: count updated but pointer not yet; skip this tick.
    return;
  }

  size_t payload_size = sizeof(io_channels_payload_hdr_t) +
                        n * sizeof(io_channel_wire_t);
  uint8_t *buf = (uint8_t *)malloc(payload_size);
  if (!buf) {
    LOGE(TAG, "on_sensors_change: OOM (%u bytes)", (unsigned)payload_size);
    return;
  }
  memset(buf, 0, payload_size);

  io_channels_payload_hdr_t *hdr = (io_channels_payload_hdr_t *)buf;
  hdr->count = (uint16_t)n;

  io_channel_wire_t *wire =
      (io_channel_wire_t *)(buf + sizeof(io_channels_payload_hdr_t));

  for (size_t i = 0; i < n; i++) {
    const mc_io_channel_t *ch = &channels[i];
    wire[i].source_index  = ch->source_index;
    wire[i].direction     = (uint8_t)ch->direction;
    wire[i].signal        = (uint8_t)ch->signal;
    wire[i].role          = (uint8_t)ch->role;
    wire[i].health        = (uint8_t)ch->health;
    wire[i].value         = ch->value;
    wire[i].setpoint      = ch->setpoint;
    wire[i].active        = ch->active ? 1 : 0;
    wire[i].setpoint_bool = ch->setpoint_bool ? 1 : 0;
    wire[i].min_value     = ch->min_value;
    wire[i].max_value     = ch->max_value;
    if (ch->name) {
      strncpy(wire[i].name, ch->name, sizeof(wire[i].name) - 1);
      wire[i].name[sizeof(wire[i].name) - 1] = '\0';
    }
    if (ch->unit) {
      strncpy(wire[i].unit, ch->unit, sizeof(wire[i].unit) - 1);
      wire[i].unit[sizeof(wire[i].unit) - 1] = '\0';
    }
  }

  led_status_sending();
  remote_wrapper_broadcast_fragmented_message(MSG_SUB_TYPE_IO_CHANNELS,
                                              buf, payload_size);
  free(buf);
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
      led_status_sending();
      remote_wrapper_send_fragmented_message(display_mac_address,
                          MSG_SUB_TYPE_MESSAGE_BOX, (const uint8_t*)msg_box_payload, payload_size);
      free(msg_box_payload);
    } else {
      LOGE(TAG, "Failed to serialize message box.");
      led_status_error();
    }
  } else {
    // Modal was dismissed on the hub side – tell the pendant to remove it too.
    LOGI(TAG, "Dialog dismissed, sending MSG_TYPE_DISMISS_MODAL to pendant.");
    dismiss_modal_msg_t msg;
    msg.type = MSG_TYPE_DISMISS_MODAL;
    msg.modal_id = -1;  // -1 means "dismiss whatever is currently shown"
    led_status_sending();
    remote_wrapper_send(display_mac_address, (const uint8_t *)&msg, sizeof(msg));
  }
}

void on_spindles_tools_change(machine_interface_t *machine, void *user_data) {
  led_status_sending();
  const char *tool = machine->tool ? machine->tool : "";
  int32_t rpm  = machine->spindles ? (int32_t)machine->spindles->rpm : 0;
  uint16_t tool_len = (uint16_t)(strlen(tool) & 0xFFFFu);

  // Diameter: encode as uint16_t × 100 (e.g. 6.35 mm → 635).
  uint16_t dia_x100 = (machine->tool_diameter_mm > 0.0f)
                        ? (uint16_t)(machine->tool_diameter_mm * 100.0f + 0.5f)
                        : 0u;
  uint8_t flutes = (uint8_t)(machine->tool_flute_count > 0
                              ? machine->tool_flute_count : 0);

  size_t total_len = SPINDLES_TOOLS_WIRE_HEADER_LEN + tool_len;
  uint8_t *buf = (uint8_t *)malloc(total_len);
  if (!buf) {
    LOGE(TAG, "Failed to allocate spindles_tools buffer");
    return;
  }

  size_t off = 0;
  // type
  buf[off++] = (uint8_t)MSG_TYPE_SPINDLES_TOOLS;

  // rpm (int32_t, LE)
  memcpy(buf + off, &rpm, sizeof(int32_t));
  off += sizeof(int32_t);

  // tool name length (uint16_t, LE)
  memcpy(buf + off, &tool_len, sizeof(uint16_t));
  off += sizeof(uint16_t);

  // diameter × 100 (uint16_t, LE)
  memcpy(buf + off, &dia_x100, sizeof(uint16_t));
  off += sizeof(uint16_t);

  // flute count (uint8_t)
  buf[off++] = flutes;

  // tool name bytes (no NUL)
  if (tool_len > 0) {
    memcpy(buf + off, tool, tool_len);
    off += tool_len;
  }

  remote_wrapper_send(display_mac_address, buf, total_len);
  free(buf);
}

void on_files_changed(machine_interface_t *mach, void *user_data,
                      const char *path, char **files) {
  LOGI(TAG, "Files change detected.");

  size_t idx = MAX_FILE_LISTS;
  for (size_t i = 0; i < MAX_FILE_LISTS; i++) {
    if (!mach->filelists[i].fdir) continue;  // NULL guard: uninitialized slot
    if (strcmp(mach->filelists[i].fdir, path) == 0) {
      idx = i;
      break;
    }
  }

  if (idx >= MAX_FILE_LISTS) {
    LOGE(TAG, "Cannot find file list for directory %s.", path);
    led_status_error();
    return;
  }

  LOGI(TAG, "Files present, attempting to serialize and send.");

  // Determine which files pointer to use: prefer the provided `files`
  // parameter (may be supplied by callers), otherwise fall back to the
  // stored slot in the machine struct. Guard against NULL.
  char **slot_files = files ? files : mach->filelists[idx].files;
  if (!slot_files) {
    LOGI(TAG, "No files present for '%s' (slot %u) — nothing to send.", path, (unsigned)idx);
    return;
  }

  // Compute size of all strings + header.
  file_list_payload_t *header;
  size_t total_size = sizeof(*header);
  total_size += strlen(path) + 1;
  size_t num_files = 0;
  size_t i = 0;
  while (slot_files[i] != NULL) {
    total_size += strlen(slot_files[i]) + 1;
    ++num_files;
    ++i;
  }

  uint8_t *buf = (uint8_t *)malloc(total_size);
  if (!buf) {
    LOGE(TAG, "on_files_changed: OOM (%u bytes)", (unsigned)total_size);
    return;
  }
  memset(buf, 0, total_size);

  header = (file_list_payload_t *)buf;
  header->total_size = total_size;
  header->num_files = num_files;
  char *offset = (char *)header + sizeof(*header);
  memcpy(offset, path, strlen(path) + 1);
  offset += strlen(path) + 1;

  i = 0;
  while (slot_files[i] != NULL) {
    size_t sz = strlen(slot_files[i]) + 1;
    memcpy(offset, slot_files[i], sz);
    offset += sz;
    ++i;
  }

  LOGI(TAG, "Serialized file list to %u bytes.", total_size);
  led_status_sending();
  if (!remote_wrapper_send_fragmented_message(display_mac_address, MSG_SUB_TYPE_FILE_LIST, buf, total_size)) {
    LOGI(TAG, "Failed to send binary file list to %u bytes.", total_size);
    led_status_error();
  }
  free(buf);
}

bool on_log_message_received(machine_interface_t *machine, void *user_data, const char *message) {
  // Default pointer to the original message; may be replaced for client
  // forwarding when we want to suppress large/unparseable JSON payloads.
  const char *msg_to_send = message;
  char short_msg[] = "Failed to parse RRF response";

  // If this looks like a raw M409/rr_model JSON response it means the hub
  // couldn't parse it into state. Log the raw JSON at error level for
  // diagnostics. Behavior for forwarding to clients is controlled by
  // M409_FAILED_JSON_MODE (see config.h):
  // 0 = send short message to client
  // 1 = log locally only, do NOT forward
  // 2 = log only on hub and do not forward
  if (message && message[0] == '{' && strstr(message, "\"key\"") != NULL) {
    LOGE(TAG, "Failed to parse machine model JSON response; raw: %s", message);
#if M409_FAILED_JSON_MODE == 0
    msg_to_send = short_msg;
#else
    // Don't forward to clients when configured to log-only.
    return false;
#endif
  }

  size_t len = strlen(msg_to_send) + 1; // Include null terminator

  LOGI(TAG, "Broadcasting log message: %s", msg_to_send);

  // Lazy init for lock and timer
  if (!log_batch_lock) {
    log_batch_lock = xSemaphoreCreateMutex();
  }

  if (!log_batch_timer) {
    const esp_timer_create_args_t args = {
      .callback = &log_batch_timer_cb,
      .arg = NULL,
      .name = "log_batch_timer"
    };
    esp_timer_create(&args, &log_batch_timer);
  }

  // Append to the batch buffer (thread-safe)
  if (log_batch_lock && xSemaphoreTake(log_batch_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    size_t mlen = strlen(msg_to_send);
    // If there's space, append with newline separator
    if (log_batch_len + mlen + 2 < LOG_BATCH_CAP) {
      if (log_batch_len > 0) {
        log_batch_buf[log_batch_len++] = '\n';
      }
      memcpy(&log_batch_buf[log_batch_len], msg_to_send, mlen);
      log_batch_len += mlen;
      log_batch_buf[log_batch_len] = '\0';
    } else {
      // No space: flush current batch synchronously, then start new batch
      xSemaphoreGive(log_batch_lock);
      log_batch_timer_cb(NULL);
      if (xSemaphoreTake(log_batch_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        // start new batch with this message
        size_t copy_len = (mlen < LOG_BATCH_CAP - 1) ? mlen : (LOG_BATCH_CAP - 1);
        memcpy(log_batch_buf, msg_to_send, copy_len);
        log_batch_len = copy_len;
        log_batch_buf[log_batch_len] = '\0';
      }
    }

    // Restart the coalescing timer to flush after the window
    if (log_batch_timer) {
      esp_timer_start_once(log_batch_timer, LOG_COALESCE_MS * 1000);
    }

    xSemaphoreGive(log_batch_lock);
  } else {
    // Failed to take lock: fallback to immediate send
    led_status_sending();
    if (len <= (REMOTE_COMMS_DATA_MAX - offsetof(log_msg_t, message))) {
      size_t total_len = offsetof(log_msg_t, message) + len;
      log_msg_t* msg = (log_msg_t*)malloc(total_len);
      if (msg) {
        msg->type = MSG_TYPE_LOG_MESSAGE;
        strncpy(msg->message, msg_to_send, len);
        msg->message[len-1] = '\0';
        remote_wrapper_send(display_mac_address, (const uint8_t*)msg, total_len);
        free(msg);
      }
    } else {
      remote_wrapper_send_fragmented_message(display_mac_address, MSG_SUB_TYPE_LOG_MESSAGE, (const uint8_t*)msg_to_send, len);
    }
  }

  return false; // hub does not 'handle' the message for client UI suppression
}


void on_connected_change(machine_interface_t *machine, void *user_data) {
  bool conn = machine->is_connected(machine);
  LOGI(TAG, "Machine connection state changed: %s", conn ? "connected" : "disconnected");

  if (conn) {
    // Rely on the machine interface's `is_connected()` result rather than
    // inspecting `last_response_ms` directly (poll back-off may delay responses).
    led_status_connected();
  } else {
    led_status_connecting();
    // Immediately notify clients that the hub has lost the machine so they
    // don't keep showing stale positions/status.
    status_msg_t msg;
    msg.type = MSG_TYPE_STATUS;
    msg.status = MACHINE_STATUS_WAITING_FOR_MACHINE;
    remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
    LOGI(TAG, "Broadcasted WAITING_FOR_MACHINE to clients.");
  }
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

static void process_modal_ok_cmd(const uint8_t *data, int data_len) {
  if (data_len < (int)sizeof(modal_ok_cmd_t)) { LOGE(TAG, "Invalid modal_ok len"); return; }
  const modal_ok_cmd_t *cmd = (const modal_ok_cmd_t *)data;
  LOGI(TAG, "Modal OK seq=%d", cmd->modal_id);
  machine_interface_modal_ok(g_machine_base, cmd->modal_id);
}

static void process_modal_cancel_cmd(const uint8_t *data, int data_len) {
  if (data_len < (int)sizeof(modal_cancel_cmd_t)) { LOGE(TAG, "Invalid modal_cancel len"); return; }
  const modal_cancel_cmd_t *cmd = (const modal_cancel_cmd_t *)data;
  LOGI(TAG, "Modal Cancel seq=%d", cmd->modal_id);
  machine_interface_modal_cancel(g_machine_base, cmd->modal_id);
}

static void process_modal_choice_cmd(const uint8_t *data, int data_len) {
  if (data_len < (int)sizeof(modal_choice_cmd_t)) { LOGE(TAG, "Invalid modal_choice len"); return; }
  const modal_choice_cmd_t *cmd = (const modal_choice_cmd_t *)data;
  LOGI(TAG, "Modal Choice seq=%d choice=%d", cmd->modal_id, cmd->choice);
  machine_interface_modal_choice(g_machine_base, cmd->choice, cmd->modal_id);
}

static void process_modal_int_cmd(const uint8_t *data, int data_len) {
  if (data_len < (int)sizeof(modal_int_cmd_t)) { LOGE(TAG, "Invalid modal_int len"); return; }
  const modal_int_cmd_t *cmd = (const modal_int_cmd_t *)data;
  LOGI(TAG, "Modal Int seq=%d val=%d", cmd->modal_id, cmd->value);
  machine_interface_modal_int(g_machine_base, cmd->value, cmd->modal_id);
}

static void process_modal_float_cmd(const uint8_t *data, int data_len) {
  if (data_len < (int)sizeof(modal_float_cmd_t)) { LOGE(TAG, "Invalid modal_float len"); return; }
  const modal_float_cmd_t *cmd = (const modal_float_cmd_t *)data;
  LOGI(TAG, "Modal Float seq=%d val=%.3f", cmd->modal_id, cmd->value);
  machine_interface_modal_float(g_machine_base, cmd->value, cmd->modal_id);
}

static void process_modal_str_cmd(const uint8_t *data, int data_len) {
  if (data_len < (int)sizeof(modal_str_cmd_t)) { LOGE(TAG, "Invalid modal_str len"); return; }
  const modal_str_cmd_t *cmd = (const modal_str_cmd_t *)data;
  if (data_len < (int)(sizeof(modal_str_cmd_t) + cmd->len)) {
    LOGE(TAG, "Invalid modal_str len, incomplete data");
    return;
  }
  char val[cmd->len + 1];
  memcpy(val, cmd->value, cmd->len);
  val[cmd->len] = '\0';
  LOGI(TAG, "Modal Str seq=%d val=%s", cmd->modal_id, val);
  machine_interface_modal_str(g_machine_base, val, cmd->modal_id);
}

static void process_set_io_channel_cmd(const uint8_t *data, int data_len) {
  if (data_len < (int)sizeof(set_io_channel_cmd_t)) {
    LOGE(TAG, "Invalid set_io_channel len: %d", data_len);
    return;
  }
  const set_io_channel_cmd_t *cmd = (const set_io_channel_cmd_t *)data;
  LOGI(TAG, "set_io_channel ch=%u sp=%.2f bool=%d",
       cmd->ch_idx, (double)cmd->setpoint, cmd->setpoint_bool);
  machine_interface_set_io_channel(g_machine_base, cmd->ch_idx,
                                   cmd->setpoint, (bool)cmd->setpoint_bool);
}

// --- ESP-NOW Callbacks ---

void on_remote_data_sent(const uint8_t *mac_addr, int status, void *user_data) {
  LOGV(TAG, "ESP-NOW send status: %s", status == 0 ? "success" : "fail");
  // If sending to the display failed, mark that we need to re-send filelists
  // when the display becomes reachable again.
  if (memcmp(mac_addr, display_mac_address, 6) == 0) {
    if (status != 0) {
      LOGW(TAG, "ESP-NOW send to display failed; will resend filelists on reconnect");
      g_sent_filelists_to_display = false;
      led_status_error();
      return;
    }
    // Send succeeded to the display. If we haven't yet sent the initial
    // file lists (or a reconnect reset the flag), push them now so the
    // client gets up-to-date views.
    if (!g_sent_filelists_to_display && g_machine_base) {
      for (size_t i = 0; i < MAX_FILE_LISTS; ++i) {
        const char *p = g_machine_base->filelists[i].fdir;
        if (!p) continue;
        on_files_changed(g_machine_base, NULL, p, g_machine_base->filelists[i].files);
      }
      g_sent_filelists_to_display = true;
      LOGI(TAG, "Sent initial filelists to display after successful send.");
    }
  }
}

void process_message(const uint8_t *data, const size_t data_len) {
  // Prioritise user-initiated gcodes over background poll M409 commands:
  // while processing a pendant command any enqueued gcode goes to the
  // front of the send queue (xQueueSendToFront) so it is transmitted
  // before the next pending poll.  The flag is cleared at function exit.
  if (g_machine_base) g_machine_base->gcode_queue_priority = true;
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
    case CMD_TYPE_PENDANT_CONNECT:
      LOGI(TAG, "<PENDANT_CONNECT>");
      // Pendant explicitly signalled connection. Mark filelists as not sent
      // so they will be resent soon (on the next successful send callback).
      g_sent_filelists_to_display = false;
      LOGI(TAG, "Pendant connect received; marked filelists unsent for resend.");
      break;
    case CMD_TYPE_PROBE:
      LOGI(TAG, "<PROBE_CMD>");
      process_probe_cmd(data, data_len);
      break;
    case CMD_TYPE_MODAL_OK:
      LOGI(TAG, "<MODAL_OK>");
      process_modal_ok_cmd(data, data_len);
      break;
    case CMD_TYPE_MODAL_CANCEL:
      LOGI(TAG, "<MODAL_CANCEL>");
      process_modal_cancel_cmd(data, data_len);
      break;
    case CMD_TYPE_MODAL_CHOICE:
      LOGI(TAG, "<MODAL_CHOICE>");
      process_modal_choice_cmd(data, data_len);
      break;
    case CMD_TYPE_MODAL_INT:
      LOGI(TAG, "<MODAL_INT>");
      process_modal_int_cmd(data, data_len);
      break;
    case CMD_TYPE_MODAL_FLOAT:
      LOGI(TAG, "<MODAL_FLOAT>");
      process_modal_float_cmd(data, data_len);
      break;
    case CMD_TYPE_MODAL_STR:
      LOGI(TAG, "<MODAL_STR>");
      process_modal_str_cmd(data, data_len);
      break;
    case CMD_TYPE_SET_IO_CHANNEL:
      LOGI(TAG, "<SET_IO_CHANNEL>");
      process_set_io_channel_cmd(data, data_len);
      break;

    default:
      LOGW(TAG, "Unknown command type: %d", command_type);
      break;
  }
  if (g_machine_base) g_machine_base->gcode_queue_priority = false;
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

  // If this is the first message seen from the display, push the full
  // file lists to it even if they haven't changed so the client has
  // an up-to-date view after (re)connect.
  if (!g_sent_filelists_to_display && memcmp(mac_addr, display_mac_address, 6) == 0) {
    if (g_machine_base) {
      for (size_t i = 0; i < MAX_FILE_LISTS; ++i) {
        const char *p = g_machine_base->filelists[i].fdir;
        if (!p) continue;
        on_files_changed(g_machine_base, NULL, p, g_machine_base->filelists[i].files);
      }
      g_sent_filelists_to_display = true;
      LOGI(TAG, "Sent initial filelists to display after connect.");
    }
  }

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

  LOGI(TAG, "Received ESP-NOW data from " MACSTR ", len: %d",
      MAC2STR(mac_addr), data_len);
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
  // If this is the first message seen from the display, push the full
  // file lists to it even if they haven't changed so the client has
  // an up-to-date view after (re)connect.
  if (!g_sent_filelists_to_display && memcmp(mac_addr, display_mac_address, 6) == 0) {
    if (g_machine_base) {
      for (size_t i = 0; i < MAX_FILE_LISTS; ++i) {
        const char *p = g_machine_base->filelists[i].fdir;
        if (!p) continue;
        on_files_changed(g_machine_base, NULL, p, g_machine_base->filelists[i].files);
      }
      g_sent_filelists_to_display = true;
      LOGI(TAG, "Sent initial filelists to display after connect.");
    }
  }

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
  task_registry_register_handle(recv_task_handle, "remote_recv_task");
}

#endif

unsigned int ctr = 0;

// ---------------------------------------------------------------------------
// Compact machine state summary logger
// ---------------------------------------------------------------------------

static const char *status_str(machine_status_t s) {
  switch (s) {
    case MACHINE_STATUS_INITIALIZING:      return "INIT";
    case MACHINE_STATUS_FLASHING_FIRMWARE: return "FLASH";
    case MACHINE_STATUS_EMERGENCY_HALTED:  return "E-HALT";
    case MACHINE_STATUS_OFF:               return "OFF";
    case MACHINE_STATUS_PAUSED_DEC:        return "PAUSED-DEC";
    case MACHINE_STATUS_PAUSED_RESUME:     return "PAUSED-RESUME";
    case MACHINE_STATUS_PAUSED:            return "PAUSED";
    case MACHINE_STATUS_SIMULATING:        return "SIM";
    case MACHINE_STATUS_IDLE:              return "IDLE";
    case MACHINE_STATUS_TOOL_CHANGING:     return "TOOL-CHG";
    case MACHINE_STATUS_RUNNING:            return "RUNNING";
    case MACHINE_STATUS_WAITING_FOR_MACHINE: return "WAITING";
    default:                               return "UNKNOWN";
  }
}

static void log_machine_state_summary(machine_interface_t *m) {
  // Axis homed string: "XYZ", "XY-", etc.
  char homed[4] = {'-', '-', '-', '\0'};
  if (m->axes_homed[0]) homed[0] = 'X';
  if (m->axes_homed[1]) homed[1] = 'Y';
  if (m->axes_homed[2]) homed[2] = 'Z';

  // WCS label G54..G59
  char wcs_str[8];
  snprintf(wcs_str, sizeof(wcs_str), "G%d", 54 + m->wcs);

  // Spindle RPM (first spindle if present)
  int rpm = (m->spindles && m->num_spindles > 0) ? m->spindles[0].rpm : 0;

  // Tool string (guard NULL)
  const char *tool = (m->tool && m->tool[0]) ? m->tool : "-";

  bool conn = m->is_connected ? m->is_connected(m) : false;

  LOGI(TAG,
       "--- STATUS: %s  conn:%s  homed:%s  WCS:%s ---",
       status_str(m->machine_status), conn ? "Y" : "N", homed, wcs_str);
  LOGI(TAG,
       "    POS  M[ X:%.3f  Y:%.3f  Z:%.3f ]  W[ X:%.3f  Y:%.3f  Z:%.3f ]",
       m->position[0], m->position[1], m->position[2],
       m->wcs_position[0], m->wcs_position[1], m->wcs_position[2]);
  LOGI(TAG,
       "    FEED %.1f mm/min (req %.1f  x%.2f)  RPM:%d  tool:%s",
       m->feed, m->feed_req, m->feed_multiplier, rpm, tool);
}

void machine_poll_send_task_iter() {
  // Update LED status display
  led_status_task_update();

#ifndef ASYNC_GCODE_SENDING
  // Send queued g-code commands (non-async path only - in async mode
  // machine_send_task owns the gcode queue and the machine poll loop).
  machine_interface_process_gcode_q(&g_machine->base);

  // Poll the new machine state (non-async only - machine_send_task handles
  // this in async mode; calling it from a second task corrupts the shared
  // serial line_buffer).
  machine_interface_task_loop_iter(&g_machine->base);
#endif

  // Periodic compact state summary
  static unsigned int log_tick = 0;
  if (++log_tick >= STATE_LOG_INTERVAL_TICKS) {
    log_tick = 0;
    log_machine_state_summary(&g_machine->base);
  }

  machine_interface_t *mach = &g_machine->base;
  bool is_connected = mach->is_connected ? mach->is_connected(mach) : false;

  if (!is_connected) {
    // Hub is alive but has no connection to the CNC controller.
    // Periodically tell clients so they can show a meaningful state instead
    // of stale coordinates.  We piggyback on the keep-alive slot (iter == 0)
    // plus the status slot (iter == 1) of the round-robin to avoid flooding.
    const unsigned int iter = ctr++ % 5;
    if (iter == 0 || iter == 1) {
      status_msg_t smsg;
      smsg.type   = MSG_TYPE_STATUS;
      smsg.status = MACHINE_STATUS_WAITING_FOR_MACHINE;
      led_status_sending();
      remote_wrapper_send(display_mac_address, (uint8_t *)&smsg, sizeof(smsg));
    }
    if (iter == 2) {
      // Also send a keep-alive so the client knows the hub itself is running.
      keep_alive_msg_t ka;
      ka.type = MSG_TYPE_KEEP_ALIVE;
      led_status_sending();
      remote_wrapper_send(display_mac_address, (uint8_t *)&ka, sizeof(ka));
    }
    // Periodic diagnostic logging for serial issues (suppress flooding)
    static int diag_tick = 0;
    if (++diag_tick >= 50) {  // approx every 50 * HUB_POLL_INTERVAL_MS (~2.5 s)
      diag_tick = 0;
      if (g_machine) {
        if (g_machine->consecutive_parse_failures > 0) {
          LOGW(TAG, "Serial parse failures: %d",
               g_machine->consecutive_parse_failures);
        } else if (g_machine->last_response_ms == 0) {
          LOGW(TAG, "No serial responses received yet.");
        } else {
          unsigned long age = 0;
#ifdef ESP32_HW
          age = millis() - g_machine->last_response_ms;
#endif
          LOGW(TAG, "Last serial response %lu ms ago.", age);
        }
      }
    }
    return;
  }

  // --- Connected path: round-robin push of real machine state ---
  // Compressed from 10 slots to 5; each category is now refreshed every
  // 5 × 50 ms = 250 ms (was 10 × 120 ms = 1,200 ms).
  const unsigned int iter = ctr++ % 5;
  if (iter == 0) {
    // Slot 0: position (most time-sensitive)
    on_position_change(mach, mach);
  } else if (iter == 1) {
    // Slot 1: machine status + homed flags
    on_machine_state_change(mach, mach);
    on_home_change(mach, mach);
  } else if (iter == 2) {
    // Slot 2: feed rate + spindle RPM
    on_feed_change(mach, mach);
    on_spindles_tools_change(mach, mach);
  } else if (iter == 3) {
    // Slot 3: WCS + active dialogs
    on_wcs_change(mach, mach);
    on_dialogs_change(mach, mach);
  } else {
    // Slot 4: keep-alive + sensors (background)
    on_sensors_change(mach, mach);
    keep_alive_msg_t keep_alive_msg;
    keep_alive_msg.type = MSG_TYPE_KEEP_ALIVE;
    led_status_sending();
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
  LOGI(TAG, "Initialize ESP-NOW...");
  if (!remote_wrapper_init(on_remote_data_recv, on_remote_data_sent, NULL)) {
    LOGI(TAG, "Failed to initialize ESP-NOW");
    led_status_error();  // Signal error with red LED
    vTaskDelete(NULL);
    return;
  }
  LOGI(TAG, "Initialized ESP-NOW... DONE");

  // Add the display as a peer
  if (!remote_wrapper_add_peer(display_mac_address)) {
    LOGI(TAG, "Failed to add display as peer");
    led_status_error();  // Signal error with red LED
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
  // Initialize LED status indicators
  led_status_init();
  led_status_connecting();  // Start in connecting state
  
  // Initialize the machine interface.
  // Use UART1 (not UART0) to avoid conflicting with the IDF/Arduino framework's
  // UART0 console, which is initialized at 115200 before app code runs.  On
  // ESP32-S3 the GPIO matrix lets any UART use any pin, so MACH_UART_PIN_TX/RX
  // still work on UART1.
  g_machine = machine_rrf_create_serial(1, HUB_POLL_INTERVAL_MS, MACH_UART_PIN_TX,
                                 MACH_UART_PIN_RX);  // Use UART 1
  if (!g_machine) {
    LOGI(TAG, "Failed to create machine interface");
    led_status_error();  // Signal error with red LED
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

  // Pre-initialize filelist directory slots so the M20 parser and
  // on_files_changed can find them by name.  Also register the callback that
  // serialises and forwards file lists to the pendant over ESP-NOW.
  mach->filelists[0].fdir = strdup("gcodes");
  mach->filelists[1].fdir = strdup("macros");
  machine_interface_add_files_changed_cb(mach, "gcodes", mach, on_files_changed);
  machine_interface_add_files_changed_cb(mach, "macros", mach, on_files_changed);


  remote_recv_task_run();
}

TaskHandle_t hub_task_handle = NULL;

void setup_hub_tasks() {
  xTaskCreatePinnedToCore(hub_task, "hub_task", 12 * 1024, NULL,
                          HUB_TASK_PRIORITY, &hub_task_handle, 0);
  task_registry_register_handle(hub_task_handle, "hub_task");
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
                                               TASK_MACHINE_CORE,
                                               14 * 1024)) {  // cJSON + on_files_changed + ESP-NOW send
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
