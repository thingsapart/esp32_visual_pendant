// multi_machine_interface.c
#include "multi_machine_interface.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "debug.h"

#include "config.h"

static const char *TAG = "multi_machine";

// --- Forward Declarations (for internal "virtual" methods) ---
// These are *static* because they're only used within this file.

static void _multi_machine_send_gcode(machine_interface_t *self, const char *gcode, uint32_t poll_state);
static void _multi_machine__send_gcode(machine_interface_t *self, const char *gcode);
static void _multi_machine_update_machine_state(machine_interface_t *self, uint32_t poll_state);
static bool _multi_machine_is_connected(machine_interface_t *self);
static void _multi_machine_list_files(machine_interface_t *self, const char *path);
static void _multi_machine_run_macro(machine_interface_t *self, const char *macro_name);
static void _multi_machine_start_job(machine_interface_t *self, const char *job_name);
static void _multi_machine_move_continuous(machine_interface_t *self, const char axis, float feed, int direction);
static void _multi_machine_move_continuous_stop(machine_interface_t *self);
static void _multi_machine_move(machine_interface_t *self, const char axis, float feed, float value);
static void _multi_machine_home_all(machine_interface_t *self);
static void _multi_machine_home(machine_interface_t *self, const char *axes);
static void _multi_machine_set_wcs(machine_interface_t *self, int wcs);
static void _multi_machine_set_wcs_zero(machine_interface_t *self, int wcs, const char *axes);
static void _multi_machine_next_wcs(machine_interface_t *self);
static char* _multi_machine_debug_print(machine_interface_t *self); // Not yet implemented.
static void _multi_machine_modal_cancel(machine_interface_t *self, int modal_id);
static void _multi_machine_modal_ok(machine_interface_t *self, int modal_id);
static void _multi_machine_modal_choice(machine_interface_t *self, int choice, int modal_id);
static void _multi_machine_modal_int(machine_interface_t *self, int val, int modal_id);
static void _multi_machine_modal_float(machine_interface_t *self, float val, int modal_id);
static void _multi_machine_modal_str(machine_interface_t *self, const char *val, int modal_id);
static void _multi_machine_probe(machine_interface_t *self, const char *probe_gcode);
static void _multi_machine__continuous_stop(machine_interface_t *self);
static void _multi_machine__continuous_move(machine_interface_t *self, const char axis, float feed, int direction);


// --- Method Implementations ---

static void _multi_machine_send_gcode(machine_interface_t *self, const char *gcode, uint32_t poll_state) {
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
            if(multi_self->machines[i]->send_gcode) { // Always check for NULL
                multi_self->machines[i]->send_gcode(multi_self->machines[i], gcode, poll_state);
            }
        }
    }
}

static void _multi_machine__send_gcode(machine_interface_t *self, const char *gcode)
{
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if(multi_self->machines[i]->_send_gcode) { // Always check for NULL
                multi_self->machines[i]->_send_gcode(multi_self->machines[i], gcode);
             }
        }
    }
}

static void _multi_machine_update_machine_state(machine_interface_t *self, uint32_t poll_state)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if (multi_self->machines[i]->_update_machine_state) { // Always check for NULL
                multi_self->machines[i]->_update_machine_state(multi_self->machines[i], poll_state);
             }
        }
    }
}

static bool _multi_machine_is_connected(machine_interface_t *self)
{
    // Return true if ANY of the child machines are connected.
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    bool any_connected = false;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        // Check for NULL and then call is_connected
        if (multi_self->machines[i] && multi_self->machines[i]->is_connected) {
            if (multi_self->machines[i]->is_connected(multi_self->machines[i])) {
                any_connected = true;
            }
        }
    }
    return any_connected;
}

static void _multi_machine_list_files(machine_interface_t *self, const char *path)
{
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
              if (multi_self->machines[i]->list_files) { // Always check for NULL
                multi_self->machines[i]->list_files(multi_self->machines[i], path);
              }
        }
    }
}
static void _multi_machine_run_macro(machine_interface_t *self, const char *macro_name)
{
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
              if (multi_self->machines[i]->run_macro) { // Always check for NULL
                multi_self->machines[i]->run_macro(multi_self->machines[i], macro_name);
              }
        }
    }
}

static void _multi_machine_start_job(machine_interface_t *self, const char *job_name)
{
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
              if (multi_self->machines[i]->start_job) { // Always check for NULL
                multi_self->machines[i]->start_job(multi_self->machines[i], job_name);
              }
        }
    }
}

static void _multi_machine_move_continuous(machine_interface_t *self, const char axis, float feed, int direction)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
              if (multi_self->machines[i]->move_continuous) { // Always check for NULL
                multi_self->machines[i]->move_continuous(multi_self->machines[i], axis, feed, direction);
              }
        }
    }
}

