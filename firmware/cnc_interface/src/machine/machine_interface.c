#define UI_DEBUG_LOCAL_LEVEL D_ERROR
#include "debug.h"

#include "machine_interface.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

static const char *TAG = "machine_interface";  // Used for logging
static const char axes[] = {'X', 'Y', 'Z', '\0'};

#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#else
#include <unistd.h>
#define vTaskDelay(ms) usleep(ms * 1000)
#define pdMS_TO_TICKS(ms) (ms)
#ifdef ASYNC_GCODE_SENDING
#include "compat/queue.h"
#endif
#endif

#define call_callbacks(cbs_name)                                       \
  do {                                                                 \
    for (int i = 0; i < MAX_CALLBACKS; i++) {                          \
      if (self->cbs_name[i].cb_fn) {                                   \
        (*self->cbs_name[i].cb_fn)(self, self->cbs_name[i].user_data); \
      }                                                                \
    }                                                                  \
  } while (0)

#define call_callbacks_with_args(cbs_name, ...)                       \
  do {                                                                \
    for (int i = 0; i < MAX_CALLBACKS; i++) {                         \
      if (self->cbs_name[i].cb_fn) {                                  \
        (*self->cbs_name[i].cb_fn)(self, self->cbs_name[i].user_data, \
                                   __VA_ARGS__);                      \
      }                                                               \
    }                                                                 \
  } while (0)

#ifdef ASYNC_GCODE_SENDING
#ifndef ESP32_HW
static void machinte_interface_send_gcode(machine_interface_t *self,
                                          const char *gcode,
                                          poll_state_t poll_state) {
  gcode_queue_push(self->gcode_queue, gcode);
  self->poll_state = (uint32_t)self->poll_state | poll_state;
  LOGV(TAG, "gcode queued: %s", gcode);
}
#endif
#endif

// --- Default "Virtual" Method Implementations ---
// These are the default implementations that can be overridden.

static void _default_send_gcode(machine_interface_t *self, const char *gcode) {
  // Default implementation:  Just log the G-code.  Replace with actual sending
  // logic.
  LOGV(TAG, "Sending G-code (default): %s\n", gcode);
  //  _childclass_override(); // This is how you'd call a "virtual" method
}

static void _default_update_machine_state(machine_interface_t *self,
                                          uint32_t poll_state) {
  // Default implementation:  Simulate some state changes.
  LOGV(TAG, "Updating machine state (default), poll_state: %d",
       (int)poll_state);
}

static bool _default_is_connected(machine_interface_t *self) {
  return false;  // Default: Not connected
}

static void _default_list_files(machine_interface_t *self, const char *path) {
  LOGV(TAG, "Listing files (default) in: %s", path);
}

static void _default_run_macro(machine_interface_t *self,
                               const char *macro_name) {
  LOGI(TAG, "Running macro (default): %s", macro_name);
}

static void _default_start_job(machine_interface_t *self,
                               const char *job_name) {
  LOGI(TAG, "Starting job (default): %s", job_name);
}

static void _default_move_to(machine_interface_t *self, const char axis,
                             float feed, float value, bool relative) {
  int axi = machine_interface_axis_idx(self, axis);

  const char *mode = relative ? "G91" : "G90";
  float pos = value;
  if (relative) {
    self->moving_target_position[axi] = self->wcs_position[axi] + value;
  } else {
    self->moving_target_position[axi] = value;
    pos = self->moving_target_position[axi];
  }
  char gcode_cmd[64];  // Buffer for the formatted G-code
#ifdef CONTROLLER_BENCH_TEST
  snprintf(gcode_cmd, sizeof(gcode_cmd), "G92 %c%.3f F%.3f\n", mode, axis,
           self->moving_target_position[axi], feed);
#else
  snprintf(gcode_cmd, sizeof(gcode_cmd), "M120\n%s\nG1 %c%.3f F%.3f\nM121",
           mode, axis, pos, feed);
#endif
  self->send_gcode(self, gcode_cmd, MACHINE_POSITION);
}

