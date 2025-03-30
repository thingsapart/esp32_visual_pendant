// machine_remote.c

#include "machine_remote.h"
#include <string.h>
#include <assert.h>

#include "driver/remote_comms_wrapper.h"  // For ESP-NOW communication

#ifdef ASYNC_RESPONSE_PROCESSING
#  include "tasks/machine_response_proc_task.h"
#endif

#include "debug.h"

static const char *TAG = "machine_remote";

// --- Forward Declarations ---
static void _machine_interface_remote_send_gcode(machine_interface_t *self, const char *gcode, uint32_t poll_state);
static bool _machine_interface_remote_is_connected(machine_interface_t *self);
static void _machine_interface_remote_list_files(machine_interface_t *self, const char *path);
static void _machine_interface_remote_run_macro(machine_interface_t *self, const char *macro_name);
static void _machine_interface_remote_start_job(machine_interface_t *self, const char *job_name);
static void _machine_interface_remote_move_continuous(machine_interface_t *self, const char axis, float feed, int direction);
static void _machine_interface_remote_move_continuous_stop(machine_interface_t *self);
static void _machine_interface_remote_move(machine_interface_t *self, const char axis, float feed, float value);
static void _machine_interface_remote_home_all(machine_interface_t *self);
static void _machine_interface_remote_home(machine_interface_t *self, const char *axes);
static void _machine_interface_remote_set_wcs(machine_interface_t *self, int wcs);
static void _machine_interface_remote_set_wcs_zero(machine_interface_t *self, int wcs, const char *axes);
static void _machine_interface_remote_next_wcs(machine_interface_t *self);
static void _machine_interface_remote_probe(machine_interface_t *self, const char *probe_gcode);
static void _machine_interface_remote_update_machine_state(machine_interface_t *self, uint32_t poll_state);

static void _send_command(machine_interface_remote_t *self, const uint8_t *data, size_t len) {
    if (!remote_wrapper_send(self->hub_mac_address, data, len)) {
        _df(2, "[%s]" , "Failed to send command via ESP-NOW", TAG );
    }
}

static void _machine_interface_remote_send_gcode(machine_interface_t *self,
                                                 const char *gcode,
                                                 uint32_t poll_state) {
    // + sizeof(uint32_t)  // Include space for poll_state // removed.
    size_t len = sizeof(send_gcode_cmd_t) + strlen(gcode) + 1; // +1 for null terminator
    send_gcode_cmd_t *cmd = (send_gcode_cmd_t *)malloc(len);
    if (!cmd) {
      _df(2, "[%s]" , "Failed to allocate", TAG );
      return;
    }
    cmd->type = CMD_TYPE_SEND_GCODE;
    cmd->len = strlen(gcode);
    strcpy(cmd->gcode, gcode);  // Copy the G-code string
    _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
    free(cmd);
}

static bool _machine_interface_remote_is_connected(machine_interface_t *self) {
  machine_interface_remote_t *mach = (machine_interface_remote_t *) self;
  return mach->hub_mac_address;
}

static void _machine_interface_remote_list_files(machine_interface_t *self, const char *path)
{
    size_t len = sizeof(list_files_cmd_t) + strlen(path) + 1;
    list_files_cmd_t *cmd = (list_files_cmd_t *)malloc(len);
    if(!cmd)
    {
        _df(2, "[%s]" , "Failed to allocate list files", TAG );
        return;
    }
    cmd->type = CMD_TYPE_LIST_FILES;
    cmd->len = strlen(path);
    strcpy(cmd->path, path);
     _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
    free(cmd);
}

static void _machine_interface_remote_run_macro(machine_interface_t *self, const char *macro_name)
{
    size_t len = sizeof(run_macro_cmd_t) + strlen(macro_name) + 1;
    run_macro_cmd_t *cmd = (run_macro_cmd_t *)malloc(len);
    if(!cmd)
    {
        _df(2, "[%s]" , "Failed to allocate", TAG );
        return;
    }
    cmd->type = CMD_TYPE_RUN_MACRO;
    cmd->len = strlen(macro_name);
    strcpy(cmd->macro_name, macro_name);
    _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
    free(cmd);
}

