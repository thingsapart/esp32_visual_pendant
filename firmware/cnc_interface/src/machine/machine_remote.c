// machine_remote.c

#include "machine_remote.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "driver/remote_comms_wrapper.h"  // For ESP-NOW communication

#ifdef ASYNC_RESPONSE_PROCESSING
#include "tasks/machine_response_proc_task.h"
#endif

#define UI_DEBUG_LOG D_WARN
#include "debug.h"

static const char *TAG = "machine_remote";

// Messages > MAX_PAYLOAD length need to be sent in multiple fragments as
// separate messages and thus reassembled here.

#ifndef MAX_CONCURRENT_FRAGMENTED_MSGS
// Max fragmented binary messages being reassembled at once. Increase default
// from 2 to 4 to reduce the probability of overwriting in-progress
// reassemblies when multiple large messages are in flight.
#define MAX_CONCURRENT_FRAGMENTED_MSGS 4
#endif

// Structure to hold state for one in-progress fragmentede msg reassembly.
typedef struct {
  bool is_valid;             // Is this slot currently in use?
  uint16_t seq_id;           // Sequence ID of the message being reassembled
  uint8_t sub_type;          // Payload sub-type (e.g., message box, file list)
  uint32_t total_size;       // Expected total size of the final payload
  uint16_t total_fragments;  // Expected total number of fragments
  uint8_t *buffer;           // Allocated buffer for the complete payload
  uint16_t fragments_received_count;  // How many fragments have arrived so far
  bool *fragments_received_mask;  // Bitmap (or bool array) tracking received
                                  // fragments
  uint32_t last_active_tick; // Monotonic activity tick for LRU eviction
} binary_payload_buffer_t;

// --- Global State for Reassembly ---
static binary_payload_buffer_t
  g_binary_payload_buffers[MAX_CONCURRENT_FRAGMENTED_MSGS];
static size_t g_next_binary_buffer_slot =
  0;  // Index for next allocation/overwrite
static uint32_t g_reassembly_tick = 0; // Incremented on activity for LRU

// --- Forward Declarations ---
static void _machine_interface_remote_send_gcode(machine_interface_t *self,
                                                 const char *gcode,
                                                 uint32_t poll_state);
static bool _machine_interface_remote_is_connected(machine_interface_t *self);
static void _machine_interface_remote_list_files(machine_interface_t *self,
                                                 const char *path);
static void _machine_interface_remote_run_macro(machine_interface_t *self,
                                                const char *macro_name);
static void _machine_interface_remote_start_job(machine_interface_t *self,
                                                const char *job_name);
static void _machine_interface_remote_move_continuous(machine_interface_t *self,
                                                      const char axis,
                                                      float feed,
                                                      int direction);
static void _machine_interface_remote_move_continuous_stop(
    machine_interface_t *self);
static void _machine_interface_remote_move(machine_interface_t *self,
                                           const char axis, float feed,
                                           float value);
static void _machine_interface_remote_home_all(machine_interface_t *self);
static void _machine_interface_remote_home(machine_interface_t *self,
                                           const char *axes);
static void _machine_interface_remote_set_wcs(machine_interface_t *self,
                                              int wcs);
static void _machine_interface_remote_set_wcs_zero(machine_interface_t *self,
                                                   int wcs, const char *axes);
static void _machine_interface_remote_next_wcs(machine_interface_t *self);
static void _machine_interface_remote_probe(machine_interface_t *self,
                                            const char *probe_gcode);
static void _machine_interface_remote_modal_ok(machine_interface_t *self,
                                               int modal_id);
static void _machine_interface_remote_modal_cancel(machine_interface_t *self,
                                                   int modal_id);
static void _machine_interface_remote_modal_choice(machine_interface_t *self,
                                                   int choice, int modal_id);
static void _machine_interface_remote_modal_int(machine_interface_t *self,
                                                int val, int modal_id);
static void _machine_interface_remote_modal_float(machine_interface_t *self,
                                                  float val, int modal_id);
static void _machine_interface_remote_modal_str(machine_interface_t *self,
                                                const char *val, int modal_id);
static void _machine_interface_remote_update_machine_state(
    machine_interface_t *self, uint32_t poll_state);

static void process_binary_payload(machine_interface_t *self, uint8_t sub_type,
                                   uint8_t *data, size_t size);
static void binary_payload_slot_cleanup(size_t index);
static void initialize_binary_payload_buffers();
static void binary_message_fragment_to_buffer(machine_interface_remote_t *self,
                                              const uint8_t *data, size_t len);

void *message_box_t_to_payload(const message_box_t *msg_box, size_t *out_size);
message_box_t *message_box_t_from_payload(const void *payload,
                                          message_box_t *const msg_box);

static void _send_command(machine_interface_remote_t *self, const uint8_t *data,
                          size_t len) {
  if (!remote_wrapper_send(self->hub_mac_address, data, len)) {
    LOGE(TAG, "Failed to send command via ESP-NOW");
  }
}

static void _machine_interface_remote_send_gcode(machine_interface_t *self,
                                                 const char *gcode,
                                                 uint32_t poll_state) {
  // Use stack buffer to avoid malloc overhead
  #ifndef MAX_GCODE_STR_LEN
  #define MAX_GCODE_STR_LEN 256
  #endif
  
  uint8_t buf[sizeof(send_gcode_cmd_t) + MAX_GCODE_STR_LEN];
  send_gcode_cmd_t *cmd = (send_gcode_cmd_t *)buf;
  
  size_t gcode_len = strlen(gcode);
  if (gcode_len >= MAX_GCODE_STR_LEN) {
      LOGW(TAG, "GCode too long, truncating");
      gcode_len = MAX_GCODE_STR_LEN - 1;
  }
  
  size_t len = sizeof(send_gcode_cmd_t) + gcode_len + 1;

  cmd->type = CMD_TYPE_SEND_GCODE;
  cmd->len = gcode_len;
  memcpy(cmd->gcode, gcode, gcode_len);
  cmd->gcode[gcode_len] = '\0';
  
  LOGI(TAG, "Sending '%s'", cmd->gcode);
  _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
}

static bool _machine_interface_remote_is_connected(machine_interface_t *self) {
  machine_interface_remote_t *mach = (machine_interface_remote_t *)self;
  // TODO: Timeout connection.
  return mach->hub_mac_received;
}

static void _machine_interface_remote_list_files(machine_interface_t *self,
                                                 const char *path) {
  size_t len = sizeof(list_files_cmd_t) + strlen(path) + 1;
  list_files_cmd_t *cmd = (list_files_cmd_t *)malloc(len);
  if (!cmd) {
    LOGE(TAG, "Failed to allocate list files");
    return;
  }
  cmd->type = CMD_TYPE_LIST_FILES;
  cmd->len = strlen(path);
  strcpy(cmd->path, path);
  _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
  free(cmd);
}

static void _machine_interface_remote_run_macro(machine_interface_t *self,
                                                const char *macro_name) {
  size_t len = sizeof(run_macro_cmd_t) + strlen(macro_name) + 1;
  run_macro_cmd_t *cmd = (run_macro_cmd_t *)malloc(len);
  if (!cmd) {
    LOGE(TAG, "Failed to allocate");
    return;
  }
  cmd->type = CMD_TYPE_RUN_MACRO;
  cmd->len = strlen(macro_name);
  strcpy(cmd->macro_name, macro_name);
  _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
  free(cmd);
}

