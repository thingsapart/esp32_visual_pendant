#ifndef MACHINE_INTERFACE_H
#define MACHINE_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // For size_t
#include "lvgl.h"    // Assuming you have LVGL configured in your PlatformIO project

#ifdef __cplusplus
extern "C" {
#endif

// --- Constants and Enumerations ---

typedef enum {
    MACHINE_POSITION     = (1 << 0),
    MACHINE_POSITION_EXT = (1 << 1),
    SPINDLE              = (1 << 2),
    PROBES               = (1 << 3),
    TOOLS                = (1 << 4),
    MESSAGES_AND_DIALOGS = (1 << 5),
    END_STOPS            = (1 << 6),
    NETWORK              = (1 << 7),
    JOB_STATUS           = (1 << 8),
    LIST_FILES           = (1 << 9),
    LIST_MACROS          = (1 << 10),
    // Add more states as needed, up to 32 total (using all bits of uint32_t)
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
    MACHINE_STATUS_RUNNING,
    MACHINE_STATUS_TOOL_CHANGING,
    MACHINE_STATUS_BUSY,
    MACHINE_STATUS_UNKNOWN = 100,
} machine_status_t;

// --- Data Structures ---

// Forward declarations to avoid circular dependencies
typedef struct machine_interface_t machine_interface_t;

typedef struct {
    const char* id;
    float value;
} probe_t;

typedef struct {
    const char* name;
    bool triggered;
} end_stop_t;

typedef struct {
    const char* name;
    int rpm;
} spindle_t;

// --- Callback Function Types ---

typedef void (*machine_callback_t)(machine_interface_t *, void *);

typedef struct state_change_callback_t { void *user_data; machine_callback_t cb_fn; } state_change_callback_t;
typedef struct pos_changed_callback_t { void *user_data; machine_callback_t cb_fn; } pos_changed_callback_t;
typedef struct home_changed_callback_t { void *user_data; machine_callback_t cb_fn; } home_changed_callback_t;
typedef struct wcs_changed_callback_t { void *user_data; machine_callback_t cb_fn; } wcs_changed_callback_t;
typedef struct feed_changed_callback_t { void *user_data; machine_callback_t cb_fn; } feed_changed_callback_t;
typedef struct sensors_changed_callback_t { void *user_data; machine_callback_t cb_fn; } sensors_changed_callback_t;
typedef struct dialogs_changed_callback_t { void *user_data; machine_callback_t cb_fn; } dialogs_changed_callback_t;
typedef struct spindles_tools_changed_callback_t { void *user_data; machine_callback_t cb_fn; } spindles_tools_changed_callback_t;
typedef struct connected_callback_t { void *user_data; machine_callback_t cb_fn; } connected_callback_t;
typedef struct files_changed_callback_t {
    const char *path;
    void *user_data;
    void (*cb_fn)(machine_interface_t *mach, void *self, const char *path, const char **files);
} files_changed_callback_t;

// --- G-code Queue ---

#define MAX_GCODE_Q_LEN 10

typedef struct {
    char buffer[MAX_GCODE_Q_LEN][64]; // Fixed-size buffer for G-code commands.  Adjust size as needed.
    size_t head;
    size_t tail;
    size_t count;
} gcode_queue_t;

// --- Machine Interface Structure (Virtual Class) ---

typedef struct machine_interface_t {
    // --- Configuration ---
    uint16_t sleep_ms;
    uint32_t poll_state;

    // --- Machine State ---
    machine_status_t machine_status;
    bool axes_homed[3];
    float position[3];
    float wcs_position[3];
    float target_position[3];
    float moving_target_position[3];
    int wcs;
    const char* tool;  // Pointer to a string literal or dynamically allocated string
    float z_offs;
    float feed;
    float feed_req;
    float feed_multiplier;
    bool move_relative;
    bool move_step; // Use bool instead of None/value

    #define MAX_FILE_LISTS 2  // Currently just G-Codes and Macros, extend if needed.
    struct {
        const char *fdir;
        const char **files;
    } filelists[MAX_FILE_LISTS];

    probe_t* probes;
    size_t num_probes;
    end_stop_t* end_stops;
    size_t num_end_stops;
    spindle_t* spindles;
    size_t num_spindles;
    //dialogs_t* dialogs; // Example, adjust as needed

    // --- G-code Queue ---
    gcode_queue_t gcode_queue;

    // --- Callbacks ---
#define MAX_CALLBACKS 5

    state_change_callback_t state_change_cb[MAX_CALLBACKS];
    pos_changed_callback_t pos_changed_cb[MAX_CALLBACKS];
    home_changed_callback_t home_changed_cb[MAX_CALLBACKS];
    wcs_changed_callback_t wcs_changed_cb[MAX_CALLBACKS];
    feed_changed_callback_t feed_changed_cb[MAX_CALLBACKS];
    sensors_changed_callback_t sensors_changed_cb[MAX_CALLBACKS];
    dialogs_changed_callback_t dialogs_changed_cb[MAX_CALLBACKS];
    spindles_tools_changed_callback_t spindles_tools_changed_cb[MAX_CALLBACKS];
    connected_callback_t connected_changed_cb[MAX_CALLBACKS];
    files_changed_callback_t files_changed_cbs[MAX_CALLBACKS];

    // --- Internal State ---
    int polli;
    unsigned long last_continuous_tick;

    // --- "Virtual" Methods (Function Pointers) ---
    void (*send_gcode)(machine_interface_t *self, const char *gcode, uint32_t poll_state);
    void (*_send_gcode)(machine_interface_t *self, const char *gcode);
    void (*_update_machine_state)(machine_interface_t *self, uint32_t poll_state);
    bool (*is_connected)(machine_interface_t *self);
    void (*list_files)(machine_interface_t *self, const char *path);
    void (*run_macro)(machine_interface_t *self, const char *macro_name);
    void (*start_job)(machine_interface_t *self, const char *job_name);
    void (*_move_to)(machine_interface_t *self, const char axis, float feed, float value, bool relative);
    void (*move_continuous)(machine_interface_t *self, const char axis, float feed, int direction);
    void (*move_continuous_stop)(machine_interface_t *self);
    void (*_continuous_move)(machine_interface_t *self, const char axis, float feed, int direction);
    void (*_continuous_stop)(machine_interface_t *self);
    void (*move)(machine_interface_t *self, const char axis, float feed, float value);
    void (*home_all)(machine_interface_t *self);
    void (*home)(machine_interface_t *self, const char *axes);
    void (*set_wcs)(machine_interface_t *self, int wcs);
    void (*set_wcs_zero)(machine_interface_t *self, int wcs, const char *axes);
    void (*next_wcs)(machine_interface_t *self);
    const char* (*debug_print)(machine_interface_t *self);
    // Add other "virtual" methods here
} machine_interface_t;

machine_interface_t* machine_interface_create(uint16_t sleep_ms);
machine_interface_t* machine_interface_init(machine_interface_t *self, uint16_t sleep_ms);

void machine_interface_destroy(machine_interface_t *self);
void machine_interface_deinit(machine_interface_t *self);

void machine_interface_send_gcode(machine_interface_t *self, const char *gcode, uint32_t poll_state);

bool machine_interface_is_homed(machine_interface_t *self, const char *axes);
const char* machine_interface_get_wcs_str(machine_interface_t *self, int wcs_offs);
int machine_interface_axis_idx(machine_interface_t *self, char axis);
void machine_interface_process_gcode_q(machine_interface_t *self);
void machine_interface_task_loop_iter(machine_interface_t *self);
void machine_interface_setup_loop(machine_interface_t *self);
void machine_interface_maybe_execute_continuous_move(machine_interface_t *self);
void machine_interface_position_updated(machine_interface_t *self);
void machine_interface_home_updated(machine_interface_t *self);
void machine_interface_wcs_updated(machine_interface_t *self);
void machine_interface_feed_updated(machine_interface_t *self);
void machine_interface_sensors_updated(machine_interface_t *self);
void machine_interface_dialogs_updated(machine_interface_t *self);
void machine_interface_spindles_tools_updated(machine_interface_t *self);
void machine_interface_files_updated(machine_interface_t *self, const char *fdir);
void machine_interface_connected_updated(machine_interface_t *self);
void machine_interface_update_position(machine_interface_t *self, float *values, float *values_wcs);
bool machine_interface_is_continuous_move(machine_interface_t *self);
uint32_t machine_interface_next_poll_state(machine_interface_t *self);

#define add_callback_proto(type, cbs_name) \
bool type##_add_##cbs_name##_cb(type##_t *self, void *user_data, machine_callback_t cb);

add_callback_proto(machine_interface, state_change)
add_callback_proto(machine_interface, pos_changed)
add_callback_proto(machine_interface, home_changed)
add_callback_proto(machine_interface, wcs_changed)
add_callback_proto(machine_interface, feed_changed)
add_callback_proto(machine_interface, sensors_changed)
add_callback_proto(machine_interface, dialogs_changed)
add_callback_proto(machine_interface, spindles_tools_changed)
add_callback_proto(machine_interface, connected_changed)

// G-code queue functions
void gcode_queue_init(gcode_queue_t *queue);
bool gcode_queue_push(gcode_queue_t *queue, const char *gcode);
bool gcode_queue_pop(gcode_queue_t *queue, char *gcode); // Retrieves and removes
bool gcode_queue_peek(const gcode_queue_t *queue, char *gcode); // Retrieves without removing
size_t gcode_queue_count(const gcode_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif // MACHINE_INTERFACE_H   