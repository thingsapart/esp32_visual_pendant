#include "machine_interface.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "config.h"
#include "debug.h"


static const char *TAG = "machine_interface"; // Used for logging
static const char axes[] = {
        'X', 'Y', 'Z', '\0'
    };

#ifdef POSIX
# include <unistd.h>
# define vTaskDelay(ms) usleep(ms * 1000)
# define pdMS_TO_TICKS(ms) (ms)
#else
# include "freertos/FreeRTOS.h"
# include "freertos/task.h"
#endif

#define call_callbacks(cbs_name) do { \
    for (int i = 0; i < MAX_CALLBACKS; i++) { \
        if (self->cbs_name[i].cb_fn) { \
            (*self->cbs_name[i].cb_fn)(self, self->cbs_name[i].user_data); \
        } \
    } \
} while (0)

#define call_callbacks_with_args(cbs_name, ...) do { \
    for (int i = 0; i < MAX_CALLBACKS; i++) { \
        if (self->cbs_name[i].cb_fn) { \
            (*self->cbs_name[i].cb_fn)(self, self->cbs_name[i].user_data, __VA_ARGS__); \
        } \
    } \
} while (0)

#ifndef ASYNC_GCODE_SENDING

// Initialize the G-code queue
void gcode_queue_init(gcode_queue_t *queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

// Add a G-code command to the queue (FIFO).  Handles wraparound.
bool gcode_queue_push(gcode_queue_t *queue, const char *gcode) {
    if (queue->count >= MAX_GCODE_Q_LEN) {
        _d(2, "G-code queue overflow!");
        return false; // Indicate failure
    }

    size_t len = strlen(gcode);
    if (len >= sizeof(queue->buffer[0])) {
        _df(2, "G-code command too long: %s", gcode);
        return false; // Command too long for buffer
    }

    strcpy(queue->buffer[queue->head], gcode);
    queue->head = (queue->head + 1) % MAX_GCODE_Q_LEN;
    queue->count++;
    return true;
}

// Retrieve a G-code command from the queue (FIFO).  Handles wraparound.
bool gcode_queue_pop(gcode_queue_t *queue, char *gcode) {
    if (queue->count == 0) {
        return false; // Queue is empty
    }

    strcpy(gcode, queue->buffer[queue->tail]);
    queue->tail = (queue->tail + 1) % MAX_GCODE_Q_LEN;
    queue->count--;
    return true;
}

// Peek at the next G-code command without removing it.
bool gcode_queue_peek(const gcode_queue_t *queue, char *gcode) {
    if (queue->count == 0) {
        return false; // Queue is empty
    }
    strcpy(gcode, queue->buffer[queue->tail]);
    return true;
}

size_t gcode_queue_count(const gcode_queue_t *queue) {
    return queue->count;
}

static void machinte_interface_send_gcode(machine_interface_t *self, const char *gcode, poll_state_t poll_state) {
    gcode_queue_push(&self->gcode_queue, gcode);
    self->poll_state = (uint32_t) self->poll_state | poll_state;
    _df(-1, "gcode queued: %s", gcode);
}

#endif

// --- Default "Virtual" Method Implementations ---
// These are the default implementations that can be overridden.

static void _default_send_gcode(machine_interface_t *self, const char *gcode) {
    // Default implementation:  Just log the G-code.  Replace with actual sending logic.
    _df(-1, "Sending G-code (default): %s\n", gcode);
    //  _childclass_override(); // This is how you'd call a "virtual" method
}

static void _default_update_machine_state(machine_interface_t *self, uint32_t poll_state) {
    // Default implementation:  Simulate some state changes.
    _df(-1, "Updating machine state (default), poll_state: %d", (int) poll_state);
}

static bool _default_is_connected(machine_interface_t *self) {
    return false; // Default: Not connected
}

static void _default_list_files(machine_interface_t *self, const char *path) {
    _df(0, "Listing files (default) in: %s", path);
}

static void _default_run_macro(machine_interface_t *self, const char *macro_name) {
    _df(0, "Running macro (default): %s", macro_name);
}

static void _default_start_job(machine_interface_t *self, const char *job_name) {
     _df(0, "Starting job (default): %s", job_name);
}

static void _default_move_to(machine_interface_t *self, const char axis, float feed, float value, bool relative)
{
    int axi = machine_interface_axis_idx(self, axis);

    const char* mode = relative ? "G91" : "G90";
    float pos = value;
    if (relative) {
        self->moving_target_position[axi] = self->wcs_position[axi] + value;
    } else {
        self->moving_target_position[axi] = value;
        pos = self->moving_target_position[axi];
    }
    char gcode_cmd[64]; // Buffer for the formatted G-code
    snprintf(gcode_cmd, sizeof(gcode_cmd), "M120\n%s\nG1 %c%.3f F%.3f\nM121", mode, axis, pos, feed);
    self->send_gcode(self, gcode_cmd, MACHINE_POSITION);
}

static void _default_continuous_move(machine_interface_t *self, const char axis, float feed, int direction) {
     _df(0, "Continuous move (default): axis=%c, feed=%f, direction=%d", axis, feed, direction);
}

static void _default_continuous_stop(machine_interface_t *self) {
     _d(0, "Continuous stop (default)");
}

static void _default_move(machine_interface_t *self, const char axis, float feed, float value) {
     _df(0, "Move (default): axis=%c, feed=%f, value=%f", axis, feed, value);
    _default_move_to(self, axis, feed, value, true); // Default to relative move
}

static void _default_home_all(machine_interface_t *self) {
    self->send_gcode(self, "G28", MACHINE_POSITION);
}

static void _default_home(machine_interface_t *self, const char *axes) {
    char gcode_cmd[32]; // Buffer for G-code command
    snprintf(gcode_cmd, sizeof(gcode_cmd), "G28 %s", axes);
    self->send_gcode(self, gcode_cmd, MACHINE_POSITION);
}

static void _default_set_wcs(machine_interface_t *self, int wcs) {
    char gcode_cmd[16];
    snprintf(gcode_cmd, sizeof(gcode_cmd), "%s", machine_interface_get_wcs_str(self, wcs % 9));
    self->send_gcode(self, gcode_cmd, MACHINE_POSITION);
}

static void _default_set_wcs_zero(machine_interface_t *self, int wcs, const char *axes) {
    char gcode_cmd[64];
    char zer[32] = ""; // Buffer for the zeroing part
    for (const char *p = axes; *p; p++) {
        snprintf(zer + strlen(zer), sizeof(zer) - strlen(zer), "%c0 ", *p);
    }
    snprintf(gcode_cmd, sizeof(gcode_cmd), "G10 L20 P%d %s", wcs, zer);
    self->send_gcode(self, gcode_cmd, MACHINE_POSITION);
}

static void _default_next_wcs(machine_interface_t *self) {
    _default_set_wcs(self, self->wcs + 1);
}

static char* _default_debug_print(machine_interface_t *self) {
    // Create a static buffer to hold the debug string.  This is thread-safe
    // because it's static and read-only after initialization.  Adjust size as needed.
    char *debug_str = (char *) malloc(sizeof(char) * 512);

    snprintf(debug_str, sizeof(debug_str),
        "{ status: %d, homed: [%d, %d, %d], pos: [%f, %f, %f], wcs_pos: [%f, %f, %f], wcs: %d, tool: %s, feedm: %f, zoffs: %f, gcode_q_count: %zu, poll_state: %d }",
        self->machine_status,
        self->axes_homed[0], self->axes_homed[1], self->axes_homed[2],
        self->position[0], self->position[1], self->position[2],
        self->wcs_position[0], self->wcs_position[1], self->wcs_position[2],
        self->wcs,
        self->tool ? self->tool : "None", // Handle NULL tool
        self->feed_multiplier,
        self->z_offs,
#ifndef ASYNC_GCODE_SENDING
        gcode_queue_count(&self->gcode_queue),
#else
        0,
#endif
        (int) self->poll_state
    );
    return debug_str;
}

// --- Constructor ---

machine_interface_t* machine_interface_create(uint16_t procrate_ms) {
    machine_interface_t *self = (machine_interface_t *)malloc(sizeof(machine_interface_t));
    return machine_interface_init(self, procrate_ms);
}

machine_interface_t* machine_interface_init(machine_interface_t *self, uint16_t procrate_ms) {
    if (!self) {
         _d(2, "Failed to allocate memory for machine_interface");
        return NULL; // Indicate failure
    }
    memset(self, 0, sizeof(*self));

    // Initialize members
    self->procrate_ms = procrate_ms;
    self->poll_state = (uint32_t) MACHINE_POSITION | SPINDLE | PROBES | TOOLS | MESSAGES_AND_DIALOGS | END_STOPS;
    self->machine_status = MACHINE_STATUS_UNKNOWN;
    memset(self->axes_homed, 0, sizeof(self->axes_homed)); // Initialize all axes to not homed
    memset(self->position, 0, sizeof(self->position)); // Initialize positions to 0
    memset(self->wcs_position, 0, sizeof(self->wcs_position));
    memset(self->target_position, 0, sizeof(self->target_position));
    memset(self->moving_target_position, 0, sizeof(self->moving_target_position));

    self->wcs = 1;
    self->tool = NULL; // Initialize tool to NULL
    self->z_offs = 0.0f;
    self->feed = 0.0f;
    self->feed_req = 0.0f;
    self->feed_multiplier = 1.0f;
    self->move_relative = false; // Default value
    self->move_step = false;      // Default value

    // Initialize probes, end_stops, spindles, etc. to NULL and 0.  You'll need to
    // allocate these dynamically later if you use them.
    self->probes = NULL;
    self->num_probes = 0;
    self->end_stops = NULL;
    self->num_end_stops = 0;
    self->spindles = NULL;
    self->num_spindles = 0;

#ifndef ASYNC_GCODE_SENDING
    gcode_queue_init(&self->gcode_queue);
#else
    self->gcode_queue = NULL;
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
    self->_continuous_move = _default_continuous_move; // Internal version
    self->_continuous_stop = _default_continuous_stop; // Internal version
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
    self->current_move_step = 1.0;

    return self;
}

// --- Destructor ---

void machine_interface_deinit(machine_interface_t *self) {
    if (self) {
        // Free any dynamically allocated resources here (e.g., probes, end_stops, etc.)
        free(self->probes);
        free(self->end_stops);
        free(self->spindles);
        free(self->message_box);

        // Free the tool string if it was dynamically allocated
        if (self->tool) {
            free((void*)self->tool); // Cast away const for freeing
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
                    _df(1,  "Axis not homed: %c", *p);
                    return false;
                }
            } else {
                 _df(1,  "Invalid axis: %c", *p); // Log warning for invalid axis
                return false; // Consider invalid axis as not homed
            }
        }
        return true;
    }
}

