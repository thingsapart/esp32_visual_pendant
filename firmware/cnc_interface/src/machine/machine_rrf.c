#include "machine_rrf.h"

#include <ctype.h>  // For isalnum in url_encode
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_DEBUG_LOCAL_LEVEL D_VERBOSE
#include "debug.h"
#include "driver/arduino_serial_wrapper.h"

#ifdef ASYNC_RESPONSE_PROCESSING
#include "tasks/machine_response_proc_task.h"
#endif

static const char *TAG = "machine_rrf";

#ifdef TFT_WIDTH
#include "lvgl.h"
#endif

// --- Forward Declarations ---
static machine_status_t machine_status_from_rrf_string(const char *rrf_status);
void _free_modal(machine_interface_t *self, int modal_id);
static void _dwc_set_connected_impl(machine_rrf_t *self, bool connect);

// --- Generic "Virtual" Method Implementations ---
// These are assigned to the base interface and call the transport-specific
// implementation via function pointers.

static void _machine_rrf_send_gcode(machine_interface_t *self,
                                    const char *gcode) {
  machine_rrf_t *rrf_self = (machine_rrf_t *)self;
  if (rrf_self->_send_gcode_impl) {
    rrf_self->_send_gcode_impl(rrf_self, gcode);
  }
}

static void _machine_rrf_update_machine_state(machine_interface_t *self,
                                              uint32_t poll_state) {
  machine_rrf_t *rrf_self = (machine_rrf_t *)self;
  if (rrf_self->_poll_state_impl) {
    rrf_self->_poll_state_impl(rrf_self, poll_state);
  }
}

static bool _machine_rrf_is_connected(machine_interface_t *self) {
  return ((machine_rrf_t *)self)->connected;
}

static void _machine_rrf_set_connected(machine_interface_t *self,
                                       bool connected) {
  machine_rrf_t *rrf_self = (machine_rrf_t *)self;
  if (rrf_self->_set_connected_impl) {
    rrf_self->_set_connected_impl(rrf_self, connected);
  }
}

static void _machine_rrf_list_files(machine_interface_t *self,
                                    const char *path) {
  machine_rrf_t *rrf_self = (machine_rrf_t *)self;
  if (rrf_self->_list_files_impl) {
    rrf_self->_list_files_impl(rrf_self, path);
  }
}

static void _machine_rrf_proc_machine_state_response(machine_interface_t *iself,
                                                     void *data, size_t len) {
  machine_rrf_t *self = (machine_rrf_t *)iself;
  if (self->_proc_state_resp_impl) {
    self->_proc_state_resp_impl(iself, data, len);
  }
}

// --- Transport-Specific Implementations: DWC (HTTP) ---

/**
 * @brief URL-encodes a string.
 * @param str The input string to encode.
 * @param encoded_str The output buffer for the encoded string.
 * @param max_len The size of the output buffer.
 * @return true on success, false if the output buffer is too small.
 */
static bool url_encode(const char *str, char *encoded_str, size_t max_len) {
  const char *pstr = str;
  char *pbuf = encoded_str;
  size_t remaining_len = max_len;

  while (*pstr) {
    if (isalnum((unsigned char)*pstr) || *pstr == '-' || *pstr == '_' ||
        *pstr == '.' || *pstr == '~') {
      if (remaining_len < 2)
        return false;  // Need space for char + null terminator
      *pbuf++ = *pstr;
      remaining_len--;
    } else if (*pstr == ' ') {
      if (remaining_len < 2) return false;
      *pbuf++ = '+';
      remaining_len--;
    } else {
      if (remaining_len < 4)
        return false;  // Need space for %XX + null terminator
      snprintf(pbuf, 4, "%%%02X", (unsigned char)*pstr);
      pbuf += 3;
      remaining_len -= 3;
    }
    pstr++;
  }
  *pbuf = '\0';
  return true;
}