static void _default_continuous_move(machine_interface_t *self, const char axis,
                                     float feed, int direction) {
  LOGI(TAG, "Continuous move (default): axis=%c, feed=%f, direction=%d", axis,
       feed, direction);
}

static void _default_continuous_stop(machine_interface_t *self) {
  LOGI(TAG, "Continuous stop (default)");
}

static void _default_move(machine_interface_t *self, const char axis,
                          float feed, float value) {
  LOGI(TAG, "Move (default): axis=%c, feed=%f, value=%f", axis, feed, value);
  _default_move_to(self, axis, feed, value, true);  // Default to relative move
}

static void _default_home_all(machine_interface_t *self) {
  self->send_gcode(self, "G28", MACHINE_POSITION);
}

static void _default_home(machine_interface_t *self, const char *axes) {
  char gcode_cmd[32];  // Buffer for G-code command
  snprintf(gcode_cmd, sizeof(gcode_cmd), "G28 %s", axes);
  self->send_gcode(self, gcode_cmd, MACHINE_POSITION);
}

static void _default_set_wcs(machine_interface_t *self, int wcs) {
  char gcode_cmd[16];
  snprintf(gcode_cmd, sizeof(gcode_cmd), "%s",
           machine_interface_get_wcs_str(self, wcs % 9));
  self->send_gcode(self, gcode_cmd, MACHINE_POSITION);
}

static void _default_set_wcs_zero(machine_interface_t *self, int wcs,
                                  const char *axes) {
  char gcode_cmd[64];
  char zer[32] = "";  // Buffer for the zeroing part
  for (const char *p = axes; *p; p++) {
    snprintf(zer + strlen(zer), sizeof(zer) - strlen(zer), "%c0 ", *p);
  }
  snprintf(gcode_cmd, sizeof(gcode_cmd), "G10 L20 P%d %s", wcs, zer);
  self->send_gcode(self, gcode_cmd, MACHINE_POSITION);
}

static void _default_next_wcs(machine_interface_t *self) {
  _default_set_wcs(self, self->wcs + 1);
}

static char *_default_debug_print(machine_interface_t *self) {
  // Create a static buffer to hold the debug string.  This is thread-safe
  // because it's static and read-only after initialization.  Adjust size as
  // needed.
  char *debug_str = (char *)malloc(sizeof(char) * 512);

  snprintf(debug_str, sizeof(debug_str),
           "{ status: %d, homed: [%d, %d, %d], pos: [%f, %f, %f], wcs_pos: "
           "[%f, %f, %f], wcs: %d, tool: %s, feedm: %f, zoffs: %f, "
           "gcode_q_count: %zu, poll_state: %d }",
           self->machine_status, self->axes_homed[0], self->axes_homed[1],
           self->axes_homed[2], self->position[0], self->position[1],
           self->position[2], self->wcs_position[0], self->wcs_position[1],
           self->wcs_position[2], self->wcs,
           self->tool ? self->tool : "None",  // Handle NULL tool
           self->feed_multiplier, self->z_offs,
#if defined(ASYNC_GCODE_SENDING) && !defined(ESP32_HW)
           gcode_queue_count(self->gcode_queue),
#else
           0,
#endif
           (int)self->poll_state);
  return debug_str;
}

// --- Constructor ---

machine_interface_t *machine_interface_create(uint16_t procrate_ms) {
  machine_interface_t *self =
      (machine_interface_t *)malloc(sizeof(machine_interface_t));
  return machine_interface_init(self, procrate_ms);
}