static void _multi_machine__continuous_move(machine_interface_t *self, const char axis, float feed, int direction)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
              if (multi_self->machines[i]->_continuous_move) { // Always check for NULL
                multi_self->machines[i]->_continuous_move(multi_self->machines[i], axis, feed, direction);
              }
        }
    }
}

static void _multi_machine_move_continuous_stop(machine_interface_t *self)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
              if(multi_self->machines[i]->move_continuous_stop) { // Always check for NULL
                multi_self->machines[i]->move_continuous_stop(multi_self->machines[i]);
              }
        }
    }
}
static void _multi_machine__continuous_stop(machine_interface_t *self)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if (multi_self->machines[i]->_continuous_stop) { // Always check for NULL
                multi_self->machines[i]->_continuous_stop(multi_self->machines[i]);
             }
        }
    }
}

static void _multi_machine_move(machine_interface_t *self, const char axis, float feed, float value)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if (multi_self->machines[i]->move) { // Always check for NULL
                multi_self->machines[i]->move(multi_self->machines[i], axis, feed, value);
             }
        }
    }
}

static void _multi_machine_home_all(machine_interface_t *self)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
            if(multi_self->machines[i]->home_all) { // Always check for NULL
                multi_self->machines[i]->home_all(multi_self->machines[i]);
            }
        }
    }
}

static void _multi_machine_home(machine_interface_t *self, const char *axes)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
              if (multi_self->machines[i]->home) { // Always check for NULL
                multi_self->machines[i]->home(multi_self->machines[i], axes);
              }
        }
    }
}

static void _multi_machine_set_wcs(machine_interface_t *self, int wcs)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
              if (multi_self->machines[i]->set_wcs) { // Always check for NULL
                multi_self->machines[i]->set_wcs(multi_self->machines[i], wcs);
              }
        }
    }
}

static void _multi_machine_set_wcs_zero(machine_interface_t *self, int wcs, const char *axes)
{
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if(multi_self->machines[i]->set_wcs_zero) {
                multi_self->machines[i]->set_wcs_zero(multi_self->machines[i], wcs, axes);
             }
        }
    }
}

static void _multi_machine_next_wcs(machine_interface_t *self)
{
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if(multi_self->machines[i]->next_wcs) {
                multi_self->machines[i]->next_wcs(multi_self->machines[i]);
             }
        }
    }
}

static void _multi_machine_modal_cancel(machine_interface_t *self, int modal_id)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if (multi_self->machines[i]->modal_cancel) {
                multi_self->machines[i]->modal_cancel(multi_self->machines[i], modal_id);
             }
        }
    }
}

static void _multi_machine_modal_ok(machine_interface_t *self, int modal_id)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
            if(multi_self->machines[i]->modal_ok) {
                multi_self->machines[i]->modal_ok(multi_self->machines[i], modal_id);
            }
        }
    }
}
static void _multi_machine_modal_choice(machine_interface_t *self, int choice, int modal_id)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if(multi_self->machines[i]->modal_choice) {
                multi_self->machines[i]->modal_choice(multi_self->machines[i], choice, modal_id);
             }
        }
    }
}

static void _multi_machine_modal_int(machine_interface_t *self, int val, int modal_id)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
            if(multi_self->machines[i]->modal_int) {
                multi_self->machines[i]->modal_int(multi_self->machines[i], val, modal_id);
            }
        }
    }
}
static void _multi_machine_modal_float(machine_interface_t *self, float val, int modal_id)
{
    multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if(multi_self->machines[i]->modal_float) {
                multi_self->machines[i]->modal_float(multi_self->machines[i], val, modal_id);
             }
        }
    }
}
static void _multi_machine_modal_str(machine_interface_t *self, const char *val, int modal_id)
{
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if (multi_self->machines[i]->modal_str) {
                multi_self->machines[i]->modal_str(multi_self->machines[i], val, modal_id);
             }
        }
    }
}
static void _multi_machine_probe(machine_interface_t *self, const char* probe_gcode) {
     multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
    for (size_t i = 0; i < multi_self->num_machines; i++) {
        if (multi_self->machines[i]->is_connected && multi_self->machines[i]->is_connected(multi_self->machines[i])) {
             if(multi_self->machines[i]->probe) {
                multi_self->machines[i]->probe(multi_self->machines[i], probe_gcode);
             }
        }
    }
}

static char* _multi_machine_debug_print(machine_interface_t *self) {
    return NULL; // Not implemented for multi-machine.
}

// --- Constructor/Destructor ---

multi_machine_interface_t *multi_machine_interface_create() {
    multi_machine_interface_t *self = (multi_machine_interface_t *)malloc(sizeof(multi_machine_interface_t));
    if (!self) {
        ESP_LOGE(TAG, "Failed to allocate memory for multi_machine_interface");
        return NULL;
    }
     return multi_machine_interface_init(self);
}