static int _dwc_perform_get(machine_rrf_t *self, const char *path, char *buffer,
                            size_t buffer_len, int *out_status_code) {
  if (!self->transport_state.dwc.http_client) {
    LOGE(TAG, "DWC: HTTP client handle is null. This should not happen.");
    if (out_status_code) *out_status_code = -1;  // internal error
    return -1;
  }

  int status_code = 0;
  int read_len = dwc_http_get(self->transport_state.dwc.http_client, path,
                              buffer, buffer_len, &status_code);

  if (out_status_code) *out_status_code = status_code;

  if (read_len < 0) {  // Network or wrapper error
    LOGE(TAG, "DWC: dwc_http_get failed (err: %d, status: %d) for path %s",
         read_len, status_code, path);
    if (self->connected) {
      self->connected = false;
      machine_interface_connected_updated(&self->base);
    }
    return -1;
  }

  if (status_code == 401) {  // Auth error
    LOGW(TAG,
         "DWC: Unauthorized (401) for path %s. Session lost or password "
         "required.",
         path);
    if (self->connected) {
      self->connected = false;
      machine_interface_connected_updated(&self->base);
    }
  } else if (status_code != 200) {
    LOGW(TAG,
         "DWC: HTTP GET request returned non-200 status code %d for path %s",
         status_code, path);
  }

  return read_len;
}

static void _dwc_send_gcode_impl(machine_rrf_t *self, const char *gcode) {
  // Escaped gcode can be up to 3x the original length, plus null terminator.
  char path[MAX_GCODE_STR_LEN * 3 + 1] = "/rr_gcode?gcode=";
  size_t len = strlen(path);
  char *escaped_gcode = &path[len];
  if (!url_encode(gcode, escaped_gcode, sizeof(path) - len - 1)) {
    LOGE(TAG, "DWC: G-code string too long to URL encode.");
    return;
  }

  char response_buffer[64];

  int status_code = 0;
  snprintf(path, sizeof(path), "/rr_gcode?gcode=%s", escaped_gcode);
  _dwc_perform_get(self, path, response_buffer, sizeof(response_buffer),
                   &status_code);
  LOGI(TAG, "SEND G-CODE Resp: %s", response_buffer);
}

static bool _dwc_parse_json_response(machine_rrf_t *self,
                                     const char *json_response) {
  cJSON *root = cJSON_Parse(json_response);
  if (!root) {
    // Caller will log with more context
    return false;
  }
  bool success = machine_rrf_parse_m409_response(self, root);
  cJSON_Delete(root);
  return success;
}

static void _dwc_do_poll_key(machine_rrf_t *self, const char *key) {
  if (!key) return;

  // Using static buffer to save stack space in the task
  static char response_buffer[4096];
  int status_code = 0;

  LOGD(TAG, "DWC: Polling key: %s", key);
  char path[128];
  snprintf(path, sizeof(path), "/rr_model?key=%s", key);
  int len = _dwc_perform_get(self, path, response_buffer,
                             sizeof(response_buffer), &status_code);

  if (len < 0) {
    LOGW(TAG, "DWC: Request for key '%s' failed, skipping parse.", key);
    return;
  }

  if (status_code == 200) {
    if (!_dwc_parse_json_response(self, response_buffer)) {
      LOGE(TAG, "DWC: Failed to parse JSON response for key '%s'.", key);
      LOGV(TAG, "DWC: Failing JSON was: %s", response_buffer);
    }
  } else {
    LOGW(TAG, "DWC: Got non-200 status (%d) for key '%s', skipping parse.",
         status_code, key);
  }
}

static void _dwc_poll_state_impl(machine_rrf_t *self, uint32_t poll_state) {
  if (!self->connected) {
    LOGD(TAG, "DWC: Not connected, attempting reconnect during poll cycle.");
    _dwc_set_connected_impl(self, true);
    // If connection failed, self->connected is still false, so we bail.
    if (!self->connected) {
      return;
    }
  }

  if (poll_state & MACHINE_POSITION) _dwc_do_poll_key(self, "move.axes[]");
  if (poll_state & JOB_STATUS) _dwc_do_poll_key(self, "state.status");
  if (poll_state & MESSAGES_AND_DIALOGS)
    _dwc_do_poll_key(self, "state.messageBox");
  if (poll_state & SPINDLE) _dwc_do_poll_key(self, "spindles[]");
  if (poll_state & TOOLS) {
    _dwc_do_poll_key(self, "tools[]");
    _dwc_do_poll_key(self, "state.currentTool");
  }
}