machine_interface_t *machine_interface_init(machine_interface_t *self,
                                            uint16_t procrate_ms) {
  if (!self) {
    LOGE(TAG, "Failed to allocate memory for machine_interface");
    return NULL;  // Indicate failure
  }
  memset(self, 0, sizeof(*self));

  // Initialize members
  self->procrate_ms = procrate_ms;
  self->poll_state = (uint32_t)MACHINE_POSITION | SPINDLE | PROBES | TOOLS |
                     MESSAGES_AND_DIALOGS | END_STOPS;
  self->machine_status = MACHINE_STATUS_UNKNOWN;
  memset(self->axes_homed, 0,
         sizeof(self->axes_homed));  // Initialize all axes to not homed
  memset(self->position, 0,
         sizeof(self->position));  // Initialize positions to 0
  memset(self->wcs_position, 0, sizeof(self->wcs_position));
  memset(self->target_position, 0, sizeof(self->target_position));
  memset(self->moving_target_position, 0, sizeof(self->moving_target_position));

  self->wcs = 1;
  self->tool = NULL;  // Initialize tool to NULL
  self->z_offs = 0.0f;
  self->feed = 0.0f;
  self->feed_req = 0.0f;
  self->feed_multiplier = 1.0f;
  self->move_relative = false;  // Default value
  self->move_step = false;      // Default value

  // Initialize probes, end_stops, spindles, etc. to NULL and 0.  You'll need to
  // allocate these dynamically later if you use them.
  self->probes = NULL;
  self->num_probes = 0;
  self->end_stops = NULL;
  self->num_end_stops = 0;
  self->spindles = NULL;
  self->num_spindles = 0;

#ifdef ASYNC_GCODE_SENDING
#ifdef ESP32_HW
  self->gcode_queue = NULL;
#else
  self->gcode_queue = NULL;
#endif
#endif

  // Set default "virtual" method implementations
  self->send_gcode = machine_interface_send_gcode;
  self->_send_gcode = _default_send_gcode;
  self->_update_machine_state = _default_update_machine_state;
  self->is_connected = _default_is_connected;
  self->list_files = _default_list_files;
  self->run_macro = _default_run_macro;
  self->start_job = _default_start_job;
  self->_move_to = _default_move_to;
  self->move_continuous = _default_continuous_move;
  self->move_continuous_stop = _default_continuous_stop;
  self->_continuous_move = _default_continuous_move;  // Internal version
  self->_continuous_stop = _default_continuous_stop;  // Internal version
  self->move = _default_move;
  self->home_all = _default_home_all;
  self->home = _default_home;
  self->set_wcs = _default_set_wcs;
  self->set_wcs_zero = _default_set_wcs_zero;
  self->next_wcs = _default_next_wcs;
  self->debug_print = _default_debug_print;
  self->modal_cancel = NULL;
  self->modal_ok = NULL;
  self->modal_choice = NULL;
  self->modal_int = NULL;
  self->modal_float = NULL;
  self->modal_str = NULL;

  self->polli = -1;
  self->last_continuous_tick = 0;

  self->current_move_axis = AXIS_OFF;
  self->current_move_step_xy = 1.0;
  self->current_move_step_z = 0.1;

  return self;
}

// --- Destructor ---

void machine_interface_deinit(machine_interface_t *self) {
  if (self) {
    // Free any dynamically allocated resources here (e.g., probes, end_stops,
    // etc.)
    free(self->probes);
    free(self->end_stops);
    free(self->spindles);
    free_message_box_t(self->message_box);

    // Free the tool string if it was dynamically allocated
    if (self->tool) {
      free((void *)self->tool);  // Cast away const for freeing
    }
  }
}

void machine_interface_destroy(machine_interface_t *self) {
  if (self) {
    machine_interface_deinit(self);
    free(self);
  }
}

// --- Other Method Implementations ---

bool machine_interface_is_homed(machine_interface_t *self, const char *axes) {
  if (!axes) {
    // If axes is NULL, check if all axes are homed
    for (int i = 0; i < 3; i++) {
      if (!self->axes_homed[i]) {
        return false;
      }
    }
    return true;
  } else {
    // Check if specified axes are homed
    for (const char *p = axes; *p; p++) {
      int axis_index = machine_interface_axis_idx(self, *p);
      if (axis_index >= 0 && axis_index < 3) {
        if (!self->axes_homed[axis_index]) {
          LOGI(TAG, "Axis not homed: %c", *p);
          return false;
        }
      } else {
        LOGI(TAG, , "Invalid axis: %c", *p);  // Log warning for invalid axis
        return false;  // Consider invalid axis as not homed
      }
    }
    return true;
  }
}