multi_machine_interface_t *multi_machine_interface_init(multi_machine_interface_t *self)
{
    // Initialize base class (important!)
    machine_interface_init(&self->base, MACHINE_SEND_GCODE_INTERVAL_MS); // No internal processing loop for the multi-machine

    self->num_machines = 0;
    for (int i = 0; i < MAX_MACHINES; i++) {
        self->machines[i] = NULL;
    }

    // Override base class methods with multi-machine implementations
    self->base.send_gcode = _multi_machine_send_gcode;
    self->base._send_gcode = _multi_machine__send_gcode;
    self->base.is_connected = _multi_machine_is_connected;
    self->base._update_machine_state = _multi_machine_update_machine_state;
    self->base.list_files = _multi_machine_list_files;
    self->base.run_macro = _multi_machine_run_macro;
    self->base.start_job = _multi_machine_start_job;
    self->base.move_continuous = _multi_machine_move_continuous;
    self->base._continuous_move = _multi_machine__continuous_move;
    self->base.move_continuous_stop = _multi_machine_move_continuous_stop;
    self->base._continuous_stop = _multi_machine__continuous_stop;
    self->base.move = _multi_machine_move;
    self->base.home_all = _multi_machine_home_all;
    self->base.home = _multi_machine_home;
    self->base.set_wcs = _multi_machine_set_wcs;
    self->base.set_wcs_zero = _multi_machine_set_wcs_zero;
    self->base.next_wcs = _multi_machine_next_wcs;
    self->base.modal_cancel = _multi_machine_modal_cancel;
    self->base.modal_ok = _multi_machine_modal_ok;
    self->base.modal_choice = _multi_machine_modal_choice;
    self->base.modal_int = _multi_machine_modal_int;
    self->base.modal_float = _multi_machine_modal_float;
    self->base.modal_str = _multi_machine_modal_str;
    self->base.probe = _multi_machine_probe;

    self->base.debug_print = _multi_machine_debug_print;

    return self;
}

void multi_machine_interface_deinit(multi_machine_interface_t *self) {
    // Clean up any multi-machine-specific resources
    machine_interface_deinit(&self->base);

    // IMPORTANT:  Do *NOT* destroy the individual machine interfaces here.
    // They are owned and managed by whoever created them.
    // We are only *referencing* them.
}
void multi_machine_interface_destroy(multi_machine_interface_t *self) {
    if (self) {
        multi_machine_interface_deinit(self);

        free(self);
    }
}

void _mach_cb_state(machine_interface_t *mach, void *user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    // NOP, done on every task loop iter.
}

void _mach_cb_pos(machine_interface_t *mach, void *user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    machine_interface_position_updated(&self->base);
}

void _mach_cb_home(machine_interface_t *mach, void *user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    machine_interface_home_updated(&self->base);
}

void _mach_cb_wcs(machine_interface_t *mach, void *user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    machine_interface_wcs_updated(&self->base);
}

void _mach_cb_feed(machine_interface_t *mach, void *user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    machine_interface_feed_updated(&self->base);
}

void _mach_cb_sensors(machine_interface_t *mach, void *user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    machine_interface_sensors_updated(&self->base);
}


void _mach_cb_dialogs(machine_interface_t *mach, void *user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    machine_interface_dialogs_updated(&self->base);
}

void _mach_cb_spindles(machine_interface_t *mach, void *user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    machine_interface_spindles_tools_updated(&self->base);
}

void _mach_cb_files(machine_interface_t *mach, void *user_data, const char *path, const char **files) {
    multi_machine_interface_t *self = (multi_machine_interface_t *) user_data;
    machine_interface_files_updated(&self->base, path);
}

bool multi_machine_add_impl(multi_machine_interface_t *self, machine_interface_t *machine) {
    if (!self || !machine) {
        return false;
    }
    if (self->num_machines >= MAX_MACHINES) {
         ESP_LOGE(TAG, "Maximum number of machines reached");
        return false; // Too many machines
    }
    self->machines[self->num_machines++] = machine;

    machine_interface_add_state_change_cb(machine, self, _mach_cb_state);
    machine_interface_add_pos_changed_cb(machine, self, _mach_cb_pos);
    machine_interface_add_home_changed_cb(machine, self, _mach_cb_home);
    machine_interface_add_wcs_changed_cb(machine, self, _mach_cb_wcs);
    machine_interface_add_feed_changed_cb(machine, self, _mach_cb_feed);
    machine_interface_add_sensors_changed_cb(machine, self, _mach_cb_sensors);
    machine_interface_add_spindles_tools_changed_cb(machine, self, _mach_cb_spindles);
    machine_interface_add_files_changed_cb(machine, NULL, self, _mach_cb_files);

    ESP_LOGI(TAG, "Added machine interface, total: %zu", self->num_machines);
    return true;
}