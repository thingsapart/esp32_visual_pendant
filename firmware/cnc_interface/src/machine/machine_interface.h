#ifndef MACHINE_INTERFACE_H
#define MACHINE_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>  // For size_t
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"

#ifdef ASYNC_GCODE_SENDING
#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#else
#include "compat/queue.h"
#endif
#endif

// --- Constants and Enumerations ---

typedef enum {
  MACHINE_POSITION = (1 << 0),
  MACHINE_POSITION_EXT = (1 << 1),
  SPINDLE = (1 << 2),
  PROBES = (1 << 3),
  TOOLS = (1 << 4),
  MESSAGES_AND_DIALOGS = (1 << 5),
  END_STOPS = (1 << 6),
  NETWORK = (1 << 7),
  JOB_STATUS = (1 << 8),
  LIST_FILES = (1 << 9),
  LIST_MACROS = (1 << 10),
} poll_state_t;

typedef enum {
  MACHINE_STATUS_WAITING_FOR_MACHINE = 0,  // Hub is alive but has no connection to CNC controller
  MACHINE_STATUS_INITIALIZING,
  MACHINE_STATUS_FLASHING_FIRMWARE,
  MACHINE_STATUS_EMERGENCY_HALTED,
  MACHINE_STATUS_OFF,
  MACHINE_STATUS_PAUSED_DEC,
  MACHINE_STATUS_PAUSED_RESUME,
  MACHINE_STATUS_PAUSED,
  MACHINE_STATUS_SIMULATING,
  MACHINE_STATUS_IDLE,
  MACHINE_STATUS_TOOL_CHANGING,
  MACHINE_STATUS_RUNNING,
  MACHINE_STATUS_UNKNOWN = 100,
} machine_status_t;

// --- Data Structures ---

// Forward declarations to avoid circular dependencies
typedef struct machine_interface_t machine_interface_t;

typedef enum {
  AXIS_X,
  AXIS_Y,
  AXIS_Z,
  AXIS_OFF  // AXIS_OFF has to be last.
} axis_t;

typedef struct {
  const char *id;
  float value;
} probe_t;

typedef struct {
  const char *name;
  bool triggered;
} end_stop_t;

typedef struct {
  const char *name;
  int rpm;
  int min_rpm;
  int max_rpm;
} spindle_t;

typedef enum {
  MESSAGE_INFO = 0,         // Non-blocking info.
  MESSAGE = 1,              // Non-blocking message with close button.
  MESSAGE_OK = 2,           // Blocking, "OK".
  MESSAGE_OK_CANCEL = 3,    // Blocking, "OK", "CANCEL".
  MESSAGE_CHOICE = 4,       // Blocking, multiple buttons.
  MESSAGE_INPUT_INT = 5,    // Blocking, integer input.
  MESSAGE_INPUT_FLOAT = 6,  // Blocking, float input.
  MESSAGE_INPUT_STR = 7,    // Blocking, string input.
} message_box_mode_t;

typedef struct {
  char *title;
  char *text;
  char **choices;
  size_t num_choices;
  message_box_mode_t mode;
  int seq;
  void *user_data;
  machine_interface_t *machine;
} message_box_t;

// --- Callback Function Types ---

typedef void (*machine_callback_t)(machine_interface_t *, void *);

typedef struct machine_change_callback_t {
  void *user_data;
  machine_callback_t cb_fn;
} machine_change_callback_t;

typedef void (*files_changed_callback_cb_t)(machine_interface_t *mach,
                                            void *self, const char *path,
                                            char **files);
typedef struct files_changed_callback_t {
  const char *path;
  void *user_data;
  files_changed_callback_cb_t cb_fn;
} files_changed_callback_t;

typedef bool (*log_message_cb_t)(machine_interface_t *mach, void *user_data,
                                 const char *message);
typedef struct log_message_callback_t {
  void *user_data;
  log_message_cb_t cb_fn;
} log_message_callback_t;

// --- Machine Interface Structure (Virtual Class) ---

