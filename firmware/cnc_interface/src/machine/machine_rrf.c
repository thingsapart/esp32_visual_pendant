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

// How long (ms) without a successful serial response before the hub considers
// the machine disconnected.
#ifndef SERIAL_NO_RESPONSE_TIMEOUT_MS
#define SERIAL_NO_RESPONSE_TIMEOUT_MS 8000
#endif

// How many consecutive JSON parse failures trigger a disconnect.
#ifndef SERIAL_MAX_PARSE_FAILURES
#define SERIAL_MAX_PARSE_FAILURES 10
#endif

// If this many poll requests go unanswered, treat the machine as disconnected.
// This helps when poll back-off is active: missing several responses usually
// indicates the controller is no longer reachable rather than merely busy.
#ifndef SERIAL_DISCONNECT_UNANSWERED_POLLS
#define SERIAL_DISCONNECT_UNANSWERED_POLLS 5
#endif

// --- Poll back-off configuration ---
// Minimum unanswered poll cycles before back-off kicks in.
#define POLL_BACKOFF_THRESHOLD     2
// Base interval (ms) once back-off is active (doubles per extra unanswered).
#define POLL_BACKOFF_BASE_MS       1000
// Hard cap on the back-off interval so we stay well under the 8 s disconnect
// timeout and keep probing the controller regularly.
#define POLL_BACKOFF_MAX_MS        5000
// After this many ms without a response we "forget" all unanswered polls and
// resume normal-rate polling (the controller may have finished its operation).
#define POLL_BACKOFF_FORGET_MS     10000

// Control how raw, unparseable M409 JSON is forwarded to the pendant/client.
// 0 = send short message to client (no raw JSON)
// 1 = send full raw JSON to client and log (default/current behaviour)
// 2 = log on hub only and do not forward to client
#ifndef M409_FAILED_JSON_MODE
#define M409_FAILED_JSON_MODE 1
#endif

static void _maybe_forward_failed_json(machine_rrf_t *self,
                                      const char *json_response,
                                      const char *context) {
  (void)context;
#if M409_FAILED_JSON_MODE == 0
  char short_msg[128];
  if (context && context[0])
    snprintf(short_msg, sizeof(short_msg), "M409 parse failed (%s)", context);
  else
    snprintf(short_msg, sizeof(short_msg), "M409 parse failed");
  machine_interface_log_message_updated(&self->base, short_msg);
  LOGV(TAG, "Failed JSON (not forwarded): %s", json_response);
#elif M409_FAILED_JSON_MODE == 1
  // Log the full raw JSON locally only (do not forward to client / show toast).
  LOGW(TAG, "Raw failed JSON: %s", json_response);
#else
  // Do not forward to client; only log locally.
  LOGW(TAG, "JSON parse failed (not forwarded) [%s]: %s",
       context ? context : "", json_response);
#endif
}

#ifdef TFT_WIDTH
#include "lvgl.h"
#endif

// --- Forward Declarations ---
static machine_status_t machine_status_from_rrf_string(const char *rrf_status);
void _free_modal(machine_interface_t *self, int modal_id);
static void _dwc_set_connected_impl(machine_rrf_t *self, bool connect);
static void _serial_set_connected_impl(machine_rrf_t *self, bool connect);
static void _machine_rrf_attempt_connect(machine_interface_t *self);
static void _serial_send_gcode_impl(machine_rrf_t *self, const char *gcode);

#ifdef ESP32_HW
// millis() is provided by the Arduino framework; declare it for the C compiler.
extern unsigned long millis(void);
#endif


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
  // LOGI(TAG, "SEND G-CODE Resp: %s", response_buffer);
}

static bool _dwc_parse_json_response(machine_rrf_t *self,
                                     const char *json_response) {
  cJSON *root = cJSON_Parse(json_response);
  if (!root) {
    // Forward/log the failing JSON according to configuration
    _maybe_forward_failed_json(self, json_response, "DWC");
    return false;
  }
  bool success = machine_rrf_parse_m409_response(self, root);
  cJSON_Delete(root);

  if (success && self->base.set_connected) {
    self->base.set_connected(&self->base, true);
  }

  return success;
}