static void _dwc_set_connected_impl(machine_rrf_t *self, bool connect) {
  if (self->connected == connect) return;

  char path[128];
  char response_buffer[256];
  bool success = false;
  bool has_password = self->transport_state.dwc.password &&
                      self->transport_state.dwc.password[0] != '\0';

  if (connect) {
    int status_code = 0;
    int read_len = -1;

    if (has_password) {
      // Future-proof password logic
      LOGI(TAG, "DWC: Attempting to connect with password...");
      snprintf(path, sizeof(path), "/rr_connect?password=%s",
               self->transport_state.dwc.password);
      read_len = _dwc_perform_get(self, path, response_buffer,
                                  sizeof(response_buffer), &status_code);

      if (read_len >= 0 && status_code == 200) {
        response_buffer[read_len] = '\0';
        cJSON *root = cJSON_Parse(response_buffer);
        if (root) {
          cJSON *err = cJSON_GetObjectItem(root, "err");
          if (cJSON_IsNumber(err) && err->valueint == 0) {
            success = true;
          }
          cJSON_Delete(root);
        }
      }
    } else {
      // Per RRF docs, for password-less, any request establishes a session.
      // We test the connection with a standard model request.
      LOGI(TAG, "DWC: Attempting password-less connection with a test poll.");
      snprintf(path, sizeof(path), "/rr_model?key=state.status");
      read_len = _dwc_perform_get(self, path, response_buffer,
                                  sizeof(response_buffer), &status_code);

      if (read_len >= 0 && status_code == 200) {
        success = true;  // Any successful request means we have a session
      } else if (status_code == 401) {
        LOGW(TAG,
             "DWC: Received 401 on initial connect poll. Assuming "
             "password-less session is now established. Marking as connected.");
        success = true;  // As requested, treat 401 on connect as success.
      }
    }
  } else {  // Disconnect
    snprintf(path, sizeof(path), "/rr_disconnect");
    int status_code = 0;
    _dwc_perform_get(self, path, response_buffer, sizeof(response_buffer),
                     &status_code);
    success = false;  // We are disconnecting, so success state is false
  }

  if (self->connected != success) {
    self->connected = success;
    machine_interface_connected_updated(&self->base);
    if (connect) {
      LOGI(TAG, "DWC: Connection attempt result: %s.",
           success ? "established" : "failed");
    } else {
      LOGI(TAG, "DWC: Disconnected.");
    }
  }
}

static void _dwc_list_files_impl(machine_rrf_t *self, const char *path) {
  char request_path[128];
  char response_buffer[4096];
  int status_code;

  snprintf(request_path, sizeof(request_path), "/rr_filelist?dir=%s", path);
  int len = _dwc_perform_get(self, request_path, response_buffer,
                             sizeof(response_buffer), &status_code);
  if (len > 0) {
    // TODO: Parse the file list JSON and update self->base.filelists
    // This requires a different parser than the M20 response parser.
  }
}

static void _dwc_deinit_impl(machine_rrf_t *self) {
  if (self->connected) {
    _dwc_set_connected_impl(self, false);
  }
  dwc_http_destroy(self->transport_state.dwc.http_client);
  self->transport_state.dwc.http_client = NULL;
  free(self->transport_state.dwc.host);
  self->transport_state.dwc.host = NULL;
  free(self->transport_state.dwc.password);
  self->transport_state.dwc.password = NULL;
}

static void _dwc_proc_state_resp_impl(machine_interface_t *self, void *data,
                                      size_t len) {
  // Not used in DWC's synchronous model
}

// --- Transport-Specific Implementations: Serial ---

static void _serial_send_gcode_impl(machine_rrf_t *self, const char *gcode) {
  char *gcode_copy = strdup(gcode);
  if (!gcode_copy) {
    LOGE(TAG, "Serial: Failed to allocate memory for G-code copy");
    return;
  }

  char *saveptr;
  char *line = strtok_r(gcode_copy, "\n", &saveptr);
  while (line != NULL) {
    serial_write(self->transport_state.serial.uart, (const uint8_t *)line,
                 strlen(line));
    serial_write(self->transport_state.serial.uart, (const uint8_t *)"\n", 1);
    LOGI(TAG, "Sending: %s", line);
    line = strtok_r(NULL, "\n", &saveptr);
  }

  free(gcode_copy);
}