static void _machine_interface_remote_start_job(machine_interface_t *self, const char *job_name)
{
    size_t len = sizeof(start_job_cmd_t) + strlen(job_name) + 1;
    start_job_cmd_t *cmd = (start_job_cmd_t *)malloc(len);
    if (!cmd)
    {
        _df(2, "[%s]" , "Failed to allocate start_job cmd", TAG );
        return;
    }
    cmd->type = CMD_TYPE_START_JOB;
    cmd->len = strlen(job_name);
    strcpy(cmd->job_name, job_name);
    _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
    free(cmd);
}


static void _machine_interface_remote_move_continuous(
    machine_interface_t *self, const char axis, float feed, int direction) {

    move_cont_cmd_t cmd;
    cmd.type = CMD_TYPE_MOVE_CONT;
    cmd.axis = axis;
    cmd.feed = feed;
    cmd.direction = direction;
    _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd, sizeof(cmd));
}

static void _machine_interface_remote_move_continuous_stop(
    machine_interface_t *self) {
    move_cont_stop_cmd_t cmd;
    cmd.type = CMD_TYPE_MOVE_CONT_STOP;
    _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd, sizeof(cmd));
}

static void _machine_interface_remote_move(machine_interface_t *self, const char axis, float feed, float value)
{
    move_cmd_t cmd;
    cmd.type = CMD_TYPE_MOVE;
    cmd.axis = axis;
    cmd.feed = feed;
    cmd.value = value;
     _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd, sizeof(cmd));
}

static void _machine_interface_remote_home_all(machine_interface_t *self)
{
    home_all_cmd_t cmd;
    cmd.type = CMD_TYPE_HOME_ALL;
    _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd, sizeof(cmd));
}
static void _machine_interface_remote_home(machine_interface_t *self, const char *axes)
{
    size_t len = sizeof(home_cmd_t) + strlen(axes) + 1;
    home_cmd_t *cmd = (home_cmd_t *)malloc(len);
    if (!cmd)
    {
        _df(2, "[%s]" , "Failed to allocate", TAG );
        return;
    }
    cmd->type = CMD_TYPE_HOME;
    cmd->axes_len = strlen(axes);
    strcpy(cmd->axes, axes);
    _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
    free(cmd);
}

static void _machine_interface_remote_set_wcs(machine_interface_t *self, int wcs)
{
    set_wcs_cmd_t cmd;
    cmd.type = CMD_TYPE_SET_WCS;
    cmd.wcs = wcs;
    _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd, sizeof(cmd));
}
static void _machine_interface_remote_set_wcs_zero(machine_interface_t *self, int wcs, const char *axes)
{
   size_t len = sizeof(set_wcs_zero_cmd_t) + strlen(axes) + 1;
    set_wcs_zero_cmd_t *cmd = (set_wcs_zero_cmd_t *)malloc(len);
    if(!cmd)
    {
        _df(2, "[%s]" , "Failed to allocate", TAG );
        return;
    }
    cmd->type = CMD_TYPE_SET_WCS_ZERO;
    cmd->wcs = wcs;
    cmd->axes_len = strlen(axes);
    strcpy(cmd->axes, axes);
    _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
    free(cmd);
}

static void _machine_interface_remote_next_wcs(machine_interface_t *self)
{
    next_wcs_cmd_t cmd;
    cmd.type = CMD_TYPE_NEXT_WCS;
    _send_command((machine_interface_remote_t *)self, (uint8_t *)&cmd, sizeof(cmd));
}

static void _machine_interface_remote_probe(machine_interface_t *self, const char *probe_gcode)
{
    size_t len = sizeof(probe_cmd_t) + strlen(probe_gcode) + 1;
    probe_cmd_t *cmd = (probe_cmd_t *)malloc(len);
    if(!cmd)
    {
        _df(2, "[%s]" , "Failed to allocate", TAG );
        return;
    }
    cmd->type = CMD_TYPE_PROBE;
    cmd->len = strlen(probe_gcode);
    strcpy(cmd->gcode, probe_gcode);
    _send_command((machine_interface_remote_t *)self, (uint8_t *)cmd, len);
    free(cmd);
}