const char *machine_interface_get_wcs_str(machine_interface_t *self,
                                          int wcs_offs) {
  static char wcs_str[8];  // Static buffer to hold the WCS string
  int wcsi =
      (wcs_offs == -1) ? self->wcs : wcs_offs;  // Use -1 to indicate self->wcs

  if (wcsi >= 1 && wcsi <= 5) {
    snprintf(wcs_str, sizeof(wcs_str), "G%d", 53 + wcsi);
  } else if (wcsi >= 6 && wcsi <= 9) {
    snprintf(wcs_str, sizeof(wcs_str), "G59.%d", wcsi - 5);
  } else {
    strcpy(wcs_str, "G54");  // Default to G54 if out of range
  }
  return wcs_str;
}

int machine_interface_axis_idx(machine_interface_t *self, char axis) {
  switch (axis) {
    case 'X':
    case 'x':
      return 0;
    case 'Y':
    case 'y':
      return 1;
    case 'Z':
    case 'z':
      return 2;
    default:
      return -1;  // Indicate invalid axis
  }
}

int machine_interface_axis_t_idx(axis_t axis) {
  switch (axis) {
    case AXIS_X:
      return 0;
    case AXIS_Y:
      return 1;
    case AXIS_Z:
      return 2;
    default:
      return -1;  // Indicate invalid axis
  }
}

char idx_to_axis(int i) { return axes[i]; }

#ifndef ASYNC_GCODE_SENDING
void machine_interface_send_gcode(machine_interface_t *self, const char *gcode,
                                  uint32_t poll_state) {
  if (!gcode_queue_push(&self->gcode_queue, gcode)) {
    LOGE(TAG, "Failed to add gcode to the queue");
  }
  self->poll_state = (uint32_t)self->poll_state | poll_state;
  LOGV(TAG, "gcode queued: %s", gcode);
}

void machine_interface_process_gcode_q(machine_interface_t *self) {
  char gcode[MAX_GCODE_STR_LEN];  // Buffer to hold the popped G-code command
  while (gcode_queue_pop(&self->gcode_queue, gcode)) {
    self->_send_gcode(self, gcode);
    // Add response handling here if needed
  }
}
#else
void machine_interface_send_gcode(machine_interface_t *self, const char *gcode,
                                  uint32_t poll_state) {
  if (self->gcode_queue == NULL) {
    LOGW(TAG, "WARNING: GCode Queue is not initialized!");
    return;
  }

  size_t len = strlen(gcode);
  if (len >= MAX_GCODE_STR_LEN) {
    LOGW(TAG, "WARNING: Gcode to send exceeds MAX_GCODE_STR_LEN, truncating!");
  }

  LOGI(TAG, "Sending gcode: %s", gcode);
  char buf[MAX_GCODE_STR_LEN];
  memset(buf, 0, MAX_GCODE_STR_LEN);
  memcpy(buf, gcode, len + 1);
  buf[len] = '\0';

#ifdef ESP32_HW
  BaseType_t result = xQueueSend(self->gcode_queue, buf, 0);
  if (result != pdTRUE) {
    LOGW(TAG, "Failed to add gcode to the queue: %s", gcode);
  }
#else
  bool result = gcode_queue_push(self->gcode_queue, buf);
  if (!result) {
    LOGW(TAG, "Failed to add gcode to the queue: %s", gcode);
  }
#endif

  self->poll_state = (uint32_t)self->poll_state | poll_state;
  LOGV(TAG, "gcode queued: %s", gcode);
}

void machine_interface_process_gcode_q(machine_interface_t *self) {
  // NOP - this is done in task in async mode.
}
#endif