static bool _serial_parse_json_response(machine_rrf_t *self,
                                        const char *json_response) {
   LOGE(TAG, "Serial: RESPONSE \n\n%s\n\n", json_response);
  cJSON *root = cJSON_Parse(json_response);
  if (!root) {
    LOGE(TAG, "Serial: Failed to parse JSON.");
    LOGV(TAG, "Serial: Failing JSON was: %s", json_response);
    return false;
  }

  bool succ = false;
  if (cJSON_GetObjectItemCaseSensitive(root, "key")) {
    succ = machine_rrf_parse_m409_response(self, root);
  } else {
    LOGW(TAG, "Serial: Unrecognized JSON response: %s", json_response);
  }

  cJSON_Delete(root);
  return succ;
}

static void _serial_poll_state_impl(machine_rrf_t *self, uint32_t poll_state) {
  // Process any data that has been received since the last poll.
  serial_process_input(self->transport_state.serial.uart);

  // Commands are sent via the queue. The response is handled asynchronously.
  char cmd[128];
  if (poll_state & MACHINE_POSITION) {
    snprintf(cmd, sizeof(cmd), "M409 K\"move.axes[]\" F\"d5,f\"");
    machine_interface_send_gcode(&self->base, cmd, 0);
  }
  if (poll_state & JOB_STATUS) {
    snprintf(cmd, sizeof(cmd), "M409 K\"state.status\" F\"v\"");
    machine_interface_send_gcode(&self->base, cmd, 0);
  }
  if (poll_state & MESSAGES_AND_DIALOGS) {
    snprintf(cmd, sizeof(cmd), "M409 K\"state.messageBox\" F\"v\"");
    machine_interface_send_gcode(&self->base, cmd, 0);
  }
  if (poll_state & SPINDLE) {
    snprintf(cmd, sizeof(cmd), "M409 K\"spindles[]\" F\"d2,v\"");
    machine_interface_send_gcode(&self->base, cmd, 0);
  }
  if (poll_state & TOOLS) {
    machine_interface_send_gcode(&self->base,
                                 "M409 K\"state.currentTool\" F\"v\"", 0);
    machine_interface_send_gcode(&self->base, "M409 K\"tools[]\" F\"v\"", 0);
  }
}

static void _serial_set_connected_impl(machine_rrf_t *self, bool connect) {
  if (self->connected != connect) {
    self->connected = connect;
    if (connect) {
      self->message_box_last_dismissed_seq = -99999;
    }
    machine_interface_connected_updated(&self->base);
  }
}

static void _serial_list_files_impl(machine_rrf_t *self, const char *path) {
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "M20 S2 P\"/%s/\"", path);
  machine_interface_send_gcode(&self->base, cmd, 0);
}

static void _serial_deinit_impl(machine_rrf_t *self) {
  serial_end(self->transport_state.serial.uart);
}

static void _serial_proc_state_resp_impl(machine_interface_t *iself, void *data,
                                         size_t len) {
  machine_rrf_t *self = (machine_rrf_t *)iself;
  _serial_parse_json_response(self, (const char *)data);
  if (!self->connected) {
    _serial_set_connected_impl(self, true);
    machine_interface_position_updated(&self->base);
    machine_interface_wcs_updated(&self->base);
    machine_interface_home_updated(&self->base);
  }
}

// --- Common Modal/Action Functions ---
// These format G-code and send it via the standard interface, so they
// work for both transports.

void _machine_rrf_modal_cancel(machine_interface_t *self, int modal_id) {
  _free_modal(self, modal_id);
  char buf[64];
  snprintf(buf, sizeof(buf), "M292 S%d P1", modal_id);
  machine_interface_send_gcode(self, buf, MESSAGES_AND_DIALOGS);
}

void _machine_rrf_modal_ok(machine_interface_t *self, int modal_id) {
  _free_modal(self, modal_id);
  char buf[64];
  snprintf(buf, sizeof(buf), "M292 S%d P0", modal_id);
  machine_interface_send_gcode(self, buf, MESSAGES_AND_DIALOGS);
}