const char* machine_interface_get_wcs_str(machine_interface_t *self, int wcs_offs) {
    static char wcs_str[8]; // Static buffer to hold the WCS string
    int wcsi = (wcs_offs == -1) ? self->wcs : wcs_offs; // Use -1 to indicate self->wcs

    if (wcsi >= 1 && wcsi <= 5) {
        snprintf(wcs_str, sizeof(wcs_str), "G%d", 54 + wcsi);
    } else if (wcsi >= 6 && wcsi <= 9) {
        snprintf(wcs_str, sizeof(wcs_str), "G59.%d", wcsi - 5);
    } else {
        strcpy(wcs_str, "G54"); // Default to G54 if out of range
    }
    return wcs_str;
}

int machine_interface_axis_idx(machine_interface_t *self, char axis) {
    switch (axis) {
        case 'X': return 0;
        case 'Y': return 1;
        case 'Z': return 2;
        default:  return -1; // Indicate invalid axis
    }
}

int machine_interface_axis_t_idx(axis_t axis) {
    switch (axis) {
        case AXIS_X: return 0;
        case AXIS_Y: return 1;
        case AXIS_Z: return 2;
        default:  return -1; // Indicate invalid axis
    }
}

char idx_to_axis(int i) {
    return axes[i];
}

#ifndef ASYNC_GCODE_SENDING
void machine_interface_send_gcode(machine_interface_t *self, const char *gcode, uint32_t poll_state) {
    if (!gcode_queue_push(&self->gcode_queue, gcode)) {
         _d(2, "Failed to add gcode to the queue");
    }
    self->poll_state = (uint32_t) self->poll_state | poll_state;
    _df(-1, "gcode queued: %s", gcode);
}