// --- Constructor/Destructor ---

machine_interface_remote_t *machine_interface_remote_create(const uint8_t *hub_mac) {
    machine_interface_remote_t *self =
        (machine_interface_remote_t *)malloc(sizeof(machine_interface_remote_t));
// machine_remote.c (continued)

    if (!self) {
        _df(2, "[%s]" , "Failed to allocate memory for machine_interface_remote", TAG );
        return NULL;
    }
     return machine_interface_remote_init(self, hub_mac);
}

static void _machi_remote_esp_now_data_recv(const uint8_t *mac_addr, const uint8_t *data, int data_len, void *user_data);
static void _machi_remote_esp_now_data_sent(const uint8_t *mac_addr, int status, void *user_data);
static void _machine_interface_remote_process_state(machine_interface_t *self, void *data, size_t len);

machine_interface_remote_t *machine_interface_remote_init(machine_interface_remote_t *self, const uint8_t *hub_mac) {
    // Initialize base class (important!)
    machine_interface_init(&self->base, 0); // No internal processing loop

    // Copy the hub's MAC address
    memcpy(self->hub_mac_address, hub_mac, 6);
    self->hub_mac_received = false;

#ifdef ASYNC_RESPONSE_PROCESSING
        self->proc_task_event_queue = NULL;
#endif

    // Override base class methods with remote-specific implementations
    self->base.send_gcode = _machine_interface_remote_send_gcode;
    self->base.is_connected = _machine_interface_remote_is_connected; // always connected.
    self->base.list_files = _machine_interface_remote_list_files;
    self->base.run_macro = _machine_interface_remote_run_macro;
    self->base.start_job = _machine_interface_remote_start_job;
    self->base.move_continuous = _machine_interface_remote_move_continuous;
    self->base._continuous_move = _machine_interface_remote_move_continuous; // use same.
    self->base.move_continuous_stop = _machine_interface_remote_move_continuous_stop;
    self->base._continuous_stop = _machine_interface_remote_move_continuous_stop; // use same
    self->base.move = _machine_interface_remote_move;
    self->base.home_all = _machine_interface_remote_home_all;
    self->base.home = _machine_interface_remote_home;
    self->base.set_wcs = _machine_interface_remote_set_wcs;
    self->base.set_wcs_zero = _machine_interface_remote_set_wcs_zero;
    self->base.next_wcs = _machine_interface_remote_next_wcs;
    self->base.probe = _machine_interface_remote_probe;
    self->base.process_machine_state_response = _machine_interface_remote_process_state;

    // Disable methods not implemented on the remote
    self->base._update_machine_state = _machine_interface_remote_update_machine_state;
    self->base.debug_print = NULL;

    // Initialize ESP-NOW
    if (!remote_wrapper_init(_machi_remote_esp_now_data_recv, _machi_remote_esp_now_data_sent, self)) {
        _df(2, "[%s] Failed to initialize ESP-NOW", TAG);
    }

    // Add the display as a peer
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!remote_wrapper_add_peer(broadcast_mac)) {
        _df(2, "[%s] Failed to add display as peer", TAG);
    }

    _df(0, "[%s]" , "MACHINE_REMOTE INIT", TAG);

    return self;
}

void machine_interface_remote_deinit(machine_interface_remote_t *self) {
    // Clean up any remote-specific resources
    machine_interface_deinit(&self->base);
}

void machine_interface_remote_destroy(machine_interface_remote_t *self) {
    if (self) {
        machine_interface_remote_deinit(self);

        free(self);
    }
}

void _machine_interface_remote_update_machine_state(machine_interface_t *base_self, uint32_t poll_state) {
    machine_interface_remote_t *self = (machine_interface_remote_t *) base_self;
    machine_interface_remote_process_messages(self);
}