void _machine_rrf_modal_choice(machine_interface_t *self, int choice,
                               int modal_id) {
  _free_modal(self, modal_id);
  char buf[64];
  snprintf(buf, sizeof(buf), "M292 S%d P0 R{%d}", modal_id, choice);
  machine_interface_send_gcode(self, buf, MESSAGES_AND_DIALOGS);
}

void _machine_rrf_modal_int(machine_interface_t *self, int val, int modal_id) {
  _free_modal(self, modal_id);
  char buf[64];
  snprintf(buf, sizeof(buf), "M292 S%d P0 R{%d}", modal_id, val);
  machine_interface_send_gcode(self, buf, MESSAGES_AND_DIALOGS);
}

void _machine_rrf_modal_float(machine_interface_t *self, float val,
                              int modal_id) {
  _free_modal(self, modal_id);
  char buf[128];
  snprintf(buf, sizeof(buf), "M292 S%d P0 R{%f}", modal_id, val);
  machine_interface_send_gcode(self, buf, MESSAGES_AND_DIALOGS);
}

void _machine_rrf_modal_str(machine_interface_t *self, const char *val,
                            int modal_id) {
  _free_modal(self, modal_id);
  char buf[256];
  snprintf(buf, sizeof(buf), "M292 S%d P1 R{\"%s\"}", modal_id, val);
  machine_interface_send_gcode(self, buf, MESSAGES_AND_DIALOGS);
}

void _machine_rrf_probe(machine_interface_t *self, const char *probe_gcode) {
  machine_interface_send_gcode(self, "M98 P\"/macros/pre-probe.g\"",
                               MACHINE_POSITION);
  machine_interface_send_gcode(self, probe_gcode, MACHINE_POSITION_EXT);
}

// --- Common Initializer ---

static machine_rrf_t *_machine_rrf_init_common(machine_rrf_t *self,
                                               uint16_t sleep_ms) {
  machine_interface_init(&self->base, sleep_ms);

  self->connected = false;
  self->input_sel = NULL;
  self->input_idx = 0;
  self->message_box_last_dismissed_seq = -99999;
  self->current_tool_idx = -1;

  // Assign generic virtual methods
  self->base._send_gcode = _machine_rrf_send_gcode;
  self->base._update_machine_state = _machine_rrf_update_machine_state;
  self->base.is_connected = _machine_rrf_is_connected;
  self->base.set_connected = _machine_rrf_set_connected;
  self->base.list_files = _machine_rrf_list_files;
  self->base.process_machine_state_response =
      _machine_rrf_proc_machine_state_response;

  // Use default g-code based implementations for these actions, which will call
  // the appropriate transport-specific `_send_gcode`
  self->base.run_macro = NULL;  // Use base impl
  self->base.start_job = NULL;  // Use base impl
  self->base.move_continuous = NULL;
  self->base.move_continuous_stop = NULL;

  // Modals
  self->base.modal_cancel = _machine_rrf_modal_cancel;
  self->base.modal_ok = _machine_rrf_modal_ok;
  self->base.modal_choice = _machine_rrf_modal_choice;
  self->base.modal_int = _machine_rrf_modal_int;
  self->base.modal_float = _machine_rrf_modal_float;
  self->base.modal_str = _machine_rrf_modal_str;
  self->base.probe = _machine_rrf_probe;

  return self;
}

// --- Public Constructors/Initializers ---

machine_rrf_t *machine_rrf_create_serial(int rrf_serial_num, uint16_t sleep_ms,
                                         int tx_pin, int rx_pin) {
  machine_rrf_t *self = (machine_rrf_t *)malloc(sizeof(machine_rrf_t));
  if (!self) return NULL;
  return machine_rrf_init_serial(self, rrf_serial_num, sleep_ms, tx_pin,
                                 rx_pin);
}