typedef struct machine_interface_t {
  // --- Configuration ---
  uint16_t procrate_ms;
  uint32_t poll_state;

  // --- Machine State ---
  machine_status_t machine_status;
  bool axes_homed[3];
  float axis_min[3];   ///< Per-axis minimum travel limit (mm), from move.axes[].min
  float axis_max[3];   ///< Per-axis maximum travel limit (mm), from move.axes[].max
  float position[3];
  float wcs_position[3];
  float target_position[3];
  float moving_target_position[3];
  axis_t current_move_axis;
  float current_move_step_xy;
  float current_move_step_z;
  int wcs;
  const char *tool;
  float z_offs;
  float feed;
  float feed_req;
  float feed_multiplier;
  bool move_relative;
  bool move_step;

// Currently just G-Codes and Macros, extend if needed.
#define MAX_FILE_LISTS 2
  struct {
    char *fdir;
    char **files;
  } filelists[MAX_FILE_LISTS];

  probe_t *probes;
  size_t num_probes;
  end_stop_t *end_stops;
  size_t num_end_stops;
  spindle_t *spindles;
  size_t num_spindles;
  message_box_t *message_box;

#ifdef ASYNC_GCODE_SENDING
#ifdef ESP32_HW
  QueueHandle_t gcode_queue;
#else
  gcode_queue_t *gcode_queue;
#endif
#endif

  // --- Callbacks ---
#define MAX_CALLBACKS 5

  machine_change_callback_t state_change_cb[MAX_CALLBACKS];
  machine_change_callback_t pos_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t home_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t wcs_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t feed_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t sensors_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t dialogs_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t spindles_tools_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t connected_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t current_move_axis_changed_cb[MAX_CALLBACKS];
  files_changed_callback_t files_changed_cb[MAX_CALLBACKS];
  log_message_callback_t log_message_cb[MAX_CALLBACKS];

  // --- Internal State ---
  int polli;
  unsigned long last_continuous_tick;
  bool last_log_message_handled;

  // --- "Virtual" Methods (Function Pointers) ---
  void (*send_gcode)(machine_interface_t *self, const char *gcode,
                     uint32_t poll_state);
  void (*_send_gcode)(machine_interface_t *self, const char *gcode);
  void (*_update_machine_state)(
      machine_interface_t *self,
      uint32_t poll_state);  // Periodically called to poll/update the machine's
                             // internal state model from real machine.
  void (*process_machine_state_response)(
      machine_interface_t *self, void *data,
      size_t len);  // Called when new machine state data is available.
  bool (*should_poll)(machine_interface_t *self);
  bool (*is_connected)(machine_interface_t *self);
  void (*attempt_connect)(machine_interface_t *self);
  void (*list_files)(machine_interface_t *self, const char *path);
  void (*run_macro)(machine_interface_t *self, const char *macro_name);
  void (*start_job)(machine_interface_t *self, const char *job_name);
  void (*_move_to)(machine_interface_t *self, const char axis, float feed,
                   float value, bool relative);
  void (*move_continuous)(machine_interface_t *self, const char axis,
                          float feed, int direction);
  void (*move_continuous_stop)(machine_interface_t *self);
  void (*_continuous_move)(machine_interface_t *self, const char axis,
                           float feed, int direction);
  void (*_continuous_stop)(machine_interface_t *self);
  void (*move)(machine_interface_t *self, const char axis, float feed,
               float value);
  void (*home_all)(machine_interface_t *self);
  void (*home)(machine_interface_t *self, const char *axes);
  void (*set_wcs)(machine_interface_t *self, int wcs);
  void (*set_wcs_zero)(machine_interface_t *self, int wcs, const char *axes);
  void (*next_wcs)(machine_interface_t *self);
  char *(*debug_print)(machine_interface_t *self);
  void (*modal_cancel)(machine_interface_t *self, int modal_id);
  void (*modal_ok)(machine_interface_t *self, int modal_id);
  void (*modal_choice)(machine_interface_t *self, int choice, int modal_id);
  void (*modal_int)(machine_interface_t *self, int val, int modal_id);
  void (*modal_float)(machine_interface_t *self, float val, int modal_id);
  void (*modal_str)(machine_interface_t *self, const char *val, int modal_id);
  void (*probe)(machine_interface_t *self, const char *probe_gcode);
  void (*set_connected)(machine_interface_t *self, bool connected);
} machine_interface_t;

machine_interface_t *machine_interface_create(uint16_t procrate_ms);
machine_interface_t *machine_interface_init(machine_interface_t *self,
                                            uint16_t procrate_ms);

void machine_interface_destroy(machine_interface_t *self);
void machine_interface_deinit(machine_interface_t *self);

void machine_interface_send_gcode(machine_interface_t *self, const char *gcode,
                                  uint32_t poll_state);

bool machine_interface_is_homed(machine_interface_t *self, const char *axes);
const char *machine_interface_get_wcs_str(machine_interface_t *self,
                                          int wcs_offs);