uint32_t machine_interface_next_poll_state(machine_interface_t *self) {
  uint32_t poll_state =
      (self->polli % 19 == 0) ? MACHINE_POSITION_EXT : MACHINE_POSITION;
  if (self->polli % 3 == 0) poll_state |= JOB_STATUS;
  if (self->polli % 5 == 0) poll_state |= MESSAGES_AND_DIALOGS;
  if (self->polli % 7 == 0) poll_state |= PROBES;
  if (self->polli % 11 == 0) poll_state |= END_STOPS;
  if (self->polli % 13 == 0) poll_state |= SPINDLE;
  if (self->polli % 17 == 0) poll_state |= TOOLS;
  if (self->polli % 9973 == 0) poll_state |= (LIST_MACROS | LIST_FILES);
  return poll_state;
}

axis_t machine_interface_move_current_axis(machine_interface_t *self,
                                           float feed, float value,
                                           bool relative) {
  if (self->current_move_axis == AXIS_OFF) {
    return self->current_move_axis;
  }
  int axi = machine_interface_axis_t_idx(self->current_move_axis);
  if (axi < 0 || axi >= AXIS_OFF) {
    return self->current_move_axis;
  }

  char axis = idx_to_axis(axi);
  LOGI(TAG, ">> MOVE_CURR_AX: %d, %c [%c, %c, %c].\n", axi, axis, axes[0],
       axes[1], axes[2]);
  _default_move_to(self, axis, feed, value, relative);
  return self->current_move_axis;
}

axis_t machine_interface_step_current_axis(machine_interface_t *self,
                                           float feed, int steps) {
  LOGI(TAG, "> machine_interface_step_current_axis (AX_OFF = %d)",
       self->current_move_axis == AXIS_OFF);
  if (self->current_move_axis == AXIS_OFF) {
    return self->current_move_axis;
  }
  int axi = machine_interface_axis_t_idx(self->current_move_axis);
  LOGI(TAG, "  axi: %d", axi);
  if (axi < 0 || axi >= AXIS_OFF) {
    return self->current_move_axis;
  }

  char axis = idx_to_axis(axi);
  LOGI(TAG, "  axis: %c", axis);

  float step_dist;
  if (axi == 2) {  // AXIS_Z
    step_dist = self->current_move_step_z;
  } else {  // AXIS_X or AXIS_Y
    step_dist = self->current_move_step_xy;
  }
  float dist = step_dist * steps;
  LOGI(TAG, ">> MOVE_CURR_AX: %d, %c [%c, %c, %c].\n", axi, axis, axes[0],
       axes[1], axes[2]);
  _default_move_to(self, axis, feed, dist, true);
  LOGI(TAG, "< machine_interface_step_current_axis");
  return self->current_move_axis;
}

axis_t machine_interface_next_move_axis(machine_interface_t *self) {
  size_t i = (size_t)self->current_move_axis;
  i = (i + 1) % (AXIS_OFF + 1);
  self->current_move_axis = (axis_t)i;

  machine_interface_current_move_axis_updated(self);

  return self->current_move_axis;
}

axis_t machine_interface_get_current_move_axis(machine_interface_t *self) {
  return self->current_move_axis;
}

void machine_interface_set_current_move_axis(machine_interface_t *self,
                                             axis_t axis) {
  LOGI(TAG, ">> SET_CURR_AX: %d => %d [%c, %c, %c].\n", self->current_move_axis,
       axis, axes[0], axes[1], axes[2]);
  if (self->current_move_axis != axis) {
    self->current_move_axis = axis;
    machine_interface_current_move_axis_updated(self);
  }
}

void machine_interface_task_loop_iter(machine_interface_t *self) {
  // Moved to main machine interface loop, but ensure this is called before
  // _update_machine_state().
  // machine_interface_process_gcode_q(self);

  self->_update_machine_state(self, self->poll_state);

  call_callbacks(state_change_cb);

  self->polli += 1;
  self->poll_state = machine_interface_next_poll_state(self);
}

#ifdef MACHINE_INTERFACE_CALC_TICKS
#include "Arduino.h"
static unsigned long last_tick = 0;
#endif