machine_rrf_t *machine_rrf_init_serial(machine_rrf_t *self, int rrf_serial_num,
                                       uint16_t sleep_ms, int tx_pin,
                                       int rx_pin) {
  _machine_rrf_init_common(self, sleep_ms);

  // Init transport state
  self->transport_state.serial.uart =
      serial_init(rrf_serial_num, 115200, CFG_SERIAL_8N1, rx_pin, tx_pin);
  if (!self->transport_state.serial.uart) {
    LOGE(TAG, "Failed to initialize serial port");
    // No need to free `self` here as caller owns it
    return NULL;
  }

  // Assign transport impls
  self->_send_gcode_impl = _serial_send_gcode_impl;
  self->_poll_state_impl = _serial_poll_state_impl;
  self->_set_connected_impl = _serial_set_connected_impl;
  self->_list_files_impl = _serial_list_files_impl;
  self->_deinit_impl = _serial_deinit_impl;
  self->_proc_state_resp_impl = _serial_proc_state_resp_impl;

  return self;
}

machine_rrf_t *machine_rrf_create_dwc(const char *host, const char *password,
                                      uint16_t sleep_ms) {
  machine_rrf_t *self = (machine_rrf_t *)malloc(sizeof(machine_rrf_t));
  if (!self) return NULL;
  return machine_rrf_init_dwc(self, host, password, sleep_ms);
}

machine_rrf_t *machine_rrf_init_dwc(machine_rrf_t *self, const char *host,
                                    const char *password, uint16_t sleep_ms) {
  _machine_rrf_init_common(self, sleep_ms);

  // Init transport state
  self->transport_state.dwc.host = strdup(host);
  self->transport_state.dwc.password = password ? strdup(password) : NULL;
  self->transport_state.dwc.http_client =
      dwc_http_create(self->transport_state.dwc.host, 80);
  if (self->transport_state.dwc.http_client == NULL) {
    LOGE(TAG, "Failed to initialize DWC HTTP client");
    free(self->transport_state.dwc.host);
    free(self->transport_state.dwc.password);
    return NULL;
  }

  // Assign transport impls
  self->_send_gcode_impl = _dwc_send_gcode_impl;
  self->_poll_state_impl = _dwc_poll_state_impl;
  self->_set_connected_impl = _dwc_set_connected_impl;
  self->_list_files_impl = _dwc_list_files_impl;
  self->_deinit_impl = _dwc_deinit_impl;
  self->_proc_state_resp_impl = _dwc_proc_state_resp_impl;

  // Attempt to connect immediately
  _dwc_set_connected_impl(self, true);

  return self;
}

// --- Destructor ---

void machine_rrf_deinit(machine_rrf_t *self) {
  if (self) {
    if (self->_deinit_impl) {
      self->_deinit_impl(self);
    }
    if (self->input_sel) {
      free((void *)self->input_sel);
    }
    machine_interface_deinit(&self->base);
  }
}

void machine_rrf_destroy(machine_rrf_t *self) {
  if (self) {
    machine_rrf_deinit(self);
    free(self);
  }
}

// --- JSON Parsing (Common) ---
// ... (The entire machine_rrf_parse_m409_response and its helpers are placed
// here, unchanged from the original machine_rrf.c)
static machine_status_t machine_status_from_rrf_string(const char *rrf_status) {
  if (strcmp(rrf_status, "updating") == 0)
    return MACHINE_STATUS_FLASHING_FIRMWARE;
  if (strcmp(rrf_status, "halted") == 0) return MACHINE_STATUS_EMERGENCY_HALTED;
  if (strcmp(rrf_status, "off") == 0) return MACHINE_STATUS_OFF;
  if (strcmp(rrf_status, "pausing") == 0) return MACHINE_STATUS_PAUSED_DEC;
  if (strcmp(rrf_status, "resuming") == 0) return MACHINE_STATUS_PAUSED_RESUME;
  if (strcmp(rrf_status, "paused") == 0) return MACHINE_STATUS_PAUSED;
  if (strcmp(rrf_status, "simulating") == 0) return MACHINE_STATUS_SIMULATING;
  if (strcmp(rrf_status, "processing") == 0) return MACHINE_STATUS_RUNNING;
  if (strcmp(rrf_status, "changingTool") == 0)
    return MACHINE_STATUS_TOOL_CHANGING;
  if (strcmp(rrf_status, "busy") == 0) return MACHINE_STATUS_BUSY;
  if (strcmp(rrf_status, "idle") == 0)
    return MACHINE_STATUS_RUNNING;  // RRF idle means ready for command, same as
                                    // our "running" state when not in a job.
  if (strcmp(rrf_status, "starting") == 0) return MACHINE_STATUS_INITIALIZING;
  return MACHINE_STATUS_UNKNOWN;
}

