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
  IO_SENSORS = (1 << 11),   ///< Fans, heaters, temperature sensors, GPIO in/out, endstops
} poll_state_t;

typedef enum {
  MACHINE_STATUS_INITIALIZING = 0,
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
  MACHINE_STATUS_WAITING_FOR_MACHINE = 101,  // Hub is alive but has no connection to CNC controller
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

// ---------------------------------------------------------------------------
// Chipload / material enum
// ---------------------------------------------------------------------------

/// Work material for chipload table lookup.
typedef enum {
  TOOL_MATERIAL_ALUMINIUM      = 0,
  TOOL_MATERIAL_STEEL_MILD     = 1,
  TOOL_MATERIAL_STEEL_STAINLESS= 2,
  TOOL_MATERIAL_HARD_PLASTIC   = 3,
  TOOL_MATERIAL_ACRYLIC        = 4,
  TOOL_MATERIAL_MDF            = 5,
  TOOL_MATERIAL_SOFTWOOD       = 6,  ///< Softwood / Plywood
  TOOL_MATERIAL_HARDWOOD       = 7,
  TOOL_MATERIAL_COUNT,
} tool_material_t;

/**
 * @brief Return human-readable name for a material enum value.
 *
 * Useful for UI dropdowns and logging.
 */
const char *tool_material_name(tool_material_t m);

/// Sentinel returned by chipload functions when the spindle is not spinning.
#define CHIPLOAD_SPINDLE_STOPPED (-1.0f)

// ---------------------------------------------------------------------------
// Generalized I/O Channel Model
//
// Every CNC controller — RRF, GRBL, FlexiHAL, Masso, Mach4 — can be mapped
// to one of four (direction × signal) combinations:
//
//   MC_IO_DIR_INPUT  + MC_IO_SIG_DIGITAL  → limit switches, probes, e-stop,
//                                           door, gpin, cycle-start …
//   MC_IO_DIR_INPUT  + MC_IO_SIG_ANALOG   → temperature sensors, 0-10 V
//                                           position/speed feedback …
//   MC_IO_DIR_OUTPUT + MC_IO_SIG_DIGITAL  → coolant relay, ATC solenoid,
//                                           status LED, gpout …
//   MC_IO_DIR_OUTPUT + MC_IO_SIG_ANALOG   → fan PWM, heater setpoint, spindle
//                                           speed (VFD 0-10 V), laser power …
//
// The 'role' field refines the classification for icons / grouping.
// RRF-specific intermediate state lives in machine_rrf.h / machine_rrf.c.
// ---------------------------------------------------------------------------

///< Signal direction
typedef enum {
  MC_IO_DIR_INPUT  = 0,  ///< Read-only input (sensor, switch, encoder)
  MC_IO_DIR_OUTPUT = 1,  ///< Writable output (relay, PWM, analog command)
} mc_io_direction_t;

///< Signal value type
typedef enum {
  MC_IO_SIG_DIGITAL = 0,  ///< Binary on/off; 'value' is 0.0 or 1.0
  MC_IO_SIG_ANALOG  = 1,  ///< Scalar float with physical range and unit
} mc_io_signal_t;

///< Functional role — what does this channel do?
///< Controllers map their native types here; UI uses it for icons / grouping.
typedef enum {
  MC_IO_ROLE_GENERIC      =  0,  ///< Unassigned / user GPIO
  MC_IO_ROLE_LIMIT        =  1,  ///< Endstop / limit switch
  MC_IO_ROLE_PROBE        =  2,  ///< Work or tool probe, touch plate
  MC_IO_ROLE_ESTOP        =  3,  ///< Emergency stop
  MC_IO_ROLE_FEED_HOLD    =  4,  ///< Feed hold / pause
  MC_IO_ROLE_CYCLE_START  =  5,  ///< Cycle start / resume
  MC_IO_ROLE_DOOR         =  6,  ///< Machine guard / safety door
  MC_IO_ROLE_COOLANT      =  7,  ///< Coolant (flood, mist, air blast)
  MC_IO_ROLE_FAN          =  8,  ///< Cooling or exhaust fan (PWM)
  MC_IO_ROLE_HEATER       =  9,  ///< Thermal heater with PID control
  MC_IO_ROLE_TEMP_SENSOR  = 10,  ///< Temperature measurement (read-only)
  MC_IO_ROLE_SPINDLE      = 11,  ///< Spindle speed or enable
  MC_IO_ROLE_STATUS_LED   = 12,  ///< Status indicator light
  MC_IO_ROLE_TOOL_CHANGER = 13,  ///< ATC / tool changer signal
} mc_io_role_t;

///< Channel health / fault indicator
typedef enum {
  MC_IO_HEALTH_OK      = 0,
  MC_IO_HEALTH_FAULT   = 1,
  MC_IO_HEALTH_UNKNOWN = 2,
} mc_io_health_t;

///< Universal I/O channel — controller-independent representation.
///
/// RRF mapping (populated by machine_rrf.c → _rrf_rebuild_io_channels):
///   fans[]           → ANALOG  OUTPUT  FAN         (setpoint=%,  value=actual%)
///   heat.heaters[]   → ANALOG  OUTPUT  HEATER      (setpoint=°C, value=actual°C)
///   heat.sensors[]   → ANALOG  INPUT   TEMP_SENSOR (value=°C)
///   sensors.gpIn[]   → DIGITAL INPUT   GENERIC
///   endstops[]       → DIGITAL INPUT   LIMIT
///   sensors.probes[] → DIGITAL INPUT   PROBE
///
/// GRBL mapping: limit pins→DIGITAL INPUT LIMIT, probe→DIGITAL INPUT PROBE,
///   spindle PWM→ANALOG OUTPUT SPINDLE, M8/M9→DIGITAL OUTPUT COOLANT.
///
/// Masso mapping: digital inputs (24 pins)→DIGITAL INPUT/role,
///   digital outputs→DIGITAL OUTPUT/role, analog I/O→ANALOG INPUT|OUTPUT.
typedef struct {
  char             *name;          ///< Human-readable label (always non-NULL)
  mc_io_direction_t direction;
  mc_io_signal_t    signal;
  mc_io_role_t      role;
  mc_io_health_t    health;

  // --- Current reading ---
  float  value;       ///< DIGITAL: 0.0/1.0 │ ANALOG INPUT: physical read
                      ///< ANALOG OUTPUT: actual feedback (else == setpoint)
  bool   active;      ///< true when input is triggered or output is on

  // --- ANALOG range metadata ---
  float       min_value;   ///< Minimum physical value
  float       max_value;   ///< Maximum physical value
  const char *unit;        ///< "%", "°C", "RPM", "V" … static string, never freed

  // --- OUTPUT: commanded value ---
  float  setpoint;         ///< Requested analog output value (OUTPUT only)
  bool   setpoint_bool;    ///< Requested state (DIGITAL OUTPUT only)

  // --- Implementation hint ---
  uint8_t source_index;    ///< Index in the controller's native array;
                           ///<   used to generate the correct G-code command
} mc_io_channel_t;

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

// ---------------------------------------------------------------------------
// Dirty flags — set atomically by machine_interface_*_updated() functions.
// Read and cleared by interface_tick() (LVGL task) via machine_interface_take_dirty().
// Using dirty flags ensures LVGL mutations only happen in the LVGL task;
// the *_updated() callbacks can be called from any task.
// ---------------------------------------------------------------------------
#define MI_DIRTY_POSITION        (1u << 0)  ///< position[] / wcs_position[] changed
#define MI_DIRTY_STATE           (1u << 1)  ///< machine_status changed
#define MI_DIRTY_HOME            (1u << 2)  ///< axes_homed[] changed
#define MI_DIRTY_WCS             (1u << 3)  ///< wcs / target_position changed
#define MI_DIRTY_FEED            (1u << 4)  ///< feed / feed_multiplier changed
#define MI_DIRTY_SPINDLES_TOOLS  (1u << 5)  ///< spindles[] / tool changed
#define MI_DIRTY_SENSORS         (1u << 6)  ///< io_channels[] / probes changed
#define MI_DIRTY_DIALOGS         (1u << 7)  ///< message_box changed
#define MI_DIRTY_FILES           (1u << 8)  ///< filelists[] changed
#define MI_DIRTY_CONNECTED       (1u << 9)  ///< connection state changed

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
  float tool_diameter_mm;  ///< Parsed from tool name; 0 if unknown
  int   tool_flute_count;  ///< Parsed from tool name; 0 if unknown
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

  // --- Generalized I/O channels (controller-independent) ---
  // Populated by the active driver (e.g. _rrf_rebuild_io_channels in machine_rrf.c)
  // whenever IO_SENSORS data arrives. Read by the UI via lv_cnc_io_panel.
  mc_io_channel_t *io_channels;       ///< Flat array of all I/O channels
  size_t           num_io_channels;

  message_box_t *message_box;

  // --- G-code line buffer for visualizer ---
  // Ring buffer of recent G-code lines from the movement queue.
  // Written by the machine driver, read by the G-code viewer widget.
#define GCODE_LINE_BUF_LINES    64
#define GCODE_LINE_BUF_LINE_LEN 96
  char    gcode_line_buf[GCODE_LINE_BUF_LINES][GCODE_LINE_BUF_LINE_LEN];
  uint16_t gcode_line_head;           ///< Next write index
  uint16_t gcode_line_count;          ///< Number of valid lines

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
  machine_change_callback_t gcode_buffer_changed_cb[MAX_CALLBACKS];
  files_changed_callback_t files_changed_cb[MAX_CALLBACKS];
  log_message_callback_t log_message_cb[MAX_CALLBACKS];

  // --- Internal State ---
  int polli;
  unsigned long last_continuous_tick;
  bool last_log_message_handled;
  // When true, gcodes enqueued via machine_interface_send_gcode are placed at
  // the front of the queue (xQueueSendToFront) so they pre-empt pending poll
  // commands.  Set this flag before dispatching user-initiated commands and
  // clear it immediately after.  Not re-entrant / not atomically safe, but
  // the single-bit benign race (one poll going to front once) is acceptable.
  bool gcode_queue_priority;

  // Dirty flags for deferred LVGL updates.  Set atomically by
  // machine_interface_*_updated(); read+cleared by interface_tick() so that
  // lv_obj_* calls only happen on the LVGL task.  See MI_DIRTY_* constants.
  volatile uint32_t dirty_flags;

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
  /// Set the value / state of an I/O output channel.
  /// @param ch_idx       Index into self->io_channels[]
  /// @param setpoint     Analog target value (0–100 or physical unit)
  /// @param setpoint_bool Digital on/off state (for digital outputs)
  void (*set_io_channel)(machine_interface_t *self, uint8_t ch_idx,
                         float setpoint, bool setpoint_bool);
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
void machine_interface_gcode_buffer_updated(machine_interface_t *self);
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
void machine_interface_set_io_channel(machine_interface_t *self, uint8_t ch_idx,
                                      float setpoint, bool setpoint_bool);
void machine_interface_process_machine_state_response(machine_interface_t *self,
                                                      void *data, size_t len);

bool machine_interface_should_poll(machine_interface_t *self);

/**
 * @brief Atomically read and clear the specified dirty flag bits.
 *
 * Returns the subset of @p mask bits that were dirty (set) before clearing.
 * Safe to call from any task; intended for use in interface_tick() running
 * on the LVGL task to apply pending state changes to the UI safely.
 *
 * Example:
 *   uint32_t dirty = machine_interface_take_dirty(mach, MI_DIRTY_POSITION | MI_DIRTY_STATE);
 *   if (dirty & MI_DIRTY_POSITION) update_position_widgets();
 */
uint32_t machine_interface_take_dirty(machine_interface_t *self, uint32_t mask);

// ---------------------------------------------------------------------------
// Chipload calculation
// ---------------------------------------------------------------------------
// Returns CHIPLOAD_SPINDLE_STOPPED when the spindle RPM is 0 or tool info
// (diameter, flute count) is unknown (0).  All other inputs are taken from
// self->feed (mm/min), self->spindles[0].rpm, self->tool_flute_count and the
// material chipload tables.

/// Compute current chipload in mm/tooth (or CHIPLOAD_SPINDLE_STOPPED).
float machine_interface_compute_chipload(const machine_interface_t *self);

/// Compute chipload relative to the optimal for the current tool diameter and
/// material.  Returns a value in [0, 150] where 100 = optimal midpoint.
/// Returns CHIPLOAD_SPINDLE_STOPPED when spindle is stopped.
/// @param material  Work material for table lookup.
float machine_interface_compute_chipload_relative(const machine_interface_t *self,
                                                  tool_material_t material);

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
add_callback_proto(machine_interface, gcode_buffer_changed);

// --- G-code line buffer helpers ---
// Push a G-code line into the ring buffer and fire gcode_buffer_changed_cb.
void machine_interface_push_gcode_line(machine_interface_t *self,
                                       const char *line);
// Get line at logical index i (0 = oldest).  Returns NULL if out of range.
const char *machine_interface_get_gcode_line(const machine_interface_t *self,
                                              uint16_t i);
// Clear the gcode line buffer.
void machine_interface_clear_gcode_lines(machine_interface_t *self);

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