void machine_interface_process_gcode_q(machine_interface_t *self) {
    char gcode[MAX_GCODE_STR_LEN]; // Buffer to hold the popped G-code command
    while (gcode_queue_pop(&self->gcode_queue, gcode)) {
        self->_send_gcode(self, gcode);
        // Add response handling here if needed
    }
}
#else
void machine_interface_send_gcode(machine_interface_t *self, const char *gcode, uint32_t poll_state) {
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

    BaseType_t result = xQueueSend(self->gcode_queue, buf, 0);
    if (result != pdTRUE) {
         LOGW(TAG, "Failed to add gcode to the queue: %s", gcode);
    }

    self->poll_state = (uint32_t) self->poll_state | poll_state;
    LOGV(TAG, "gcode queued: %s", gcode);
}

void machine_interface_process_gcode_q(machine_interface_t *self) {
    // NOP - this is done in task in async mode.
}
#endif

uint32_t machine_interface_next_poll_state(machine_interface_t *self)
{
    uint32_t poll_state = (self->polli % 19 == 0) ? MACHINE_POSITION_EXT : MACHINE_POSITION;
    if (self->polli % 3 == 0)  poll_state |= JOB_STATUS;
    if (self->polli % 5 == 0)  poll_state |= MESSAGES_AND_DIALOGS;
    if (self->polli % 7 == 0)  poll_state |= PROBES;
    if (self->polli % 11 == 0) poll_state |= END_STOPS;
    if (self->polli % 13 == 0) poll_state |= SPINDLE;
    if (self->polli % 17 == 0) poll_state |= TOOLS;
    if (self->polli % 9973 == 0) poll_state |= (LIST_MACROS | LIST_FILES);
    return poll_state;
}