int _json_key_int(cJSON *parent, const char *key) {
  cJSON *val = cJSON_GetObjectItemCaseSensitive(parent, key);
  if (cJSON_IsNumber(val)) {
    return val->valueint;
  }
  return 0;
}

float _json_key_float(cJSON *parent, const char *key) {
  cJSON *val = cJSON_GetObjectItemCaseSensitive(parent, key);
  if (cJSON_IsNumber(val)) {
    return val->valuedouble;
  }
  return 0.0f;
}

const char *_json_key_str(cJSON *parent, const char *key) {
  cJSON *val = cJSON_GetObjectItemCaseSensitive(parent, key);
  if (cJSON_IsString(val)) {
    return val->valuestring;
  }
  return NULL;
}

static int machine_rrf_axis_idx(char axis) {
  switch (axis) {
    case 'X':
      return 0;
    case 'Y':
      return 1;
    case 'Z':
      return 2;
    default:
      return -1;
  }
}

void _free_modal(machine_interface_t *self, int modal_id) {
  if (self->message_box && (self->message_box->seq == modal_id)) {
#ifdef TFT_WIDTH
    if (self->message_box->user_data) {
      lv_msgbox_close((lv_obj_t *)self->message_box->user_data);
    }
#endif
    free_message_box_t(self->message_box);
    self->message_box = NULL;
    ((machine_rrf_t *)self)->message_box_last_dismissed_seq = modal_id;
  }
}

bool machine_rrf_parse_m409_response(machine_rrf_t *self, cJSON *json_obj) {
  cJSON *key_json = cJSON_GetObjectItemCaseSensitive(json_obj, "key");
  cJSON *result_json = cJSON_GetObjectItemCaseSensitive(json_obj, "result");

  if (!cJSON_IsString(key_json) || !result_json) {
    LOGW(TAG, "Invalid M409/rr_model response format");
    return false;
  }

  const char *key = key_json->valuestring;
  LOGV(TAG, "Parsing response for key: '%s'", key);

  if (strcmp(key, "move.axes") == 0 || strcmp(key, "move.axes[]") == 0) {
    // This parser needs to be more robust for DWC, which doesn't include homed
    // status
    bool pos_updated = false;
    bool home_updated = false;
    cJSON *axis_item;
    int i = 0;
    cJSON_ArrayForEach(axis_item, result_json) {
      if (i >= 3) break;
      float machine_pos = _json_key_float(axis_item, "machinePosition");
      float wcs_pos = _json_key_float(axis_item, "userPosition");
      if (self->base.position[i] != machine_pos ||
          self->base.wcs_position[i] != wcs_pos) {
        self->base.position[i] = machine_pos;
        self->base.wcs_position[i] = wcs_pos;
        pos_updated = true;
      }
      cJSON *homed_json = cJSON_GetObjectItemCaseSensitive(axis_item, "homed");
      if (cJSON_IsBool(homed_json)) {
        bool homed = cJSON_IsTrue(homed_json);
        if (self->base.axes_homed[i] != homed) {
          self->base.axes_homed[i] = homed;
          home_updated = true;
        }
      }
      i++;
    }
    if (pos_updated) machine_interface_position_updated(&self->base);
    if (home_updated) machine_interface_home_updated(&self->base);
    return true;
  } else if (strcmp(key, "state.status") == 0) {
    if (cJSON_IsString(result_json)) {
      machine_status_t new_status =
          machine_status_from_rrf_string(result_json->valuestring);
      if (self->base.machine_status != new_status) {
        self->base.machine_status = new_status;
        machine_interface_state_updated(&self->base);
      }
      return true;
    }
  } else if (strcmp(key, "state.messageBox") == 0) {
    if (cJSON_IsNull(result_json)) {
      if (self->base.message_box) {
        _free_modal(&self->base, self->base.message_box->seq);
        machine_interface_dialogs_updated(&self->base);
      }
      return true;
    }
    int seq = _json_key_int(result_json, "seq");
    if (self->base.message_box && self->base.message_box->seq == seq)
      return true;  // Already showing
    if (seq <= self->message_box_last_dismissed_seq)
      return true;  // Old message

    if (self->base.message_box)
      _free_modal(&self->base, self->base.message_box->seq);

    self->base.message_box = (message_box_t *)calloc(1, sizeof(message_box_t));
    self->base.message_box->title = strdup(_json_key_str(result_json, "title"));
    self->base.message_box->text =
        strdup(_json_key_str(result_json, "message"));
    self->base.message_box->mode =
        (message_box_mode_t)_json_key_int(result_json, "mode");
    self->base.message_box->seq = seq;
    self->base.message_box->machine = &self->base;
    machine_interface_dialogs_updated(&self->base);
    return true;
  } else if (strcmp(key, "state.currentTool") == 0) {
    if (cJSON_IsNumber(result_json)) {
      self->current_tool_idx = result_json->valueint;
    }
    return true;
  } else if (strcmp(key, "tools[]") == 0) {
    cJSON *tool_json;
    bool tool_updated = false;
    cJSON_ArrayForEach(tool_json, result_json) {
      if (_json_key_int(tool_json, "number") == self->current_tool_idx) {
        const char *tool_name = _json_key_str(tool_json, "name");
        if (tool_name &&
            (!self->base.tool || strcmp(self->base.tool, tool_name) != 0)) {
          if (self->base.tool) free((void *)self->base.tool);
          self->base.tool = strdup(tool_name);
          tool_updated = true;
        }
      }
    }
    if (tool_updated) machine_interface_spindles_tools_updated(&self->base);
    return true;
  } else if (strcmp(key, "spindles[]") == 0) {
    // Simplified parser
    if (self->base.num_spindles == 0) {
      self->base.spindles = (spindle_t *)calloc(1, sizeof(spindle_t));
      self->base.num_spindles = 1;
    }
    cJSON *spindle0 = cJSON_GetArrayItem(result_json, 0);
    if (spindle0) {
      int rpm = _json_key_int(spindle0, "current");
      if (self->base.spindles[0].rpm != rpm) {
        self->base.spindles[0].rpm = rpm;
        machine_interface_spindles_tools_updated(&self->base);
      }
    }
    return true;
  }

  LOGD(TAG, "Unhandled response key: '%s'", key);
  return false;
}