void machine_interface_remote_buffer_message(machine_interface_remote_t *self, const uint8_t *data, size_t data_len) {
    size_t len = self->msg_buf_len;
    if (len >= MAX_MSG_BUFFERED) { return; }
    self->msg_buf_len = len + 1;
    assert(data_len < sizeof(remote_msg_t) || "Remote message too large to buffer.");
    self->msg_buffer_msg_len[len] = data_len;
    memcpy(&self->msg_buffer[len], data, data_len);
}

void machine_interface_remote_process_message(machine_interface_remote_t *self, const uint8_t *data, size_t len) {
    if (len == 0) {
        return;
    }

    // All messages start with a message type byte.
    uint8_t message_type = data[0];
    machine_interface_t *machine = &self->base;

    switch (message_type) {
        case MSG_TYPE_KEEP_ALIVE:
            // Handle keep-alive (could update a last-seen timestamp)
            _df(0, "[%s]" , "Received keep-alive", TAG ); // Use LOGD for debugging
            break;

        case MSG_TYPE_POSITION: {
            if (len != sizeof(position_msg_t)) {
              _df(2, "[%s]" , "Invalid position message length: %zu", len, TAG );
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

            LOGI(TAG, "Received position %f, %f, %f (%f, %f, %f)", 
                machine->position[0], machine->position[1], machine->position[2],
                machine->wcs_position[0],  machine->wcs_position[1],  machine->wcs_position[2]);

            break;
        }
        case MSG_TYPE_WCS: {
          if (len != sizeof(wcs_msg_t)) {
             _df(2, "[%s]" , "Invalid wcs message length: %zu", len, TAG );
            return;
          }
          wcs_msg_t *msg = (wcs_msg_t *)data;
          self->base.wcs = msg->wcs;
          machine_interface_wcs_updated(&self->base);

          LOGI(TAG, "Received wcs: %d", machine->wcs);

          break;
        }
        case MSG_TYPE_STATUS: {
          if (len != sizeof(status_msg_t)) {
            _df(2, "[%s]" , "Invalid status message length: %zu", len, TAG );
            return;
          }
          status_msg_t *msg = (status_msg_t *)data;
          self->base.machine_status = msg->status;
          machine_interface_connected_updated(&self->base);

          LOGI(TAG, "Received status %d", machine->machine_status);

          break;
        }
        case MSG_TYPE_HOMED: {
            if (len != sizeof(homed_msg_t))
            {
                _df(2, "[%s]" , "Invalid message length for MSG_TYPE_HOMED", TAG );
                return;
            }
            homed_msg_t *msg = (homed_msg_t *)data;
            self->base.axes_homed[0] = msg->x_homed;
            self->base.axes_homed[1] = msg->y_homed;
            self->base.axes_homed[2] = msg->z_homed;
            machine_interface_home_updated(&self->base);
    
            LOGI(TAG, "Received axes homed %d, %d, %d", machine->axes_homed[0], machine->axes_homed[1], machine->axes_homed[2]);

            break;
        }
         case MSG_TYPE_FEED: {
           if (len != sizeof(feed_msg_t)) {
            _df(2, "[%s]" , "Invalid feed message length: %zu", len, TAG );
            return;
          }
          feed_msg_t *msg = (feed_msg_t *)data;
          self->base.feed = msg->feed;
          self->base.feed_req = msg->feed_req;
          self->base.feed_multiplier = msg->feed_multiplier;
          machine_interface_feed_updated(&self->base);

          LOGI(TAG, "Received feed changed: %f/%f (x%f)", machine->feed, machine->feed_req, machine->feed_multiplier);

          break;
         }
        case MSG_TYPE_SPINDLES_TOOLS: {
            if (len < sizeof(uint8_t) + sizeof(int) + sizeof(size_t)) { // type + rpm + tool_name_len
                _df(2, "[%s]" , "Invalid MSG_TYPE_SPINDLES_TOOLS message len", TAG );
                return;
            }
            uint8_t *ptr = (uint8_t *) data;
            ptr += sizeof(uint8_t); // skip the message type, already processed.

            int rpm = *((int*) ptr);
            ptr += sizeof(int);

            size_t tool_name_len = *((size_t *) ptr);
            ptr += sizeof(size_t);

            // now ptr points to the beginning of the tool name.
            if (tool_name_len >= len - (ptr - data)) {
               _df(2, "[%s]" , "Invalid tool name length", TAG );
               return;
            }
            // +1 for null termination, but we don't read past the received len
            char tool_name[tool_name_len + 1];
            strncpy(tool_name, (char *) ptr, tool_name_len); // safe copy.
            tool_name[tool_name_len] = '\0'; // make sure it's null terminated

            // Update the values
            self->base.spindles->rpm = rpm;
             if(self->base.tool)
             {
                free((void*) self->base.tool);
             }
             self->base.tool = strdup(tool_name); // create a copy.

            machine_interface_spindles_tools_updated(&self->base);

            LOGI(TAG, "Received tool/spindle changed: rpm %d / %s", machine->spindles->rpm, machine->tool);

            break;
        }

        // Add cases for other message types...

        default:
            _df(1, "[%s]" , "Unknown message type: %d", message_type, TAG );
            break;
    }
}

void machine_interface_remote_process_messages(machine_interface_remote_t *self) {
    size_t len = self->msg_buf_len;
    LOGI(TAG, "Processing %d saved messages...", self->msg_buf_len);
    self->msg_buf_len = MAX_MSG_BUFFERED;

    for (size_t i = 0; i < len; ++i) {
        LOGI(TAG, "...message[%d / %d]", i, len);
        machine_interface_remote_process_message(self, (uint8_t *) &self->msg_buffer[i], self->msg_buffer_msg_len[i]);
    }
    self->msg_buf_len = 0;
    LOGI(TAG, "DONE.");
}

static void _machine_interface_remote_process_state(machine_interface_t *self, void *data, size_t len) {
  machine_interface_remote_process_message((machine_interface_remote_t *) self, (const uint8_t *) data, len);
}

static void _machi_remote_esp_now_data_sent(const uint8_t *mac_addr, int status, void *user_data) {
     _df(0, "[%s] ESP-NOW send status: %s", TAG, status == 0 ? "success" : "fail");
}

void machine_interface_remote_buffer_message(machine_interface_remote_t *self, const uint8_t *data, size_t data_len);

static void _machi_remote_esp_now_data_recv(const uint8_t *mac_addr, const uint8_t *data, int data_len, void *user_data) {
    LOGI(TAG, "Received ESP-NOW data from "MACSTR", len: %d", MAC2STR(mac_addr), data_len);

    machine_interface_remote_t *self = (machine_interface_remote_t *) user_data;

    // Try to add/update the peer (hub)
    if (!self->hub_mac_received) {
        if (!remote_wrapper_add_peer_if_not_known(mac_addr, self->hub_mac_address)) {
            LOGD(TAG, "Failed to add remote " MACSTR, MAC2STR(mac_addr)); 

            return;
        }
        self->hub_mac_received = true;
    }

#ifdef ASYNC_RESPONSE_PROCESSING
    if (self->proc_task_event_queue != NULL) {
        machine_response_process_for_task(self->proc_task_event_queue, data, data_len);
    } else {
        LOGW(TAG, "Cannot machine_response_process_for_task => queue NULL");
    }
#else
    //machine_interface_remote_process_message(self, data, data_len);
    machine_interface_remote_buffer_message(self, data, data_len);
#endif
}

#ifdef ASYNC_RESPONSE_PROCESSING

bool machine_remote_setup_response_processing_task(machine_interface_remote_t *self, QueueHandle_t task_event_queue) {
    LOGI(TAG, "Updating proc_task_event_queue for remote %p: %p");
    self->proc_task_event_queue = task_event_queue;

    return true;
}

#endif