void machine_interface_setup_loop(machine_interface_t *self) {
  LOGI(TAG, "Machine event loop starting...");
  size_t i = 0;
  while (1) {
    machine_interface_process_gcode_q(self);
    if (i++ % MACHINE_POLL_EVERY_NTH_INTERVAL == 0) {
      machine_interface_task_loop_iter(self);
    }
    if (i % 10 == 0) {
#ifdef MACHINE_INTERFACE_CALC_TICKS
      unsigned long now = millis();
      LOGI(TAG, "Machine task stack size high: %d (%d/%d ms)\n",
           uxTaskGetStackHighWaterMark(NULL), now - last_tick, self->procrate_ms);
      last_tick = now;
#else
      LOGI(TAG, "Machine task stack size high: %d\n",
           uxTaskGetStackHighWaterMark(NULL));
#endif
    }

    //vTaskDelay(pdMS_TO_TICKS(self->procrate_ms));
    vTaskDelay(self->procrate_ms / portTICK_PERIOD_MS);
  }
}

void machine_interface_maybe_execute_continuous_move(
    machine_interface_t *self) {
  // C implementation of maybe_execute_continuous_move
}

void machine_interface_position_updated(machine_interface_t *self) {
  machine_interface_maybe_execute_continuous_move(self);
  LOGI(TAG, "Pos updated: callbacks %p, %p, %p", self->pos_changed_cb[0],
       self->pos_changed_cb[1], self->pos_changed_cb[2]);
  call_callbacks(pos_changed_cb);
}

void machine_interface_home_updated(machine_interface_t *self) {
  call_callbacks(home_changed_cb);
}

void machine_interface_state_updated(machine_interface_t *self) {
  call_callbacks(state_change_cb);
}

void machine_interface_wcs_updated(machine_interface_t *self) {
  // Clear target positions
  for (int i = 0; i < 3; i++) {
    self->target_position[i] = 0.0f;  // Or any appropriate default value
    self->moving_target_position[i] = 0.0f;
  }

  call_callbacks(wcs_changed_cb);
}

void machine_interface_feed_updated(machine_interface_t *self) {
  call_callbacks(feed_changed_cb);
}

void machine_interface_sensors_updated(machine_interface_t *self) {
  call_callbacks(sensors_changed_cb);
}

void machine_interface_dialogs_updated(machine_interface_t *self) {
  call_callbacks(dialogs_changed_cb);
}

void machine_interface_spindles_tools_updated(machine_interface_t *self) {
  call_callbacks(spindles_tools_changed_cb);
}

void machine_interface_files_updated(machine_interface_t *self,
                                     const char *fdir) {
  // Find the filelist entry that matches fdir to pass the correct files pointer.
  char **files = NULL;
  for (int j = 0; j < MAX_FILE_LISTS; ++j) {
    if (self->filelists[j].fdir && strcmp(fdir, self->filelists[j].fdir) == 0) {
      files = self->filelists[j].files;
      break;
    }
  }
  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (!self->files_changed_cb[i].cb_fn) continue;
    // NULL path = wildcard: fire for any fdir (used by multi_machine aggregator).
    // Non-NULL path = fire only when fdir matches exactly.
    if (self->files_changed_cb[i].path == NULL ||
        strcmp(fdir, self->files_changed_cb[i].path) == 0) {
      self->files_changed_cb[i].cb_fn(self, self->files_changed_cb[i].user_data,
                                      fdir, files);
    }
  }
}

void machine_interface_connected_updated(machine_interface_t *self) {
  call_callbacks(connected_changed_cb);
}

void machine_interface_current_move_axis_updated(machine_interface_t *self) {
  call_callbacks(current_move_axis_changed_cb);
}

bool machine_interface_log_message_updated(machine_interface_t *self,
                                           const char *message) {
  bool handled = false;
  self->last_log_message_handled = false;
  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (self->log_message_cb[i].cb_fn) {
      bool cb_handled = false;
      // Call the callback and honor its return value indicating it handled
      // the message. Protect against callbacks that still use the old
      // signature by assuming false if the function pointer is NULL.
      cb_handled = self->log_message_cb[i].cb_fn(self, self->log_message_cb[i].user_data, message);
      handled = handled || cb_handled;
      self->last_log_message_handled = handled; // expose to later callbacks
    }
  }
  return handled;
}