// --- Async Task Setup (Serial Only) ---

#ifdef ASYNC_RESPONSE_PROCESSING

#define MAX_SERIAL_PROC_TASKS 1

static struct {
  serial_handle_t serial;
#ifdef ESP32_HW
  QueueHandle_t queue;
#else
  gcode_queue_t *queue;
#endif
} queue_serial_map[MAX_SERIAL_PROC_TASKS] = {NULL};

static void serial_line_received_callback(serial_handle_t handle,
                                          const char *line, size_t len) {
#ifdef ESP32_HW
  QueueHandle_t task_event_queue = NULL;
#else
  gcode_queue_t *task_event_queue = NULL;
#endif
  for (size_t i = 0; i < MAX_SERIAL_PROC_TASKS; ++i) {
    if (queue_serial_map[i].serial == handle) {
      task_event_queue = queue_serial_map[i].queue;
      break;
    }
  }

  if (task_event_queue) {
    machine_response_proc_task_data_ready(task_event_queue,
                                          (const uint8_t *)line, len, true);
  }
}

bool machine_rrf_setup_response_processing_task(machine_rrf_t *self,
#ifdef ESP32_HW
                                                QueueHandle_t task_event_queue
#else
                                                gcode_queue_t *task_event_queue
#endif
) {
  // This function is only for serial transport
  if (!self->transport_state.serial.uart) return false;

  if (!serial_register_line_callback(self->transport_state.serial.uart,
                                     serial_line_received_callback)) {
    LOGE(TAG, "Failed to register serial line callback!");
    return false;
  }

  for (size_t i = 0; i < MAX_SERIAL_PROC_TASKS; ++i) {
    if (queue_serial_map[i].serial == NULL) {
      queue_serial_map[i].serial = self->transport_state.serial.uart;
      queue_serial_map[i].queue = task_event_queue;
      return true;
    }
  }

  LOGE(TAG, "Exceeded max serial processing tasks!");
  return false;
}
#endif