axis_t machine_interface_move_current_axis(machine_interface_t *self, float feed, float value, bool relative) {
    if (self->current_move_axis == AXIS_OFF) { return self->current_move_axis; }
    int axi = machine_interface_axis_t_idx(self->current_move_axis);
    if (axi < 0 || axi >= AXIS_OFF) { return self->current_move_axis; }

    char axis = idx_to_axis(axi);
    _df(0, ">> MOVE_CURR_AX: %d, %c [%c, %c, %c].\n", axi, axis, axes[0], axes[1], axes[2]);
    _default_move_to(self, axis, feed, value, relative);
    return self->current_move_axis;
}

axis_t machine_interface_step_current_axis(machine_interface_t *self, float feed, int steps) {
    if (self->current_move_axis == AXIS_OFF) { return self->current_move_axis; }
    int axi = machine_interface_axis_t_idx(self->current_move_axis);
    if (axi < 0 || axi >= AXIS_OFF) { return self->current_move_axis; }

    char axis = idx_to_axis(axi);

    float dist = self->current_move_step * steps;
    _df(0, ">> MOVE_CURR_AX: %d, %c [%c, %c, %c].\n", axi, axis, axes[0], axes[1], axes[2]);
    _default_move_to(self, axis, feed, dist, true);
    return self->current_move_axis;
}

axis_t machine_interface_next_move_axis(machine_interface_t *self){
    size_t i = (size_t)self->current_move_axis;
    i = (i + 1) % (AXIS_OFF + 1);
    self->current_move_axis = (axis_t) i;

    machine_interface_current_move_axis_updated(self);

    return self->current_move_axis;
}

axis_t machine_interface_get_current_move_axis(machine_interface_t *self) {
    return self->current_move_axis;
}