static void _machine_interface_remote_start_job(machine_interface_t *self,
                                                const char *job_name) {
  size_t len = sizeof(start_job_cmd_t) + strlen(job_name) + 1;
  start_job_cmd_t *cmd = (start_job_cmd_t *)malloc(len);
  if (!cmd) {
    LOGE(TAG, "Failed to allocate start_job cmd");
    return;
  }
  cmd->type = CMD_TYPE_START_JOB;
  cmd->len = strlen(job_name);
  strcpy(cmd->job_name, job_name);
  _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
  free(cmd);
}

static void _machine_interface_remote_move_continuous(machine_interface_t *self,
                                                      const char axis,
                                                      float feed,
                                                      int direction) {
  move_cont_cmd_t cmd;
  cmd.type = CMD_TYPE_MOVE_CONT;
  cmd.axis = axis;
  cmd.feed = feed;
  cmd.direction = direction;
  _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_move_continuous_stop(
    machine_interface_t *self) {
  move_cont_stop_cmd_t cmd;
  cmd.type = CMD_TYPE_MOVE_CONT_STOP;
  _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_move(machine_interface_t *self,
                                           const char axis, float feed,
                                           float value) {
  move_cmd_t cmd;
  cmd.type = CMD_TYPE_MOVE;
  cmd.axis = axis;
  cmd.feed = feed;
  cmd.value = value;
  _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_home_all(machine_interface_t *self) {
  home_all_cmd_t cmd;
  cmd.type = CMD_TYPE_HOME_ALL;
  _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd,
                sizeof(cmd));
}
static void _machine_interface_remote_home(machine_interface_t *self,
                                           const char *axes) {
  size_t len = sizeof(home_cmd_t) + strlen(axes) + 1;
  home_cmd_t *cmd = (home_cmd_t *)malloc(len);
  if (!cmd) {
    LOGE(TAG, "Failed to allocate");
    return;
  }
  cmd->type = CMD_TYPE_HOME;
  cmd->axes_len = strlen(axes);
  strcpy(cmd->axes, axes);
  _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
  free(cmd);
}

static void _machine_interface_remote_set_wcs(machine_interface_t *self,
                                              int wcs) {
  set_wcs_cmd_t cmd;
  cmd.type = CMD_TYPE_SET_WCS;
  cmd.wcs = wcs;
  _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_set_wcs_zero(machine_interface_t *self,
                                                   int wcs, const char *axes) {
  size_t len = sizeof(set_wcs_zero_cmd_t) + strlen(axes) + 1;
  set_wcs_zero_cmd_t *cmd = (set_wcs_zero_cmd_t *)malloc(len);
  if (!cmd) {
    LOGE(TAG, "Failed to allocate");
    return;
  }
  cmd->type = CMD_TYPE_SET_WCS_ZERO;
  cmd->wcs = wcs;
  cmd->axes_len = strlen(axes);
  strcpy(cmd->axes, axes);
  _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
  free(cmd);
}

static void _machine_interface_remote_next_wcs(machine_interface_t *self) {
  next_wcs_cmd_t cmd;
  cmd.type = CMD_TYPE_NEXT_WCS;
  _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_probe(machine_interface_t *self,
                                            const char *probe_gcode) {
  size_t len = sizeof(probe_cmd_t) + strlen(probe_gcode) + 1;
  probe_cmd_t *cmd = (probe_cmd_t *)malloc(len);
  if (!cmd) {
    LOGE(TAG, "Failed to allocate");
    return;
  }
  cmd->type = CMD_TYPE_PROBE;
  cmd->len = strlen(probe_gcode);
  strcpy(cmd->gcode, probe_gcode);
  _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
  free(cmd);
}

// --- Modal response implementations ---
// Each function dismisses the local modal immediately, then sends the
// appropriate response command back to the hub.

static void _dismiss_local_modal(machine_interface_t *self, int modal_id) {
  if (self->message_box && self->message_box->seq == modal_id) {
    free_message_box_t(self->message_box);
    self->message_box = NULL;
    machine_interface_dialogs_updated(self);
  }
}

static void _machine_interface_remote_modal_ok(machine_interface_t *self,
                                               int modal_id) {
  _dismiss_local_modal(self, modal_id);
  modal_ok_cmd_t cmd;
  cmd.type = CMD_TYPE_MODAL_OK;
  cmd.modal_id = modal_id;
  _send_command((machine_interface_remote_t *)self, (const uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_modal_cancel(machine_interface_t *self,
                                                   int modal_id) {
  _dismiss_local_modal(self, modal_id);
  modal_cancel_cmd_t cmd;
  cmd.type = CMD_TYPE_MODAL_CANCEL;
  cmd.modal_id = modal_id;
  _send_command((machine_interface_remote_t *)self, (const uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_modal_choice(machine_interface_t *self,
                                                   int choice, int modal_id) {
  _dismiss_local_modal(self, modal_id);
  modal_choice_cmd_t cmd;
  cmd.type = CMD_TYPE_MODAL_CHOICE;
  cmd.modal_id = modal_id;
  cmd.choice = choice;
  _send_command((machine_interface_remote_t *)self, (const uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_modal_int(machine_interface_t *self,
                                                int val, int modal_id) {
  _dismiss_local_modal(self, modal_id);
  modal_int_cmd_t cmd;
  cmd.type = CMD_TYPE_MODAL_INT;
  cmd.modal_id = modal_id;
  cmd.value = val;
  _send_command((machine_interface_remote_t *)self, (const uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_modal_float(machine_interface_t *self,
                                                  float val, int modal_id) {
  _dismiss_local_modal(self, modal_id);
  modal_float_cmd_t cmd;
  cmd.type = CMD_TYPE_MODAL_FLOAT;
  cmd.modal_id = modal_id;
  cmd.value = val;
  _send_command((machine_interface_remote_t *)self, (const uint8_t *)&cmd,
                sizeof(cmd));
}

static void _machine_interface_remote_modal_str(machine_interface_t *self,
                                                const char *val, int modal_id) {
  _dismiss_local_modal(self, modal_id);
  size_t val_len = val ? strlen(val) : 0;
  size_t cmd_len = sizeof(modal_str_cmd_t) + val_len + 1;
  modal_str_cmd_t *cmd = (modal_str_cmd_t *)malloc(cmd_len);
  if (!cmd) {
    LOGE(TAG, "Failed to allocate modal_str_cmd");
    return;
  }
  cmd->type = CMD_TYPE_MODAL_STR;
  cmd->modal_id = modal_id;
  cmd->len = (uint16_t)val_len;
  if (val) memcpy(cmd->value, val, val_len);
  cmd->value[val_len] = '\0';
  _send_command((machine_interface_remote_t *)self, (const uint8_t *)cmd,
                cmd_len);
  free(cmd);
}

void _machine_interface_remote_set_connected(machine_interface_t *self,
                                             bool connected) {
  machine_interface_remote_t *rself = (machine_interface_remote_t *)self;
  if (!connected) {
    rself->hub_mac_received = false;
  }
  machine_interface_connected_updated(self);
}

static void _machine_interface_remote_set_io_channel(machine_interface_t *self,
                                                      uint8_t ch_idx,
                                                      float setpoint,
                                                      bool setpoint_bool) {
  set_io_channel_cmd_t cmd;
  cmd.type          = CMD_TYPE_SET_IO_CHANNEL;
  cmd.ch_idx        = ch_idx;
  cmd.setpoint      = setpoint;
  cmd.setpoint_bool = setpoint_bool ? 1 : 0;
  _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd,
                sizeof(cmd));
}

// --- Constructor/Destructor ---

machine_interface_remote_t *machine_interface_remote_create(
    const uint8_t *hub_mac) {
  machine_interface_remote_t *self =
      (machine_interface_remote_t *)malloc(sizeof(machine_interface_remote_t));

  if (!self) {
    LOGE(TAG, "Failed to allocate memory for machine_interface_remote");
    return NULL;
  }
  return machine_interface_remote_init(self, hub_mac);
}

static void _machi_remote_esp_now_data_recv(const uint8_t *mac_addr,
                                            const uint8_t *data, int data_len,
                                            void *user_data);
static void _machi_remote_esp_now_data_sent(const uint8_t *mac_addr, int status,
                                            void *user_data);
static void _machine_interface_remote_process_state(machine_interface_t *self,
                                                    void *data, size_t len);

machine_interface_remote_t *machine_interface_remote_init(
    machine_interface_remote_t *self, const uint8_t *hub_mac) {
  // Initialize base class (important!)
  machine_interface_init(&self->base, 100);  // No internal processing loop

  // Copy the hub's MAC address
  memcpy(self->hub_mac_address, hub_mac, 6);
  self->hub_mac_received = false;

#ifdef ASYNC_RESPONSE_PROCESSING
  self->proc_task_event_queue = NULL;
#endif

  // Override base class methods with remote-specific implementations
  self->base.send_gcode = _machine_interface_remote_send_gcode;
  self->base.is_connected =
      _machine_interface_remote_is_connected;  // always connected.
  self->base.list_files = _machine_interface_remote_list_files;
  self->base.run_macro = _machine_interface_remote_run_macro;
  self->base.start_job = _machine_interface_remote_start_job;
  self->base.move_continuous = _machine_interface_remote_move_continuous;
  self->base._continuous_move =
      _machine_interface_remote_move_continuous;  // use same.
  self->base.move_continuous_stop =
      _machine_interface_remote_move_continuous_stop;
  self->base._continuous_stop =
      _machine_interface_remote_move_continuous_stop;  // use same
  self->base.move = _machine_interface_remote_move;
  self->base.home_all = _machine_interface_remote_home_all;
  self->base.home = _machine_interface_remote_home;
  self->base.set_wcs = _machine_interface_remote_set_wcs;
  self->base.set_wcs_zero = _machine_interface_remote_set_wcs_zero;
  self->base.next_wcs = _machine_interface_remote_next_wcs;
  self->base.probe = _machine_interface_remote_probe;
  self->base.modal_ok = _machine_interface_remote_modal_ok;
  self->base.modal_cancel = _machine_interface_remote_modal_cancel;
  self->base.modal_choice = _machine_interface_remote_modal_choice;
  self->base.modal_int = _machine_interface_remote_modal_int;
  self->base.modal_float = _machine_interface_remote_modal_float;
  self->base.modal_str = _machine_interface_remote_modal_str;
  self->base.process_machine_state_response =
      _machine_interface_remote_process_state;
  self->base.set_connected = _machine_interface_remote_set_connected;

  // Disable methods not implemented on the remote
  self->base._update_machine_state =
      _machine_interface_remote_update_machine_state;
  self->base.debug_print = NULL;
  self->base.set_io_channel = _machine_interface_remote_set_io_channel;

  // Pre-initialize the two standard filelist slots so that incoming
  // MSG_SUB_TYPE_FILE_LIST payloads are always matched to a named slot,
  // and so that interface_tick() can build Job.items / Macro.items strings
  // even before the first request is answered.
  self->base.filelists[0].fdir = strdup("gcodes");
  self->base.filelists[0].files = NULL;
  self->base.filelists[1].fdir = strdup("macros");
  self->base.filelists[1].files = NULL;

  initialize_binary_payload_buffers();

  // Initialize ESP-NOW
  if (!remote_wrapper_init(_machi_remote_esp_now_data_recv,
                           _machi_remote_esp_now_data_sent, self)) {
    LOGE(TAG, "Failed to initialize ESP-NOW", TAG);
  }

  // Add the display as a peer
  uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (!remote_wrapper_add_peer(broadcast_mac)) {
    LOGE(TAG, "Failed to add display as peer", TAG);
  }

  LOGI(TAG, "MACHINE_REMOTE INIT", TAG);

  return self;
}

void machine_interface_remote_deinit(machine_interface_remote_t *self) {
  for (size_t i = 0; i < MAX_CONCURRENT_FRAGMENTED_MSGS; ++i) {
    binary_payload_slot_cleanup(i);
  }

  // Clean up any remote-specific resources
  machine_interface_deinit(&self->base);
}

void machine_interface_remote_destroy(machine_interface_remote_t *self) {
  if (self) {
    machine_interface_remote_deinit(self);

    free(self);
  }
}

void _machine_interface_remote_update_machine_state(
    machine_interface_t *base_self, uint32_t poll_state) {
  machine_interface_remote_t *self = (machine_interface_remote_t *)base_self;
  machine_interface_remote_process_messages(self);

  // Periodically request file-listings from the hub.  The base class sets
  // LIST_FILES | LIST_MACROS every ~9973 ticks; consume those bits here and
  // send the corresponding CMD_TYPE_LIST_FILES commands so the hub will run
  // M20 and return the current file-list over ESP-NOW.
  if (self->hub_mac_received) {
    if (poll_state & LIST_FILES) {
      LOGI(TAG, "Requesting file list: gcodes");
      _machine_interface_remote_list_files(base_self, "gcodes");
    }
    if (poll_state & LIST_MACROS) {
      LOGI(TAG, "Requesting file list: macros");
      _machine_interface_remote_list_files(base_self, "macros");
    }
  }
}

void machine_interface_remote_buffer_message(machine_interface_remote_t *self,
                                             const uint8_t *data,
                                             size_t data_len) {
  // Check if message is too large ONLY for non-binary messages if using fixed
  // buffer
  if (data[0] != MSG_TYPE_BINARY &&
      data_len > sizeof(remote_msg_t)) {  // sizeof(remote_msg_t) is max size
                                          // for simple types
    LOGW(TAG,
         "Non-binary remote message too large (%zu bytes) to buffer. "
         "Discarding.",
         data_len);
    return;
  }

  // Process binary message fragments to buffer directly.
  if (data[0] == MSG_TYPE_BINARY) {
    LOGT(TAG, "Processing binary fragment directly (not buffering).");
    binary_message_fragment_to_buffer(self, data, data_len);

    return;
  }

  if (self->msg_buf_len >= MAX_MSG_BUFFERED) {
    LOGW(TAG,
         "Message buffer full (%d). Discarding incoming non-binary message.",
         MAX_MSG_BUFFERED);
    return;
  }
  size_t len_idx = self->msg_buf_len;
  self->msg_buf_len = len_idx + 1;
  // Use the correct size check based on the union
  assert(data_len <= sizeof(remote_msg_t) ||
         "Remote message too large to buffer.");
  self->msg_buffer_msg_len[len_idx] = data_len;
  memcpy(&self->msg_buffer[len_idx], data, data_len);
  LOGD(TAG, "Buffered non-binary message type %u (index %zu)", data[0],
       len_idx);
}

static void binary_message_fragment_to_buffer(machine_interface_remote_t *self,
                                              const uint8_t *data, size_t len) {
  if (len < BINARY_FRAGMENT_MSG_HEADER_SIZE) {
    LOGW(TAG,
         "Received truncated binary fragment message (len %zu < header %zu).",
         len, BINARY_FRAGMENT_MSG_HEADER_SIZE);
    return;
  }

  const binary_fragment_msg_t *frag_msg = (const binary_fragment_msg_t *)data;

  // Validate data length reported in header matches received length
  if (len != BINARY_FRAGMENT_MSG_HEADER_SIZE + frag_msg->fragment_len) {
    LOGW(TAG, "Binary fragment length mismatch: header says %u, received %zu.",
         frag_msg->fragment_len, len - BINARY_FRAGMENT_MSG_HEADER_SIZE);
    return;
  }

  LOGT(TAG,
       "Received Binary Fragment: Seq=%u, Frag=%u/%u, Offset=%u, Len=%u, "
       "SubType=%u",
       frag_msg->seq_id, frag_msg->fragment_index, frag_msg->total_fragments,
       frag_msg->fragment_offset, frag_msg->fragment_len, frag_msg->sub_type);

  // Find existing buffer for this sequence ID
  int target_slot = -1;
  for (size_t i = 0; i < MAX_CONCURRENT_FRAGMENTED_MSGS; ++i) {
    if (g_binary_payload_buffers[i].is_valid &&
        g_binary_payload_buffers[i].seq_id == frag_msg->seq_id) {
      // Basic validation: check if fragment details match the ongoing
      // reassembly
      if (g_binary_payload_buffers[i].total_size !=
              frag_msg->total_payload_size ||
          g_binary_payload_buffers[i].total_fragments !=
              frag_msg->total_fragments ||
          g_binary_payload_buffers[i].sub_type != frag_msg->sub_type) {
        LOGW(TAG,
             "Fragment Seq %u mismatch with ongoing reassembly slot %zu. "
             "Discarding fragment.",
             frag_msg->seq_id, i);
        return;  // Inconsistent fragment, ignore it
      }
      target_slot = i;
      break;
    }
  }

  // If no existing buffer, try to allocate a new one. Prefer an unused slot
  // if available; only overwrite when all slots are busy.
  if (target_slot == -1) {
    // Search for a free slot first
    int free_slot = -1;
    for (size_t i = 0; i < MAX_CONCURRENT_FRAGMENTED_MSGS; ++i) {
      if (!g_binary_payload_buffers[i].is_valid) {
        free_slot = (int)i;
        break;
      }
    }

    if (free_slot != -1) {
      target_slot = free_slot;
    } else {
      // No free slot: pick the least-recently-used (oldest) slot for eviction
      uint32_t oldest_tick = 0xFFFFFFFFu;
      int oldest_idx = -1;
      for (size_t i = 0; i < MAX_CONCURRENT_FRAGMENTED_MSGS; ++i) {
        if (g_binary_payload_buffers[i].last_active_tick < oldest_tick) {
          oldest_tick = g_binary_payload_buffers[i].last_active_tick;
          oldest_idx = (int)i;
        }
      }
      if (oldest_idx == -1) {
        // Fallback to circular if something unexpected happened
        target_slot = g_next_binary_buffer_slot;
      } else {
        target_slot = oldest_idx;
      }
      LOGW(TAG,
           "No free reassembly slots available; evicting slot %d (Seq ID: %u) for new Seq ID %u.",
           target_slot, g_binary_payload_buffers[target_slot].seq_id,
           frag_msg->seq_id);
      binary_payload_slot_cleanup(target_slot);
    }

    LOGI(TAG,
         "New binary sequence %u detected. Allocating in slot %d.",
         frag_msg->seq_id, target_slot);

    // Allocate main buffer
    g_binary_payload_buffers[target_slot].buffer =
        (uint8_t *)malloc(frag_msg->total_payload_size);
    if (!g_binary_payload_buffers[target_slot].buffer) {
      LOGE(TAG, "Failed to allocate %u bytes for reassembly buffer (Seq %u).",
           frag_msg->total_payload_size, frag_msg->seq_id);
      return;
    }

    // Allocate fragment received mask
    g_binary_payload_buffers[target_slot].fragments_received_mask =
        (bool *)calloc(frag_msg->total_fragments, sizeof(bool));
    if (!g_binary_payload_buffers[target_slot].fragments_received_mask) {
      LOGE(TAG,
           "Failed to allocate fragment mask (%u bools) for reassembly (Seq %u).",
           frag_msg->total_fragments, frag_msg->seq_id);
      free(g_binary_payload_buffers[target_slot].buffer);
      g_binary_payload_buffers[target_slot].buffer = NULL;
      return;
    }

    // Initialize the new slot
    g_binary_payload_buffers[target_slot].is_valid = true;
    g_binary_payload_buffers[target_slot].seq_id = frag_msg->seq_id;
    g_binary_payload_buffers[target_slot].sub_type = frag_msg->sub_type;
    g_binary_payload_buffers[target_slot].total_size =
      frag_msg->total_payload_size;
    g_binary_payload_buffers[target_slot].total_fragments =
      frag_msg->total_fragments;
    g_binary_payload_buffers[target_slot].fragments_received_count = 0;
    g_binary_payload_buffers[target_slot].last_active_tick = ++g_reassembly_tick;

    // Move to the next slot for compatibility with older logic
    g_next_binary_buffer_slot =
      (g_next_binary_buffer_slot + 1) % MAX_CONCURRENT_FRAGMENTED_MSGS;

    LOGI(TAG, "Allocated slot %d for Seq %u (Total Size: %u, Fragments: %u)",
         target_slot, frag_msg->seq_id, frag_msg->total_payload_size,
         frag_msg->total_fragments);
  }

  // At this point, target_slot should be valid index for the reassembly buffer
  binary_payload_buffer_t *target_buffer =
      &g_binary_payload_buffers[target_slot];

  // Validate fragment index and offset against the buffer state
  if (frag_msg->fragment_index >= target_buffer->total_fragments) {
    LOGW(TAG, "Invalid fragment index %u (>= %u) for Seq %u. Discarding.",
         frag_msg->fragment_index, target_buffer->total_fragments,
         frag_msg->seq_id);
    return;
  }
  if (frag_msg->fragment_offset + frag_msg->fragment_len >
      target_buffer->total_size) {
    LOGW(TAG,
         "Fragment offset+len (%u + %u) exceeds total size (%u) for Seq %u. "
         "Discarding.",
         frag_msg->fragment_offset, frag_msg->fragment_len,
         target_buffer->total_size, frag_msg->seq_id);
    // This could potentially corrupt memory if we proceeded, definitely return.
    return;
  }

  // Check if this fragment has already been received
  if (!target_buffer->fragments_received_mask[frag_msg->fragment_index]) {
    LOGV(TAG, "Storing fragment %u for Seq %u into buffer at offset %u.",
         frag_msg->fragment_index, frag_msg->seq_id, frag_msg->fragment_offset);

    // Copy the fragment data into the correct position in the main buffer
    memcpy(target_buffer->buffer + frag_msg->fragment_offset, frag_msg->data,
           frag_msg->fragment_len);

    // Mark fragment as received
    target_buffer->fragments_received_mask[frag_msg->fragment_index] = true;
    target_buffer->fragments_received_count++;
    // Update LRU tick
    target_buffer->last_active_tick = ++g_reassembly_tick;

    LOGV(TAG, "Seq %u: Received %u / %u fragments.", target_buffer->seq_id,
         target_buffer->fragments_received_count,
         target_buffer->total_fragments);

    // Check if the message is complete
    if (target_buffer->fragments_received_count ==
        target_buffer->total_fragments) {
      LOGI(TAG, "Reassembly complete for Seq %u (SubType %u, Size %u bytes).",
           target_buffer->seq_id, target_buffer->sub_type,
           target_buffer->total_size);

      uint8_t msg[2] = {MSG_TYPE_BINARY, target_slot};

#ifdef ASYNC_RESPONSE_PROCESSING
      // Message done - post to task for actual processing.
      machine_response_proc_task_data_ready(self->proc_task_event_queue, msg,
                                            sizeof(msg), true);
#else
      // TODO:
#endif
    }
  } else {
    LOGV(TAG, "Received duplicate fragment %u for Seq %u. Ignoring.",
         frag_msg->fragment_index, frag_msg->seq_id);
  }
}

void machine_interface_remote_process_message(machine_interface_remote_t *self,
                                              const uint8_t *data, size_t len) {
  LOGD(TAG, "RECV: len %d t=%d d=%d:%d", len, data[0], data[1], data[2]);

  if (len == 0) {
    return;
  }

  // All messages start with a message type byte.
  uint8_t message_type = data[0];
  machine_interface_t *machine = &self->base;

  switch (message_type) {
    case MSG_TYPE_KEEP_ALIVE:
      // Handle keep-alive (could update a last-seen timestamp)
      LOGV(TAG, "keep-alive");
      break;

    case MSG_TYPE_POSITION: {
      if (len != sizeof(position_msg_t)) {
        LOGE(TAG, "Invalid position message length: %zu", len);
        return;
      }
      position_msg_t *msg = (position_msg_t *)data;

      // Update local machine state
      self->base.position[0] = msg->x;
      self->base.position[1] = msg->y;
      self->base.position[2] = msg->z;

      self->base.wcs_position[0] = msg->wcs_x;
      self->base.wcs_position[1] = msg->wcs_y;
      self->base.wcs_position[2] = msg->wcs_z;

      machine_interface_position_updated(&self->base);

      LOGD(TAG, "pos %.3f %.3f %.3f / wcs %.3f %.3f %.3f",
           machine->position[0], machine->position[1], machine->position[2],
           machine->wcs_position[0], machine->wcs_position[1],
           machine->wcs_position[2]);

      break;
    }
    case MSG_TYPE_WCS: {
      if (len != sizeof(wcs_msg_t)) {
        LOGE(TAG, "Invalid wcs message length: %zu", len);
        return;
      }
      wcs_msg_t *msg = (wcs_msg_t *)data;
      self->base.wcs = msg->wcs;
      machine_interface_wcs_updated(&self->base);

      LOGD(TAG, "wcs=%d", machine->wcs);

      break;
    }
    case MSG_TYPE_STATUS: {
      if (len != sizeof(status_msg_t)) {
        LOGE(TAG, "Invalid status message length: %zu", len);
        return;
      }
      status_msg_t *msg = (status_msg_t *)data;
      self->base.machine_status = msg->status;
      machine_interface_state_updated(&self->base);

      LOGD(TAG, "status=%d", machine->machine_status);

      break;
    }
    case MSG_TYPE_HOMED: {
      if (len != sizeof(homed_msg_t)) {
        LOGE(TAG, "Invalid message length for MSG_TYPE_HOMED");
        return;
      }
      homed_msg_t *msg = (homed_msg_t *)data;
      self->base.axes_homed[0] = msg->x_homed;
      self->base.axes_homed[1] = msg->y_homed;
      self->base.axes_homed[2] = msg->z_homed;
      machine_interface_home_updated(&self->base);

      LOGI(TAG, "homed x=%d y=%d z=%d", machine->axes_homed[0],
           machine->axes_homed[1], machine->axes_homed[2]);

      break;
    }
    case MSG_TYPE_FEED: {
      if (len != sizeof(feed_msg_t)) {
        LOGE(TAG, "Invalid feed message length: %zu", len);
        return;
      }
      feed_msg_t *msg = (feed_msg_t *)data;
      self->base.feed = msg->feed;
      self->base.feed_req = msg->feed_req;
      self->base.feed_multiplier = msg->feed_multiplier;
      machine_interface_feed_updated(&self->base);

      LOGD(TAG, "feed %.1f/%.1f x%.2f", machine->feed,
           machine->feed_req, machine->feed_multiplier);

      break;
    }
    case MSG_TYPE_SPINDLES_TOOLS: {
      if (len < sizeof(uint8_t) + sizeof(int) +
                    sizeof(size_t)) {  // type + rpm + tool_name_len
        LOGE(TAG, "Invalid MSG_TYPE_SPINDLES_TOOLS message len");
        return;
      }
      uint8_t *ptr = (uint8_t *)data;
      ptr += sizeof(uint8_t);  // skip the message type, already processed.

      int rpm = *((int *)ptr);
      ptr += sizeof(int);

      size_t tool_name_len = *((size_t *)ptr);
      ptr += sizeof(size_t);

      // now ptr points to the beginning of the tool name.
      if (tool_name_len >= len - (ptr - data)) {
        LOGE(TAG, "Invalid tool name length");
        return;
      }
      // +1 for null termination, but we don't read past the received len
      char tool_name[tool_name_len + 1];
      strncpy(tool_name, (char *)ptr, tool_name_len);  // safe copy.
      tool_name[tool_name_len] = '\0';  // make sure it's null terminated

      // Lazily allocate spindle struct if it doesn't exist.
      if (!self->base.spindles) {
        self->base.spindles = (spindle_t *)calloc(1, sizeof(spindle_t));
        if (self->base.spindles) {
          self->base.num_spindles = 1;
        } else {
          LOGE(TAG, "Failed to allocate memory for spindle state.");
          return;
        }
      }

      // Update the values
      self->base.spindles->rpm = rpm;
      if (self->base.tool) {
        free((void *)self->base.tool);
      }
      self->base.tool = strdup(tool_name);  // create a copy.

      machine_interface_spindles_tools_updated(&self->base);

      LOGD(TAG, "spindle rpm=%d tool=%s", machine->spindles->rpm, machine->tool);

      break;
    }
    case MSG_TYPE_LOG_MESSAGE: {
      if (len <= offsetof(log_msg_t, message)) {
        LOGE(TAG, "Invalid log message length: %zu", len);
        return;
      }
      log_msg_t *msg = (log_msg_t *)data;
      // Split batched messages by newline and dispatch each line separately.
      const char *p = msg->message;
      const char *line_start = p;
      while (*p) {
        if (*p == '\n') {
          size_t line_len = p - line_start;
          if (line_len > 0) {
            char *tmp = (char *)malloc(line_len + 1);
            if (tmp) {
              memcpy(tmp, line_start, line_len);
              tmp[line_len] = '\0';
              machine_interface_log_message_updated(&self->base, tmp);
              LOGI(TAG, "Received log message: %s", tmp);
              free(tmp);
            }
          }
          p++; line_start = p;
        } else {
          p++;
        }
      }
      // Final trailing line
      if (p != line_start) {
        machine_interface_log_message_updated(&self->base, line_start);
        LOGI(TAG, "Received log message: %s", line_start);
      }
      break;
    }
    case MSG_TYPE_DISMISS_MODAL: {
      if (len < sizeof(dismiss_modal_msg_t)) {
        LOGE(TAG, "Invalid dismiss_modal message length: %zu", len);
        return;
      }
      dismiss_modal_msg_t *msg = (dismiss_modal_msg_t *)data;
      // modal_id == -1 is a broadcast dismiss (dismiss whatever is shown).
      if (self->base.message_box &&
          (msg->modal_id == -1 ||
           self->base.message_box->seq == msg->modal_id)) {
        free_message_box_t(self->base.message_box);
        self->base.message_box = NULL;
        machine_interface_dialogs_updated(&self->base);
        LOGI(TAG, "Modal dismissed by hub (seq %d)", msg->modal_id);
      }
      break;
    }
    case MSG_TYPE_BINARY: {
      uint8_t *ptr = (uint8_t *)data;
      uint8_t target_slot = data[1];
      binary_payload_buffer_t *target_buffer =
          &g_binary_payload_buffers[target_slot];
      LOGT(TAG, "Processsing binary PAYLOAD: slot %d", target_slot);
      LOGT(TAG, "mach %p, sub_type %d, total_sz %d", &self->base,
           target_buffer->sub_type, target_buffer->buffer,
           target_buffer->total_size);

      process_binary_payload(&self->base, target_buffer->sub_type,
                             target_buffer->buffer, target_buffer->total_size);
      binary_payload_slot_cleanup(target_slot);

      break;
    }

    default:
      // Camera protocol messages (0x80–0x8F) are handled by cam_transport_espnow
      // directly via its own recv callback; silently ignore them here.
      if (message_type >= 0x80 && message_type <= 0x8F) {
        LOGD(TAG, "Ignoring cam-protocol msg type 0x%02X (handled by cam transport)",
             message_type);
      } else {
        LOGW(TAG, "Unknown message type: %d", message_type);
      }
      break;
  }
}

void machine_interface_remote_process_messages(
    machine_interface_remote_t *self) {
  size_t len = self->msg_buf_len;
  self->msg_buf_len = MAX_MSG_BUFFERED;

  if (len > 0) {
    LOGD(TAG, "Processing %d msgs", (int)len);
  }
  for (size_t i = 0; i < len; ++i) {
    machine_interface_remote_process_message(
        self, (uint8_t *)&self->msg_buffer[i], self->msg_buffer_msg_len[i]);
  }
  self->msg_buf_len = 0;
}

static void _machine_interface_remote_process_state(machine_interface_t *self,
                                                    void *data, size_t len) {
  machine_interface_remote_process_message((machine_interface_remote_t *)self,
                                           (const uint8_t *)data, len);
}

static void _machi_remote_esp_now_data_sent(const uint8_t *mac_addr, int status,
                                            void *user_data) {
  if (status != 0) LOGW(TAG, "ESP-NOW send failed");
}

void machine_interface_remote_buffer_message(machine_interface_remote_t *self,
                                             const uint8_t *data,
                                             size_t data_len);

static void _machi_remote_esp_now_data_recv(const uint8_t *mac_addr,
                                            const uint8_t *data, int data_len,
                                            void *user_data) {
  LOGD(TAG, "ESPNOW from " MACSTR " len=%d", MAC2STR(mac_addr), data_len);

  machine_interface_remote_t *self = (machine_interface_remote_t *)user_data;

  // Try to add/update the peer (hub)
  if (!self->hub_mac_received) {
    if (!remote_wrapper_add_peer_if_not_known(mac_addr,
                                              self->hub_mac_address)) {
      LOGV(TAG, "Failed to add remote " MACSTR, MAC2STR(mac_addr));

      return;
    }
    self->hub_mac_received = true;
  }

#ifdef ASYNC_RESPONSE_PROCESSING

  // Process binary message fragments to buffer directly.
  if (data[0] == MSG_TYPE_BINARY) {
    LOGT(TAG, "Processing binary fragment immediately (not buffering).");
    binary_message_fragment_to_buffer(self, data, data_len);

    return;
  }

  if (self->proc_task_event_queue != NULL) {
    machine_response_proc_task_data_ready(self->proc_task_event_queue, data,
                                          data_len, true);
  } else {
    LOGW(TAG, "Cannot machine_response_process_for_task => queue NULL");
  }
#else
  // machine_interface_remote_process_message(self, data, data_len);
  machine_interface_remote_buffer_message(self, data, data_len);
#endif
}

#ifdef ASYNC_RESPONSE_PROCESSING
#ifdef ESP32_HW
bool machine_remote_setup_response_processing_task(
    machine_interface_remote_t *self, QueueHandle_t task_event_queue)
#else
bool machine_remote_setup_response_processing_task(
    machine_interface_remote_t *self, gcode_queue_t *task_event_queue)
#endif
{
  LOGI(TAG, "Updating proc_task_event_queue for remote %p: %p");
  self->proc_task_event_queue = task_event_queue;

  return true;
}
#endif

// Header for the contiguous message_box_t payload block. Contains fixed-size
// data and offsets.
typedef struct {
  size_t total_size;  // Total size of the entire contiguous block
  size_t num_choices;
  message_box_mode_t mode;
  int seq;
  size_t title_offset;  // Offset from start of block to title string
  size_t text_offset;   // Offset from start of block to text string
  // The array of choice offsets (size_t[num_choices]) immediately follows this
  // header The string data (title, text, choices) follows the array of offsets
} message_box_payload_t;

// Note: The actual contiguous block will be laid out in memory as:
// 1. contiguous_message_box_header_t header;
// 2. size_t choice_offsets[header.num_choices]; // Array of offsets
// 3. char title_string_data[];                  // Null-terminated
// 4. char text_string_data[];                   // Null-terminated
// 5. char choice_0_string_data[];               // Null-terminated
// 6. ...
// 7. char choice_N-1_string_data[];             // Null-terminated

#include <assert.h>  // For optional assertions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *message_box_t_to_payload(const message_box_t *msg_box, size_t *out_size) {
  if (!msg_box) {
    LOGE(TAG, "Error: Input message_box_t is NULL.\n");
    return NULL;
  }
  // msg_box->title and msg_box->text should generally not be NULL for a valid
  // message box
  if (!msg_box->title || !msg_box->text) {
    LOGE(TAG, "Error: Input message_box_t has NULL title or text.\n");
    return NULL;
  }
  // Check choice pointers if num_choices > 0
  if (msg_box->num_choices > 0 && !msg_box->choices) {
    LOGE(TAG,
         "Error: Input message_box_t has num_choices > 0 but choices is "
         "NULL.\n");
    return NULL;
  }
  for (size_t i = 0; i < msg_box->num_choices; ++i) {
    if (!msg_box->choices[i]) {
      LOGW(TAG,
           "Error: Input message_box_t has NULL pointer in choices array at "
           "index %zu.\n",
           i);
    }
  }

  // Calculate total size needed.
  size_t title_len = strlen(msg_box->title) + 1;  // +1 for null terminator
  size_t text_len = strlen(msg_box->text) + 1;
  size_t choices_total_len = 0;
  for (size_t i = 0; i < msg_box->num_choices; ++i) {
    if (msg_box->choices[i]) {
      choices_total_len += strlen(msg_box->choices[i]) + 1;
    }
  }

  size_t header_size = sizeof(message_box_payload_t);
  size_t choice_offsets_array_size = msg_box->num_choices * sizeof(size_t);
  size_t total_size = header_size + choice_offsets_array_size + title_len +
                      text_len + choices_total_len;

  void *block = malloc(total_size);
  if (!block) {
    LOGE(TAG, "Failed to allocate memory for contiguous message box");
    return NULL;
  }

  char *current_ptr = (char *)block;
  message_box_payload_t *header = (message_box_payload_t *)block;
  header->total_size = total_size;
  header->num_choices = msg_box->num_choices;
  header->mode = msg_box->mode;
  header->seq = msg_box->seq;

  // Calculate base offset for string data.
  size_t current_offset = header_size + choice_offsets_array_size;

  // Copy the data.
  header->title_offset = current_offset;
  memcpy(current_ptr + current_offset, msg_box->title, title_len);
  current_offset += title_len;

  header->text_offset = current_offset;
  memcpy(current_ptr + current_offset, msg_box->text, text_len);
  current_offset += text_len;

  size_t *choice_offsets_ptr = (size_t *)(current_ptr + header_size);
  for (size_t i = 0; i < msg_box->num_choices; ++i) {
    choice_offsets_ptr[i] = current_offset;  // Set offset for this choice
    size_t choice_len = strlen(msg_box->choices[i]) + 1;
    memcpy(current_ptr + current_offset, msg_box->choices[i], choice_len);
    current_offset += choice_len;
  }

  assert(current_offset ==
         total_size);  // Ensure we used exactly the calculated space

  if (out_size) {
    *out_size = total_size;
  }
  return block;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper to duplicate a string (like strdup, but safer regarding NULL)
// Returns NULL if input is NULL or malloc fails. Caller must free.
static char *duplicate_string(const char *src) {
  if (!src) return NULL;
  size_t len = strlen(src) + 1;
  char *dest = (char *)malloc(len);
  if (!dest) return NULL;
  memcpy(dest, src, len);
  return dest;
}

message_box_t *message_box_t_from_payload(const void *payload,
                                          message_box_t *msg_box) {
  if (!payload) {
    LOGE(TAG, "Error: Input payload is NULL.\n");
    return NULL;
  }

  const char *block_base = (const char *)payload;
  const message_box_payload_t *header =
      (const message_box_payload_t *)block_base;

  // Basic validation: Check if offsets seem plausible (within total_size)
  if (header->title_offset >= header->total_size ||
      header->text_offset >= header->total_size) {
    LOGE(TAG, "Error: Invalid offsets in contiguous block header.\n");
    return NULL;
  }

  // Base structure is passed in now.
  if (!msg_box) {
    msg_box = (message_box_t *)malloc(sizeof(message_box_t));
  }

  if (!msg_box) {
    LOGE(TAG, "Failed to allocate memory for message_box_t");
    return NULL;
  }
  memset(msg_box, 0,
         sizeof(message_box_t));  // Sets pointers to NULL, counts/enums to 0

  msg_box->num_choices = header->num_choices;
  msg_box->mode = header->mode;
  msg_box->seq = header->seq;
  msg_box->user_data = NULL;
  msg_box->machine = NULL;

  const char *title_ptr_contig = block_base + header->title_offset;
  msg_box->title = duplicate_string(title_ptr_contig);
  if (!msg_box->title) {
    LOGE(TAG, "Failed to duplicate title string");
    free(msg_box);
    return NULL;
  }

  const char *text_ptr_contig = block_base + header->text_offset;
  msg_box->text = duplicate_string(text_ptr_contig);
  if (!msg_box->text) {
    LOGE(TAG, "Failed to duplicate text string");
    free(msg_box->title);
    free(msg_box);
    return NULL;
  }

  if (msg_box->num_choices > 0) {
    // Allocate the array of char pointers
    msg_box->choices = (char **)malloc(msg_box->num_choices * sizeof(char *));
    if (!msg_box->choices) {
      LOGE(TAG, "Failed to allocate choices array");
      free(msg_box->title);
      free(msg_box->text);
      free(msg_box);
      return NULL;
    }
    // Initialize pointers in the array to NULL for safe cleanup
    for (size_t i = 0; i < msg_box->num_choices; ++i) {
      msg_box->choices[i] = NULL;
    }

    // Get pointer to the choice offsets array within the contiguous block
    const size_t *choice_offsets_ptr =
        (const size_t *)(block_base + sizeof(message_box_payload_t));

    // Duplicate each choice string
    for (size_t i = 0; i < msg_box->num_choices; ++i) {
      // Validate offset before using
      if (choice_offsets_ptr[i] >= header->total_size) {
        LOGE(TAG, "Error: Invalid choice offset %zu in contiguous block.\n", i);
        // Cleanup already allocated choices and the array/struct
        for (size_t j = 0; j < i;
             ++j) {  // Free successfully duplicated strings so far
          free(msg_box->choices[j]);
        }
        free(msg_box->choices);
        free(msg_box->title);
        free(msg_box->text);
        free(msg_box);
        return NULL;
      }

      const char *choice_ptr_contig = block_base + choice_offsets_ptr[i];
      msg_box->choices[i] = duplicate_string(choice_ptr_contig);
      if (!msg_box->choices[i]) {
        LOGE(TAG, "Failed to duplicate choice string");
        // Cleanup: Free previously allocated choices, the choices array, title,
        // text, and the struct
        for (size_t j = 0; j < i;
             ++j) {  // Free successfully duplicated strings so far
          free(msg_box->choices[j]);
        }
        free(msg_box->choices);
        free(msg_box->title);
        free(msg_box->text);
        free(msg_box);
        return NULL;
      }
    }
  } else {
    msg_box->choices = NULL;  // No choices, so pointer is NULL
  }

  return msg_box;
}

void process_binary_msg_file_list(machine_interface_remote_t *mach,
                                  uint8_t *data, size_t size) {
  file_list_payload_t *header = (file_list_payload_t *)data;
  char *fdir = (char *)header + sizeof(*header);
  size_t idx = MAX_FILE_LISTS;
  size_t empty_slot = MAX_FILE_LISTS;

  for (size_t i = 0; i < MAX_FILE_LISTS; ++i) {
    if (mach->base.filelists[i].fdir) {
      size_t slot_len = strlen(mach->base.filelists[i].fdir);
      if (strcmp(fdir, mach->base.filelists[i].fdir) == 0) {
        idx = i;
        break;
      }
      // Prefix match: incoming fdir is a subdir of the stored slot root.
      if (slot_len > 0 && strncmp(fdir, mach->base.filelists[i].fdir, slot_len) == 0 && fdir[slot_len] == '/') {
        idx = i;
        break;
      }
    }
    if (!mach->base.filelists[i].fdir && empty_slot == MAX_FILE_LISTS) {
      empty_slot = i;
    }
  }
  // If no existing slot matches, use an empty slot to accept the new list.
  if (idx == MAX_FILE_LISTS) {
    idx = empty_slot;
  }
  if (idx == MAX_FILE_LISTS) {
    LOGE(TAG, "No filelist slot available for '%s'", fdir);
    return;
  }

  if (mach->base.filelists[idx].fdir) {
    free(mach->base.filelists[idx].fdir);
  }
  if (mach->base.filelists[idx].files) {
    for (size_t i = 0; mach->base.filelists[idx].files[i]; ++i) {
      free(mach->base.filelists[idx].files[i]);
    }
    free(mach->base.filelists[idx].files);
  }

  mach->base.filelists[idx].files =
      calloc(header->num_files + 1, sizeof(char *));  // +1 for NULL sentinel
  mach->base.filelists[idx].fdir = strdup(fdir);

  if (!mach->base.filelists[idx].files || !mach->base.filelists[idx].fdir) {
    LOGE(TAG, "process_binary_msg_file_list: OOM allocating slot for '%s'", fdir);
    free(mach->base.filelists[idx].files);
    free(mach->base.filelists[idx].fdir);
    mach->base.filelists[idx].files = NULL;
    mach->base.filelists[idx].fdir = NULL;
    return;
  }

  char *offset = fdir + strlen(fdir) + 1;
  char *end = ((char *)header) + header->total_size;
  size_t filled = 0;
  for (size_t i = 0; i < header->num_files && offset < end; ++i) {
    const char *fn = offset;
    const size_t fn_len = strlen(fn) + 1;
    char *entry = strdup(fn);
    if (!entry) {
      LOGE(TAG, "process_binary_msg_file_list: strdup OOM at file %u of %u", (unsigned)i, header->num_files);
      break;  // filled not incremented; remaining slots left NULL-terminated by calloc
    }
    mach->base.filelists[idx].files[i] = entry;
    offset += fn_len;
    filled++;
  }
  mach->base.filelists[idx].files[filled] = NULL;  // NULL-terminate (calloc already zeroed)

  if (filled < header->num_files) {
    LOGW(TAG, "FILELIST '%s': only stored %u of %u file(s) — payload truncated or OOM",
         fdir, (unsigned)filled, header->num_files);
  }
  LOGI(TAG, "Received FILELIST '%s': %u file(s):", fdir, (unsigned)filled);
  for (size_t i = 0; i < filled; ++i) {
    LOGI(TAG, "  [%u] %s", (unsigned)i,
         mach->base.filelists[idx].files[i] ? mach->base.filelists[idx].files[i] : "(null)");
  }
  machine_interface_files_updated(&mach->base, mach->base.filelists[idx].fdir);
}

static void process_binary_payload(machine_interface_t *self, uint8_t sub_type,
                                   uint8_t *data, size_t size) {
  machine_interface_remote_t *mach = (machine_interface_remote_t *)self;
  LOGT(TAG, "PROCESSING BINARY %d (= [%d, %d] ?)", sub_type,
       MSG_SUB_TYPE_MESSAGE_BOX, MSG_SUB_TYPE_FILE_LIST);

  switch (sub_type) {
    case MSG_SUB_TYPE_MESSAGE_BOX:
      if (mach->base.message_box) {
        free_message_box_t(mach->base.message_box);
      }
      mach->base.message_box = message_box_t_from_payload(data, NULL);
      LOGI(TAG, "UNPACKED MESSAGE BOX: %s / %s", mach->base.message_box->title,
           mach->base.message_box->text);
      machine_interface_dialogs_updated(&mach->base);

      break;
    case MSG_SUB_TYPE_FILE_LIST:
      process_binary_msg_file_list(mach, data, size);
      break;
    case MSG_SUB_TYPE_IO_CHANNELS: {
      if (size < sizeof(io_channels_payload_hdr_t)) break;
      const io_channels_payload_hdr_t *hdr = (const io_channels_payload_hdr_t *)data;
      size_t expected = sizeof(io_channels_payload_hdr_t) +
                        hdr->count * sizeof(io_channel_wire_t);
      if (size < expected) {
        LOGW(TAG, "MSG_SUB_TYPE_IO_CHANNELS payload too small (%zu < %zu)", size, expected);
        break;
      }
      // Free old channels
      if (mach->base.io_channels) {
        for (size_t i = 0; i < mach->base.num_io_channels; i++)
          free(mach->base.io_channels[i].name);
        free(mach->base.io_channels);
        mach->base.io_channels     = NULL;
        mach->base.num_io_channels = 0;
      }
      if (hdr->count == 0) {
        machine_interface_sensors_updated(&mach->base);
        break;
      }
      mc_io_channel_t *chs = (mc_io_channel_t *)calloc(hdr->count, sizeof(mc_io_channel_t));
      if (!chs) { LOGE(TAG, "OOM io_channels"); break; }
      const io_channel_wire_t *wire =
          (const io_channel_wire_t *)(data + sizeof(io_channels_payload_hdr_t));
      for (uint16_t i = 0; i < hdr->count; i++) {
        chs[i].source_index  = wire[i].source_index;
        chs[i].direction     = (mc_io_direction_t)wire[i].direction;
        chs[i].signal        = (mc_io_signal_t)wire[i].signal;
        chs[i].role          = (mc_io_role_t)wire[i].role;
        chs[i].health        = (mc_io_health_t)wire[i].health;
        chs[i].value         = wire[i].value;
        chs[i].setpoint      = wire[i].setpoint;
        chs[i].active        = (bool)wire[i].active;
        chs[i].setpoint_bool = (bool)wire[i].setpoint_bool;
        chs[i].min_value     = wire[i].min_value;
        chs[i].max_value     = wire[i].max_value;
        char name_buf[sizeof(wire[i].name) + 1];
        memcpy(name_buf, wire[i].name, sizeof(wire[i].name));
        name_buf[sizeof(wire[i].name)] = '\0';
        chs[i].name = strdup(name_buf);
        char unit_buf[sizeof(wire[i].unit) + 1];
        memcpy(unit_buf, wire[i].unit, sizeof(wire[i].unit));
        unit_buf[sizeof(wire[i].unit)] = '\0';
        // unit field on mc_io_channel_t is a const char* (usually a literal);
        // we store the unit string directly — use a permanent suffix approach.
        // For simplicity, map known UTF-8 literals; fallback to empty string.
        if (name_buf[0] == '\0' || unit_buf[0] == '\0') {
          chs[i].unit = "";
        } else if (strcmp(unit_buf, "%") == 0) {
          chs[i].unit = "%";
        } else if (strncmp(unit_buf, "\xC2\xB0", 2) == 0) {
          chs[i].unit = "\xC2\xB0" "C";
        } else {
          chs[i].unit = "";  // unknown unit — safe fallback
        }
      }
      mach->base.io_channels     = chs;
      mach->base.num_io_channels = hdr->count;
      machine_interface_sensors_updated(&mach->base);
      LOGI(TAG, "Received io_channels: %u channel(s)", hdr->count);
      break;
    }
    case MSG_SUB_TYPE_LOG_MESSAGE: {
      // Fragmented message has been reassembled into data (NUL-terminated).
      const char *buf = (const char *)data;
      const char *p = buf;
      const char *line_start = p;
      while (*p) {
        if (*p == '\n') {
          size_t line_len = p - line_start;
          if (line_len > 0) {
            char *tmp = (char *)malloc(line_len + 1);
            if (tmp) {
              memcpy(tmp, line_start, line_len);
              tmp[line_len] = '\0';
              machine_interface_log_message_updated(self, tmp);
              LOGI(TAG, "Received fragmented log message: %s", tmp);
              free(tmp);
            }
          }
          p++; line_start = p;
        } else {
          p++;
        }
      }
      if (p != line_start) {
        machine_interface_log_message_updated(self, line_start);
        LOGI(TAG, "Received fragmented log message: %s", line_start);
      }
      break;
    }
    default:
      LOGT(TAG, "Received unknown reassembled payload sub-type: %u", sub_type);
      break;
  }
}

static void initialize_binary_payload_buffers() {
  for (size_t i = 0; i < MAX_CONCURRENT_FRAGMENTED_MSGS; ++i) {
    memset(&g_binary_payload_buffers[i], 0, sizeof(binary_payload_buffer_t));
    g_binary_payload_buffers[i].is_valid = false;
  }
  g_next_binary_buffer_slot = 0;
}

static void binary_payload_slot_cleanup(size_t index) {
  if (index < MAX_CONCURRENT_FRAGMENTED_MSGS &&
      g_binary_payload_buffers[index].is_valid) {
    LOGI(TAG, "Cleaning up reassembly slot %zu (Seq ID: %u)", index,
         g_binary_payload_buffers[index].seq_id);

    free(g_binary_payload_buffers[index].buffer);
    free(g_binary_payload_buffers[index].fragments_received_mask);
    g_binary_payload_buffers[index].buffer = NULL;
    g_binary_payload_buffers[index].fragments_received_mask = NULL;
    g_binary_payload_buffers[index].is_valid = false;
    // Optionally reset other fields
    memset(&g_binary_payload_buffers[index], 0,
           sizeof(binary_payload_buffer_t));
    g_binary_payload_buffers[index].is_valid = false;
  }
}