int machine_interface_axis_idx(machine_interface_t *self, char axis);
void machine_interface_process_gcode_q(machine_interface_t *self);
void machine_interface_task_loop_iter(machine_interface_t *self);
void machine_interface_setup_loop(machine_interface_t *self);
void machine_interface_maybe_execute_continuous_move(machine_interface_t *self);
void machine_interface_position_updated(machine_interface_t *self);
void machine_interface_home_updated(machine_interface_t *self);
void machine_interface_state_updated(machine_interface_t *self);
void machine_interface_wcs_updated(machine_interface_t *self);
void machine_interface_feed_updated(machine_interface_t *self);
void machine_interface_sensors_updated(machine_interface_t *self);
void machine_interface_dialogs_updated(machine_interface_t *self);
void machine_interface_spindles_tools_updated(machine_interface_t *self);
void machine_interface_files_updated(machine_interface_t *self,
                                     const char *fdir);
void machine_interface_connected_updated(machine_interface_t *self);
void machine_interface_current_move_axis_updated(machine_interface_t *self);
bool machine_interface_log_message_updated(machine_interface_t *self,
                                           const char *message);
void machine_interface_update_position(machine_interface_t *self, float *values,
                                       float *values_wcs);
bool machine_interface_is_continuous_move(machine_interface_t *self);
uint32_t machine_interface_next_poll_state(machine_interface_t *self);
void machine_interface_set_current_move_axis(machine_interface_t *self,
                                             axis_t axis);
axis_t machine_interface_get_current_move_axis(machine_interface_t *self);
axis_t machine_interface_next_move_axis(machine_interface_t *self);
axis_t machine_interface_move_current_axis(machine_interface_t *self,
                                           float feed, float value,
                                           bool relative);
axis_t machine_interface_step_current_axis(machine_interface_t *self,
                                           float feed, int steps);
void machine_interface_modal_cancel(machine_interface_t *self, int modal_id);
void machine_interface_modal_ok(machine_interface_t *self, int modal_id);
void machine_interface_modal_choice(machine_interface_t *self, int choice,
                                    int modal_id);
void machine_interface_modal_int(machine_interface_t *self, int val,
                                 int modal_id);
void machine_interface_modal_float(machine_interface_t *self, float val,
                                   int modal_id);
void machine_interface_modal_str(machine_interface_t *self, const char *val,
                                 int modal_id);
void machine_interface_probe(machine_interface_t *self,
                             const char *probe_gcode);
void machine_interface_process_machine_state_response(machine_interface_t *self,
                                                      void *data, size_t len);

bool machine_interface_should_poll(machine_interface_t *self);

bool machine_interface_add_files_changed_cb(machine_interface_t *self,
                                            const char *path, void *user_data,
                                            files_changed_callback_cb_t cb);
bool machine_interface_add_log_message_cb(machine_interface_t *self,
                                          void *user_data,
                                          log_message_cb_t cb);

void free_message_box_t(message_box_t *msg_box);

// <type>_add_<callback_name>_cb(<tye> *self, void *user_data, <callback_t>
// callback);
// => eg machine_interface_add_state_changed_cb(...),
// machine_interface_add_pos_changed_cb(...).
#define add_callback_proto(type, cbs_name)                         \
  bool type##_add_##cbs_name##_cb(type##_t *self, void *user_data, \
                                  machine_callback_t cb)

add_callback_proto(machine_interface, state_change);
add_callback_proto(machine_interface, pos_changed);
add_callback_proto(machine_interface, home_changed);
add_callback_proto(machine_interface, wcs_changed);
add_callback_proto(machine_interface, feed_changed);
add_callback_proto(machine_interface, sensors_changed);
add_callback_proto(machine_interface, dialogs_changed);
add_callback_proto(machine_interface, spindles_tools_changed);
add_callback_proto(machine_interface, connected_changed);
add_callback_proto(machine_interface, current_move_axis_changed);

#define add_callback_fn(type, cbs_name)                                  \
  bool type##_add_##cbs_name##_cb(type##_t *self, void *user_data,       \
                                  machine_callback_t cb) {               \
    for (int i = 0; i < MAX_CALLBACKS; i++) {                            \
      if (!self->cbs_name##_cb[i].cb_fn) {                               \
        LOGI(TAG, #type "_add_" #cbs_name "_cb adding: %d (%p)", i, cb); \
        self->cbs_name##_cb[i].cb_fn = cb;                               \
        self->cbs_name##_cb[i].user_data = user_data;                    \
        return true;                                                     \
      }                                                                  \
    }                                                                    \
    assert(0 && "Maximum number of callbacks reached");                  \
    return false;                                                        \
  }

#ifdef __cplusplus
}
#endif

#endif  // MACHINE_INTERFACE_H