void machine_interface_update_position(machine_interface_t *self, float *values,
                                       float *values_wcs) {
  memcpy(self->position, values, sizeof(self->position));
  memcpy(self->wcs_position, values_wcs, sizeof(self->wcs_position));
  machine_interface_position_updated(self);
}

bool machine_interface_is_continuous_move(machine_interface_t *self) {
  return !self->move_step;
}

bool machine_interface_add_files_changed_cb(machine_interface_t *self,
                                            const char *path, void *user_data,
                                            files_changed_callback_cb_t cb) {
  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (!self->files_changed_cb[i].cb_fn) {
      self->files_changed_cb[i].path = path;  // NULL = wildcard (match any path)
      self->files_changed_cb[i].cb_fn = cb;
      self->files_changed_cb[i].user_data = user_data;
      return true;
    }
  }
  assert(0 && "Maximum number of callbacks reached");
  return false;
}

bool machine_interface_add_log_message_cb(machine_interface_t *self,
                                          void *user_data,
                                          log_message_cb_t cb) {
  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (!self->log_message_cb[i].cb_fn) {
      self->log_message_cb[i].cb_fn = cb;
      self->log_message_cb[i].user_data = user_data;
      return true;
    }
  }
  assert(0 && "Maximum number of callbacks reached");
  return false;
}

void machine_interface_modal_ok(machine_interface_t *self, int modal_id) {
  if (self->modal_ok) {
    self->modal_ok(self, modal_id);
  }
}

void machine_interface_modal_cancel(machine_interface_t *self, int modal_id) {
  if (self->modal_cancel) {
    self->modal_cancel(self, modal_id);
  }
}

void machine_interface_modal_choice(machine_interface_t *self, int choice,
                                    int modal_id) {
  if (self->modal_choice) {
    self->modal_choice(self, choice, modal_id);
  }
}

void machine_interface_modal_int(machine_interface_t *self, int val,
                                 int modal_id) {
  if (self->modal_int) {
    self->modal_int(self, val, modal_id);
  }
}

void machine_interface_modal_float(machine_interface_t *self, float val,
                                   int modal_id) {
  if (self->modal_float) {
    self->modal_float(self, val, modal_id);
  }
}

void machine_interface_modal_str(machine_interface_t *self, const char *val,
                                 int modal_id) {
  if (self->modal_str) {
    self->modal_str(self, val, modal_id);
  }
}

void machine_interface_probe(machine_interface_t *self,
                             const char *probe_gcode) {
  if (self->probe) {
    self->probe(self, probe_gcode);
  }
}

void machine_interface_process_machine_state_response(machine_interface_t *self,
                                                      void *data, size_t len) {
  self->process_machine_state_response(self, data, len);
}

bool machine_interface_should_poll(machine_interface_t *self) {
  if (!self) return false;
  if (self->should_poll) return self->should_poll(self);
  return true;
}

void free_message_box_t(message_box_t *msg_box) {
  if (!msg_box) {
    return;
  }

  free(msg_box->title);
  free(msg_box->text);

  if (msg_box->choices) {
    for (size_t i = 0; i < msg_box->num_choices; ++i) {
      free(msg_box->choices[i]);
    }
    free(msg_box->choices);
  }

  free(msg_box);
}

add_callback_fn(machine_interface, state_change)
    add_callback_fn(machine_interface, pos_changed)
        add_callback_fn(machine_interface, home_changed)
            add_callback_fn(machine_interface, wcs_changed)
                add_callback_fn(machine_interface, feed_changed)
                    add_callback_fn(machine_interface, sensors_changed)
                        add_callback_fn(machine_interface, dialogs_changed)
                            add_callback_fn(machine_interface,
                                            spindles_tools_changed)
                                add_callback_fn(machine_interface,
                                                connected_changed)
                                    add_callback_fn(machine_interface,
                                                    current_move_axis_changed)