void machine_interface_set_current_move_axis(machine_interface_t *self, axis_t axis) {
    _df(0, ">> SET_CURR_AX: %d => %d [%c, %c, %c].\n", self->current_move_axis, axis, axes[0], axes[1], axes[2]);
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

void machine_interface_setup_loop(machine_interface_t *self) {
    _d(0, "Machine event loop starting...");
    size_t i = 0;
    while (1) { 
        machine_interface_process_gcode_q(self);
        if (i++ % MACHINE_POLL_EVERY_NTH_INTERVAL == 0) {
            machine_interface_task_loop_iter(self);
        }
        if (i % 500 == 0) {
            _df(0, "Machine task stack size high: %d\n", uxTaskGetStackHighWaterMark(NULL));
        }

        vTaskDelay(pdMS_TO_TICKS(self->procrate_ms));
    }
}

void machine_interface_maybe_execute_continuous_move(machine_interface_t *self) {
    // C implementation of maybe_execute_continuous_move
}

void machine_interface_position_updated(machine_interface_t *self) {
    machine_interface_maybe_execute_continuous_move(self);
    call_callbacks(pos_changed_cb);
}

void machine_interface_home_updated(machine_interface_t *self) {
    call_callbacks(home_changed_cb);
}

void machine_interface_wcs_updated(machine_interface_t *self) {
    // Clear target positions
    for (int i = 0; i < 3; i++) {
        self->target_position[i] = 0.0f; // Or any appropriate default value
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

void machine_interface_files_updated(machine_interface_t *self, const char *fdir) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (strcmp(fdir, self->files_changed_cb[i].path) == 0) {
           self->files_changed_cb[i].cb_fn(self, self->files_changed_cb[i].user_data, fdir, self->filelists[i].files);
        }
    }
}

void machine_interface_connected_updated(machine_interface_t *self) {
    call_callbacks(connected_changed_cb);
}

void machine_interface_current_move_axis_updated(machine_interface_t *self) {
    call_callbacks(current_move_axis_changed_cb);
}

void machine_interface_update_position(machine_interface_t *self, float *values, float *values_wcs)
{
    memcpy(self->position, values, sizeof(self->position));
    memcpy(self->wcs_position, values_wcs, sizeof(self->wcs_position));
    machine_interface_position_updated(self);
}

bool machine_interface_is_continuous_move(machine_interface_t *self) {
    return !self->move_step;
}

bool machine_interface_add_files_changed_cb(machine_interface_t *self, const char *path, void *user_data, files_changed_callback_cb_t cb) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!self->files_changed_cb[i].cb_fn) {
            self->files_changed_cb[i].cb_fn = cb;
            self->files_changed_cb[i].user_data = user_data;
            return true;
        }
    }
    assert(0 && "Maximum number of callbacks reached");
    return false;
}

void machine_interface_modal_ok(machine_interface_t *self, int modal_id) {
    if (self->modal_ok) { self->modal_ok(self, modal_id); }
}

void machine_interface_modal_cancel(machine_interface_t *self, int modal_id) {
    if (self->modal_cancel) { self->modal_cancel(self, modal_id); }
}

void machine_interface_modal_choice(machine_interface_t *self, int choice, int modal_id) {
    if (self->modal_choice) { self->modal_choice(self, choice, modal_id); }
}

void machine_interface_modal_int(machine_interface_t *self, int val, int modal_id) {
    if (self->modal_int) { self->modal_int(self, val, modal_id); }
}

void machine_interface_modal_float(machine_interface_t *self, float val, int modal_id) {
    if (self->modal_float) { self->modal_float(self, val, modal_id); }
}

void machine_interface_modal_str(machine_interface_t *self, const char *val, int modal_id) {
    if (self->modal_str) { self->modal_str(self, val, modal_id); }
}

void machine_interface_probe(machine_interface_t *self, const char *probe_gcode) {
    if (self->probe) { self->probe(self, probe_gcode); }
}

void machine_interface_process_machine_state_response(machine_interface_t *self, void *data, size_t len) {
    self->process_machine_state_response(self, data, len);
}

add_callback_fn(machine_interface, state_change)
add_callback_fn(machine_interface, pos_changed)
add_callback_fn(machine_interface, home_changed)
add_callback_fn(machine_interface, wcs_changed)
add_callback_fn(machine_interface, feed_changed)
add_callback_fn(machine_interface, sensors_changed)
add_callback_fn(machine_interface, dialogs_changed)
add_callback_fn(machine_interface, spindles_tools_changed)
add_callback_fn(machine_interface, connected_changed)
add_callback_fn(machine_interface, current_move_axis_changed)