#if 0
static void _dwc_do_poll_key(machine_rrf_t *self, const char *key) {
  if (!key) return;

  char response_buffer[4096];
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
#else

static void _dwc_do_poll_key(machine_rrf_t *self, const char *key) {
}

#endif

static void _dwc_poll_state_impl(machine_rrf_t *self, uint32_t poll_state) {
  if (!self->connected) {
    LOGD(TAG, "DWC: Not connected, attempting reconnect during poll cycle.");
    _machine_rrf_attempt_connect(&self->base);
    // If connection failed, self->connected is still false, so we bail.
    if (!self->connected) {
      return;
    }
  }

  if (poll_state & MACHINE_POSITION) _dwc_do_poll_key(self, "move.axes[]");
  if (poll_state & JOB_STATUS) {
    _dwc_do_poll_key(self, "state.status");
    _dwc_do_poll_key(self, "move.currentMove");
    _dwc_do_poll_key(self, "move.speedFactor");
  }
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

#if 0
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
#else

static void _dwc_list_files_impl(machine_rrf_t *self, const char *path) { }

#endif

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
  const char *start = gcode;
  const char *end;

  // Iterate string to find newlines without allocating a copy
  while (*start) {
    end = strchr(start, '\n');
    size_t len = end ? (size_t)(end - start) : strlen(start);

    if (len > 0) {
      // Compose the full line including a single trailing newline into a
      // contiguous buffer and send atomically (write+flush) to avoid
      // interleaving from other writers.
      size_t out_len = len + 1; /* +1 for '\n' */
      char stack_buf[MAX_GCODE_STR_LEN + 2];
      char *out_buf = NULL;

      if (out_len <= sizeof(stack_buf)) {
        memcpy(stack_buf, start, len);
        stack_buf[len] = '\n';
        out_buf = stack_buf;
      } else {
        out_buf = (char *)malloc(out_len);
        if (!out_buf) {
          LOGE(TAG, "Out of memory composing gcode line");
          start = end ? end + 1 : start + len; /* advance to next */
          continue;
        }
        memcpy(out_buf, start, len);
        out_buf[len] = '\n';
      }

      serial_write_atomic(self->transport_state.serial.uart,
                          (const uint8_t *)out_buf, out_len, true);

      if (out_buf != stack_buf) free(out_buf);
    }

    if (!end) break;
    start = end + 1;
  }
}

// --- M20 response parser ---
// M20 S2 P"/gcodes" returns:
//   {"dir":"0:/gcodes/","first":0,"next":0,"files":["job.gcode","*subdir",...]
// Files is a plain string array; subdirectories are prefixed with '*'.
// Normalize the dir (strip leading "V:/" volume prefix and trailing '/')
// then find or allocate a filelists slot and call machine_interface_files_updated.
static bool _serial_parse_m20_response(machine_rrf_t *self, cJSON *json_obj) {
  cJSON *dir_json   = cJSON_GetObjectItemCaseSensitive(json_obj, "dir");
  cJSON *files_json = cJSON_GetObjectItemCaseSensitive(json_obj, "files");

  if (!cJSON_IsString(dir_json) || !cJSON_IsArray(files_json)) {
    LOGW(TAG, "M20: invalid dir/files fields");
    return false;
  }

  // Normalize "0:/gcodes/" -> "gcodes"
  const char *raw_dir = dir_json->valuestring;
  const char *dir_start = raw_dir;
  // Skip leading volume prefix like "0:/"
  if (dir_start[0] && dir_start[1] == ':' && dir_start[2] == '/') {
    dir_start += 3;
  }
  // Strip leading slashes
  while (*dir_start == '/') dir_start++;
  char fdir[128];
  strncpy(fdir, dir_start, sizeof(fdir) - 1);
  fdir[sizeof(fdir) - 1] = '\0';
  // Strip trailing slashes
  size_t fdir_len = strlen(fdir);
  while (fdir_len > 0 && fdir[fdir_len - 1] == '/') {
    fdir[--fdir_len] = '\0';
  }

  // Find or allocate a filelists slot for this directory path.
  size_t idx = MAX_FILE_LISTS;
  size_t empty_slot = MAX_FILE_LISTS;
  for (size_t i = 0; i < MAX_FILE_LISTS; ++i) {
    if (self->base.filelists[i].fdir &&
        strcmp(fdir, self->base.filelists[i].fdir) == 0) {
      idx = i;
      break;
    }
    if (!self->base.filelists[i].fdir && empty_slot == MAX_FILE_LISTS) {
      empty_slot = i;
    }
  }
  if (idx == MAX_FILE_LISTS) {
    idx = empty_slot;
  }
  if (idx == MAX_FILE_LISTS) {
    LOGW(TAG, "M20: no filelists slot available for '%s'", fdir);
    return false;
  }

  // Free existing files array.
  if (self->base.filelists[idx].files) {
    for (size_t i = 0; self->base.filelists[idx].files[i]; ++i) {
      free(self->base.filelists[idx].files[i]);
    }
    free(self->base.filelists[idx].files);
    self->base.filelists[idx].files = NULL;
  }
  if (self->base.filelists[idx].fdir) {
    free((void *)self->base.filelists[idx].fdir);
  }
  self->base.filelists[idx].fdir = strdup(fdir);

  int num_files = cJSON_GetArraySize(files_json);
  self->base.filelists[idx].files =
      (char **)calloc(num_files + 1, sizeof(char *));
  if (!self->base.filelists[idx].files) {
    LOGE(TAG, "M20: OOM for files array");
    return false;
  }

  int j = 0;
  cJSON *file_item;
  cJSON_ArrayForEach(file_item, files_json) {
    // S2 format: each item is a plain string; subdirs are prefixed with '*'.
    if (!cJSON_IsString(file_item) || !file_item->valuestring) continue;
    const char *name = file_item->valuestring;
    // Include subdirectory entries but strip the leading '*' so the UI sees
    // a plain name.  The file list is purely flat for now; navigation into
    // subdirs can be added later.
    if (name[0] == '*') name++;
    if (*name == '\0') continue;  // shouldn't happen, but be safe
    self->base.filelists[idx].files[j++] = strdup(name);
  }
  self->base.filelists[idx].files[j] = NULL;  // NULL-terminate

  LOGI(TAG, "M20: stored %d files for '%s' (slot %u)", j, fdir, (unsigned)idx);
  machine_interface_files_updated(&self->base, self->base.filelists[idx].fdir);
  return true;
}

static bool _serial_parse_json_response(machine_rrf_t *self,
                                        const char *json_response) {
  LOGD(TAG, "Serial: RESPONSE \n\n%s\n\n", json_response);
  cJSON *root = cJSON_Parse(json_response);
  if (!root) {
    self->consecutive_parse_failures++;
    _maybe_forward_failed_json(self, json_response, "Serial");
    // Treat this as a received response for back-off bookkeeping so we don't
    // overly throttle polling due to transmission/parse errors.
    if (self->unanswered_polls > 0) {
      self->unanswered_polls--;
      LOGD(TAG, "Poll backoff: unparseable response — counting against one pending poll (unanswered now: %u).",
           (unsigned)self->unanswered_polls);
    }
    if (self->consecutive_parse_failures >= SERIAL_MAX_PARSE_FAILURES) {
      LOGE(TAG, "Serial: %d consecutive parse failures — marking disconnected.",
           self->consecutive_parse_failures);
      _serial_set_connected_impl(self, false);
    }
    return false;
  }

  bool succ = false;
  if (cJSON_GetObjectItemCaseSensitive(root, "key")) {
    succ = machine_rrf_parse_m409_response(self, root);
    if (!succ) {
      LOGW(TAG, "Serial: m409 parse returned false for: %s", json_response);
    }
  } else if (cJSON_GetObjectItemCaseSensitive(root, "dir") &&
             cJSON_GetObjectItemCaseSensitive(root, "files")) {
    // M20 S2 file listing response: {"dir":"0:/gcodes/","first":0,"files":[...]}
    succ = _serial_parse_m20_response(self, root);
    if (!succ) {
      LOGW(TAG, "Serial: M20 parse returned false for: %s", json_response);
    }
  } else if (cJSON_GetObjectItemCaseSensitive(root, "seq") &&
             cJSON_GetObjectItemCaseSensitive(root, "resp")) {
    // This is a log message or simple response, not a full model query
    cJSON *resp_json = cJSON_GetObjectItemCaseSensitive(root, "resp");
    if (cJSON_IsString(resp_json) && resp_json->valuestring) {
      // Trim trailing newline if it exists
      char *resp_str = resp_json->valuestring;
      size_t len = strlen(resp_str);
      if (len > 0 && resp_str[len - 1] == '\n') {
        resp_str[len - 1] = '\0';
      }
      machine_interface_log_message_updated(&self->base, resp_str);
      succ = true;
    }
  } else {
    LOGW(TAG, "Serial: Unrecognized JSON response structure: %s", json_response);
    // Relay unexpected structures verbatim to the pendant.
    machine_interface_log_message_updated(&self->base, json_response);
  }

  cJSON_Delete(root);

  if (succ) {
#ifdef ESP32_HW
    self->last_response_ms = millis();
#endif
    // Any valid response means the controller is alive and processing — reset
    // the back-off counter so polling resumes at normal cadence.
    if (self->unanswered_polls > 0) {
      LOGD(TAG, "Poll backoff: response received — resetting from %u unanswered.",
           (unsigned)self->unanswered_polls);
      self->unanswered_polls = 0;
    }
    self->consecutive_parse_failures = 0;
    if (self->base.set_connected) {
      self->base.set_connected(&self->base, true);
    }
  } else {
    self->consecutive_parse_failures++;
    LOGD(TAG, "Serial: parse returned false (consecutive failures: %d)",
         self->consecutive_parse_failures);
    // Even if parsing/semantic handling failed, count this as a response so
    // an outstanding poll isn't treated as unanswered (avoid aggressive backoff).
    if (self->unanswered_polls > 0) {
      self->unanswered_polls--;
      LOGD(TAG, "Poll backoff: unrecognized response — counting against one pending poll (unanswered now: %u).",
           (unsigned)self->unanswered_polls);
    }
    if (self->consecutive_parse_failures >= SERIAL_MAX_PARSE_FAILURES) {
      LOGE(TAG, "Serial: %d consecutive unrecognised responses — marking disconnected.",
           self->consecutive_parse_failures);
      _serial_set_connected_impl(self, false);
    }
  }

  return succ;
}

// NOTE: Keys cannot be combined! Every key needs to be polled on its own.
// So for example `M409 K"state.status,move.currentMove,move.speedFactor,spindles[]"`
// will NOT work. It needs to broken down into 4 requests.
static void _serial_poll_state_impl(machine_rrf_t *self, uint32_t poll_state) {
  // Process any data that has been received since the last poll.
  serial_process_input(self->transport_state.serial.uart);

#ifdef ESP32_HW
  // Check for response timeout: if we have been connected but nothing has been
  // received in a while, consider the machine gone.
  if (self->connected && self->last_response_ms != 0) {
    uint32_t now = millis();
    uint32_t elapsed = now - self->last_response_ms;
    // If we've sent several poll requests with no responses, assume the
    // controller is unreachable and disconnect immediately. This handles the
    // case where back-off accumulates many unanswered polls (e.g. controller
    // crashed) while still allowing short-term back-off delays during long
    // running macros.
    if (self->unanswered_polls >= SERIAL_DISCONNECT_UNANSWERED_POLLS) {
      LOGW(TAG, "Serial: %u unanswered polls — marking disconnected.", (unsigned)self->unanswered_polls);
      _serial_set_connected_impl(self, false);
      return;  // Skip sending more queries until reconnected
    }

    // While poll back-off is active (some unanswered polls but below the
    // disconnect threshold), avoid using the raw elapsed-time check to
    // declare the machine disconnected — back-off intentionally spaces polls
    // and can exceed the simple time threshold. Only apply the elapsed-time
    // timeout when we have no outstanding unanswered polls.
    if (self->unanswered_polls == 0 && elapsed > SERIAL_NO_RESPONSE_TIMEOUT_MS) {
      LOGW(TAG, "Serial: No response for %lu ms (timeout %d ms) — marking disconnected.",
           (unsigned long)elapsed, SERIAL_NO_RESPONSE_TIMEOUT_MS);
      _serial_set_connected_impl(self, false);
      return;  // Skip sending more queries until reconnected
    }
  }
#endif

  if (!self->connected) {
    // Not connected: probe with a lightweight status query every poll cycle.
    // Responses are handled asynchronously; _serial_set_connected_impl will
    // mark us connected once valid JSON arrives.
    LOGD(TAG, "Serial: Not connected, sending probe query.");
    machine_interface_send_gcode(&self->base, "M409 K\"state.status\" F\"v\"", 0);
    return;
  }

#ifdef ESP32_HW
  // --- Poll back-off: avoid spamming M409 while the controller is busy ---
  // When we have sent polls but received no responses, progressively slow down.
  {
    uint32_t now_bo = millis();
    uint32_t since_last_poll = now_bo - self->last_poll_sent_ms;

    // Forget mechanism: if nothing heard for POLL_BACKOFF_FORGET_MS, reset and
    // resume normal-rate polling (the controller may have finished).
    if (self->unanswered_polls > 0 && since_last_poll > POLL_BACKOFF_FORGET_MS) {
      LOGD(TAG, "Poll backoff: %lu ms since last poll — forgetting %u unanswered.",
           (unsigned long)since_last_poll, (unsigned)self->unanswered_polls);
      self->unanswered_polls = 0;
    }

    if (self->unanswered_polls >= POLL_BACKOFF_THRESHOLD) {
      // Exponential back-off: 1 s, 2 s, 4 s, … capped at POLL_BACKOFF_MAX_MS.
      uint8_t shift = self->unanswered_polls - POLL_BACKOFF_THRESHOLD;  // 0, 1, 2, …
      if (shift > 3) shift = 3;  // cap the shift to avoid overflow
      uint32_t backoff_ms = POLL_BACKOFF_BASE_MS << shift;
      if (backoff_ms > POLL_BACKOFF_MAX_MS) backoff_ms = POLL_BACKOFF_MAX_MS;

      if (since_last_poll < backoff_ms) {
        LOGD(TAG, "Poll backoff: skipping poll (%u unanswered, need %lu ms, only %lu ms elapsed).",
             (unsigned)self->unanswered_polls, (unsigned long)backoff_ms,
             (unsigned long)since_last_poll);
        return;  // Skip this poll cycle
      }
    }
  }
#endif

  // Commands are sent via the queue. The response is handled asynchronously.
  char cmd[128];
  if ((poll_state & MACHINE_POSITION) || (poll_state & MACHINE_POSITION_EXT)) {
    // Use verbose flag ("v") so infrequently-changing fields like "homed" are
    // always present (with "f" RRF omits them). Use d3 instead of d5 to exclude
    // workplaceOffsets[9] and other deep arrays — the response at d5 exceeds
    // the serial line buffer and is silently discarded. All fields we parse
    // (machinePosition, userPosition, homed, letter) are at depth 1-2.
    snprintf(cmd, sizeof(cmd), "M409 K\"move.axes[]\" F\"d3,v\"");
    machine_interface_send_gcode(&self->base, cmd, 0);
  }
  if (poll_state & JOB_STATUS) {
    machine_interface_send_gcode(&self->base, "M409 K\"state.status\" F\"v\"", 0);
    machine_interface_send_gcode(&self->base, "M409 K\"move.currentMove\" F\"v\"", 0);
    machine_interface_send_gcode(&self->base, "M409 K\"move.speedFactor\" F\"v\"", 0);
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
  // Periodic file listing: issue M20 for gcodes and macros directories.
  if (poll_state & LIST_FILES) {
    LOGI(TAG, "Serial: listing gcodes (M20)");
    _machine_rrf_list_files(&self->base, "gcodes");
  }
  if (poll_state & LIST_MACROS) {
    LOGI(TAG, "Serial: listing macros (M20)");
    _machine_rrf_list_files(&self->base, "macros");
  }

#ifdef ESP32_HW
  // Track that we just sent a poll cycle for back-off bookkeeping.
  self->last_poll_sent_ms = millis();
  if (self->unanswered_polls < 255) self->unanswered_polls++;
  LOGD(TAG, "Poll sent (unanswered: %u).", (unsigned)self->unanswered_polls);
#endif
}

static void _serial_set_connected_impl(machine_rrf_t *self, bool connect) {
  if (self->connected != connect) {
    self->connected = connect;
    if (connect) {
      self->message_box_last_dismissed_seq = -99999;
      self->consecutive_parse_failures = 0;
#ifdef ESP32_HW
      self->last_response_ms = millis();
#endif
      LOGI(TAG, "Serial: Connected.");
      // Immediately request file listings so the pendant has files without
      // waiting up to 9973 poll cycles (~minutes at typical poll rates).
      _machine_rrf_list_files(&self->base, "gcodes");
      _machine_rrf_list_files(&self->base, "macros");
    } else {
      self->last_response_ms = 0;
    }
    machine_interface_connected_updated(&self->base);
  }
}

static void _serial_list_files_impl(machine_rrf_t *self, const char *path) {
  char cmd[128];
  // S2 = JSON format; path without trailing slash per RRF docs.
  snprintf(cmd, sizeof(cmd), "M20 S2 P\"/%s\"", path);
  machine_interface_send_gcode(&self->base, cmd, 0);
}

static void _serial_deinit_impl(machine_rrf_t *self) {
  serial_end(self->transport_state.serial.uart);
}

static void _serial_proc_state_resp_impl(machine_interface_t *iself, void *data,
                                         size_t len) {
  machine_rrf_t *self = (machine_rrf_t *)iself;
  bool was_connected = self->connected;
  bool parsed_ok = _serial_parse_json_response(self, (const char *)data);

  // On first successful parse after a disconnected period, push full state so
  // clients get a complete picture immediately.
  if (parsed_ok && !was_connected && self->connected) {
    machine_interface_position_updated(&self->base);
    machine_interface_wcs_updated(&self->base);
    machine_interface_home_updated(&self->base);
  }
}

// --- Non-Async Serial Response Dispatching ---
// When ASYNC_RESPONSE_PROCESSING is not defined there is no task queue and no
// call to machine_rrf_setup_response_processing_task(), so no line callback is
// ever registered.  The table below provides the same service synchronously:
// responses are parsed inline when serial_process_input() drains the ring
// buffer inside _serial_poll_state_impl().
#ifndef ASYNC_RESPONSE_PROCESSING

#define MAX_SERIAL_SYNC_INSTANCES 2

static struct {
  serial_handle_t serial;
  machine_rrf_t  *self;
} g_sync_serial_map[MAX_SERIAL_SYNC_INSTANCES];  // zero-initialised (static)

static void _serial_line_received_sync_cb(serial_handle_t handle,
                                          const char *line, size_t len) {
  for (size_t i = 0; i < MAX_SERIAL_SYNC_INSTANCES; ++i) {
    if (g_sync_serial_map[i].serial == handle) {
      _serial_proc_state_resp_impl(&g_sync_serial_map[i].self->base,
                                   (void *)line, len);
      return;
    }
  }
}

#endif  // !ASYNC_RESPONSE_PROCESSING

static void _machine_rrf_attempt_connect(machine_interface_t *self) {
    machine_rrf_t *rrf_self = (machine_rrf_t *)self;
    if (rrf_self->connected) return;

    // Distinguish transport type by checking which implementation is used.
    if (rrf_self->_poll_state_impl == _serial_poll_state_impl) {
        // Guard against a NULL uart handle (e.g. serial init failed or not yet
        // assigned).  Calling serial_process_input(NULL) dereferences the
        // handle and crashes.
        if (!rrf_self->transport_state.serial.uart) return;
        serial_process_input(rrf_self->transport_state.serial.uart);
        machine_interface_send_gcode(self, "M409 K\"state.status\" F\"v\"", 0);
    } else if (rrf_self->_poll_state_impl == _dwc_poll_state_impl) {
        // For DWC, the set_connected function handles the connection attempt.
        if (rrf_self->_set_connected_impl) {
            rrf_self->_set_connected_impl(rrf_self, true);
        }
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
  // NOTE: Do NOT re-select T{global.mosPTID} here. Sending T{...} triggers the
  // full tpre.g tool-change sequence (blocking M291 dialog + M8002 wait up to
  // 30 s), which hangs the machine mid-probe-wizard. The wizard already
  // ensures the probe tool is selected and activated before calling this.
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
  self->last_response_ms = 0;
  self->consecutive_parse_failures = 0;
  self->last_poll_sent_ms = 0;
  self->unanswered_polls = 0;

  // Assign generic virtual methods
  self->base._send_gcode = _machine_rrf_send_gcode;
  self->base._update_machine_state = _machine_rrf_update_machine_state;
  self->base.is_connected = _machine_rrf_is_connected;
  self->base.attempt_connect = _machine_rrf_attempt_connect;
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

  /* Respect transport-specific poll/backoff policy (RRF serial implements backoff). */
  self->base.should_poll = NULL; /* will be set below for serial/dwc specific */

  return self;
}

// Transport-specific poll decision: respects unanswered poll back-off.
static bool _machine_rrf_should_poll(machine_interface_t *iself) {
  machine_rrf_t *self = (machine_rrf_t *)iself;
#ifdef ESP32_HW
  uint32_t now = millis();
  uint32_t since_last_poll = now - self->last_poll_sent_ms;

  if (self->unanswered_polls > 0 && since_last_poll > POLL_BACKOFF_FORGET_MS) {
    LOGD(TAG, "Poll backoff: %lu ms since last poll — forgetting %u unanswered.",
         (unsigned long)since_last_poll, (unsigned)self->unanswered_polls);
    self->unanswered_polls = 0;
  }

  if (self->unanswered_polls >= POLL_BACKOFF_THRESHOLD) {
    uint8_t shift = self->unanswered_polls - POLL_BACKOFF_THRESHOLD;
    if (shift > 3) shift = 3;
    uint32_t backoff_ms = POLL_BACKOFF_BASE_MS << shift;
    if (backoff_ms > POLL_BACKOFF_MAX_MS) backoff_ms = POLL_BACKOFF_MAX_MS;
    if (since_last_poll < backoff_ms) {
      LOGD(TAG, "Poll backoff: skipping poll (%u unanswered, need %lu ms, only %lu ms elapsed).",
           (unsigned)self->unanswered_polls, (unsigned long)backoff_ms,
           (unsigned long)since_last_poll);
      return false;
    }
  }
#endif
  return true;
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

  /* RRF-specific poll/backoff hook */
  self->base.should_poll = _machine_rrf_should_poll;

#ifndef ASYNC_RESPONSE_PROCESSING
  // Register a synchronous line callback so serial responses are parsed
  // immediately in serial_process_input() rather than being silently dropped.
  for (size_t i = 0; i < MAX_SERIAL_SYNC_INSTANCES; ++i) {
    if (g_sync_serial_map[i].serial == NULL) {
      g_sync_serial_map[i].serial = self->transport_state.serial.uart;
      g_sync_serial_map[i].self   = self;
      if (!serial_register_line_callback(self->transport_state.serial.uart,
                                         _serial_line_received_sync_cb)) {
        LOGE(TAG, "Failed to register sync serial line callback!");
      } else {
        LOGI(TAG, "Sync serial line callback registered for UART handle %p",
             self->transport_state.serial.uart);
      }
      break;
    }
  }
#endif

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

  /* RRF-specific poll/backoff hook */
  self->base.should_poll = _machine_rrf_should_poll;

  // Attempt to connect immediately
  _machine_rrf_attempt_connect(&self->base);

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
  if (strcmp(rrf_status, "busy") == 0) return MACHINE_STATUS_RUNNING;
  if (strcmp(rrf_status, "idle") == 0)
    return MACHINE_STATUS_IDLE;
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
    bool pos_updated = false;
    bool home_updated = false;
    cJSON *axis_item;
    int i = 0;
    cJSON_ArrayForEach(axis_item, result_json) {
      if (i >= 3) break;
      cJSON *item;
      item = cJSON_GetObjectItemCaseSensitive(axis_item, "machinePosition");
      if (item && cJSON_IsNumber(item)) {
        float machine_pos = item->valuedouble;
        if (fabsf(self->base.position[i] - machine_pos) > 1e-5) {
          self->base.position[i] = machine_pos;
          pos_updated = true;
        }
      }
      item = cJSON_GetObjectItemCaseSensitive(axis_item, "userPosition");
      if (item && cJSON_IsNumber(item)) {
        float wcs_pos = item->valuedouble;
        if (fabsf(self->base.wcs_position[i] - wcs_pos) > 1e-5) {
          self->base.wcs_position[i] = wcs_pos;
          pos_updated = true;
        }
      }
      item = cJSON_GetObjectItemCaseSensitive(axis_item, "min");
      if (item && cJSON_IsNumber(item)) {
        self->base.axis_min[i] = (float)item->valuedouble;
      }
      item = cJSON_GetObjectItemCaseSensitive(axis_item, "max");
      if (item && cJSON_IsNumber(item)) {
        self->base.axis_max[i] = (float)item->valuedouble;
      }

      item = cJSON_GetObjectItemCaseSensitive(axis_item, "homed");
      if (!item || cJSON_IsNull(item)) {
        LOGW(TAG, "axis[%d] 'homed' field missing from M409 response (RRF version mismatch?)", i);
      } else {
        bool homed = false;
        bool homed_valid = false;
        if (cJSON_IsBool(item)) {
          homed = cJSON_IsTrue(item);
          homed_valid = true;
        } else if (cJSON_IsNumber(item)) {
          // RRF 3.6+ may return 0/1 instead of false/true
          homed = (item->valueint != 0);
          homed_valid = true;
        } else {
          LOGW(TAG, "axis[%d] 'homed' has unexpected JSON type (%d)", i, item->type);
        }
        if (homed_valid) {
          LOGD(TAG, "axis[%d] homed=%d", i, (int)homed);
          if (self->base.axes_homed[i] != homed) {
            self->base.axes_homed[i] = homed;
            home_updated = true;
          }
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
    if (!self->base.message_box) {
        LOGE(TAG, "Failed to allocate memory for message box");
        return false;
    }
    const char *title_str = _json_key_str(result_json, "title");
    self->base.message_box->title = title_str ? strdup(title_str) : strdup("");
    const char *message_str = _json_key_str(result_json, "message");
    self->base.message_box->text = message_str ? strdup(message_str) : strdup("");
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
    bool updated = false;
    if (self->base.num_spindles == 0) {
      self->base.spindles = (spindle_t *)calloc(1, sizeof(spindle_t));
      if (self->base.spindles) self->base.num_spindles = 1;
    }
    cJSON *spindle0 = cJSON_GetArrayItem(result_json, 0);
    if (spindle0) {
      cJSON *item = cJSON_GetObjectItemCaseSensitive(spindle0, "current");
      if (item && cJSON_IsNumber(item)) {
        int rpm = item->valueint;
        if (self->base.spindles[0].rpm != rpm) {
          self->base.spindles[0].rpm = rpm;
          updated = true;
        }
      }
    }
    if (updated) machine_interface_spindles_tools_updated(&self->base);
    return true;
  } else if (strcmp(key, "move.currentMove") == 0) {
    bool updated = false;
    cJSON *item;
    item = cJSON_GetObjectItemCaseSensitive(result_json, "requestedSpeed");
    if (item && cJSON_IsNumber(item)) {
      float feed_req = item->valuedouble;
      if (fabsf(self->base.feed_req - feed_req) > 1e-5) {
        self->base.feed_req = feed_req;
        updated = true;
      }
    }
    item = cJSON_GetObjectItemCaseSensitive(result_json, "topSpeed");
    if (item && cJSON_IsNumber(item)) {
      float feed = item->valuedouble * 60; // mm/s to mm/min
      if (fabsf(self->base.feed - feed) > 1e-5) {
        self->base.feed = feed;
        updated = true;
      }
    }
    if (updated) machine_interface_feed_updated(&self->base);
    return true;
  } else if (strcmp(key, "move.speedFactor") == 0) {
    if (cJSON_IsNumber(result_json)) {
      float multiplier = result_json->valuedouble / 100.0f;
      if (fabsf(self->base.feed_multiplier - multiplier) > 1e-5) {
        self->base.feed_multiplier = multiplier;
        machine_interface_feed_updated(&self->base);
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
