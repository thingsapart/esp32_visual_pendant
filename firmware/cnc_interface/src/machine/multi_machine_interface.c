// multi_machine_interface.c
#include "multi_machine_interface.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#define UI_DEBUG_LOCAL_LEVEL D_VERBOSE
#include "debug.h"

static const char *TAG = "multi_machine";

// --- Forward Declarations (for internal "virtual" methods) ---
static void _multi_machine_send_gcode(machine_interface_t *self,
                                      const char *gcode, uint32_t poll_state);
static void _multi_machine__send_gcode(machine_interface_t *self,
                                       const char *gcode);
static void _multi_machine_update_machine_state(machine_interface_t *self,
                                                uint32_t poll_state);
static bool _multi_machine_is_connected(machine_interface_t *self);
static void _multi_machine_list_files(machine_interface_t *self,
                                      const char *path);
static void _multi_machine_run_macro(machine_interface_t *self,
                                     const char *macro_name);
static void _multi_machine_start_job(machine_interface_t *self,
                                     const char *job_name);
static void _multi_machine_move_continuous(machine_interface_t *self,
                                           const char axis, float feed,
                                           int direction);
static void _multi_machine_move_continuous_stop(machine_interface_t *self);
static void _multi_machine_move(machine_interface_t *self, const char axis,
                                float feed, float value);
static void _multi_machine_home_all(machine_interface_t *self);
static void _multi_machine_home(machine_interface_t *self, const char *axes);
static void _multi_machine_set_wcs(machine_interface_t *self, int wcs);
static void _multi_machine_set_wcs_zero(machine_interface_t *self, int wcs,
                                        const char *axes);
static void _multi_machine_next_wcs(machine_interface_t *self);
static char * IRAM_ATTR _multi_machine_debug_print(
    machine_interface_t *self);  // Not yet implemented.
static void _multi_machine_modal_cancel(machine_interface_t *self,
                                        int modal_id);
static void _multi_machine_modal_ok(machine_interface_t* self, int modal_id);
static void _multi_machine_modal_choice(machine_interface_t *self, int choice,
                                        int modal_id);
static void _multi_machine_modal_int(machine_interface_t *self, int val,
                                     int modal_id);
static void _multi_machine_modal_float(machine_interface_t *self, float val,
                                       int modal_id);
static void _multi_machine_modal_str(machine_interface_t *self, const char *val,
                                     int modal_id);
static void _multi_machine_probe(machine_interface_t *self,
                                 const char *probe_gcode);
static void _multi_machine_set_connected(machine_interface_t *self, bool connected);
static void _multi_machine_process_machine_state_response(
    machine_interface_t *self, void *data, size_t len);

static void _multi_machine__continuous_stop(machine_interface_t *self);
static void _multi_machine__continuous_move(machine_interface_t *self,
                                            const char axis, float feed,
                                            int direction);
static void _mach_copy_state(multi_machine_interface_t *mm,
                             machine_interface_t *mach);

// --- Method Implementations ---

#define GET_ACTIVE_MACHINE(self, mach_ptr)                                  \
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self; \
  if (multi_self->active_machine_idx == -1) {                               \
    LOGW(TAG, "Cannot perform action, no active machine.");                  \
    return;                                                                 \
  }                                                                         \
  machine_interface_t *mach_ptr =                                           \
      multi_self->machines[multi_self->active_machine_idx];                 \
  if (!mach_ptr) return

static void _multi_machine_send_gcode(machine_interface_t *self,
                                      const char *gcode, uint32_t poll_state) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->send_gcode) {
        active_mach->send_gcode(active_mach, gcode, poll_state);
    }
}

static void _multi_machine__send_gcode(machine_interface_t *self,
                                       const char *gcode) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->_send_gcode) {
        active_mach->_send_gcode(active_mach, gcode);
    }
}

static void _multi_machine_update_machine_state(machine_interface_t *self,
                                                uint32_t poll_state) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;

  if (multi_self->active_machine_idx != -1) {
    // === ACTIVE POLLING ===
    // We have a connection, only poll the active machine.
    machine_interface_t *active_mach =
        multi_self->machines[multi_self->active_machine_idx];
    if (active_mach && active_mach->_update_machine_state) {
      if (multi_self->base.gcode_queue && !active_mach->gcode_queue) {
        active_mach->gcode_queue = multi_self->base.gcode_queue;
      }
      active_mach->_update_machine_state(active_mach, poll_state);
    }
  } else {
    // === RECONNECT POLLING ===
    // No active connection, poll all interfaces to try and find one.
    LOGV(TAG, "No active machine, polling all interfaces for reconnect...");
    for (size_t i = 0; i < multi_self->num_machines; i++) {
      machine_interface_t *mach = multi_self->machines[i];
      if (mach && mach->_update_machine_state) {
        if (multi_self->base.gcode_queue && !mach->gcode_queue) {
          mach->gcode_queue = multi_self->base.gcode_queue;
        }
        mach->_update_machine_state(mach, poll_state);
      }
    }
  }
}

static bool _multi_machine_is_connected(machine_interface_t *self) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  return multi_self->active_machine_idx != -1;
}

static void _multi_machine_list_files(machine_interface_t *self,
                                      const char *path) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->list_files) {
        active_mach->list_files(active_mach, path);
    }
}
static void _multi_machine_run_macro(machine_interface_t *self,
                                     const char *macro_name) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->run_macro) {
        active_mach->run_macro(active_mach, macro_name);
    }
}

static void _multi_machine_start_job(machine_interface_t *self,
                                     const char *job_name) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->start_job) {
        active_mach->start_job(active_mach, job_name);
    }
}

static void _multi_machine_move_continuous(machine_interface_t *self,
                                           const char axis, float feed,
                                           int direction) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->move_continuous) {
        active_mach->move_continuous(active_mach, axis, feed, direction);
    }
}

static void _multi_machine__continuous_move(machine_interface_t *self,
                                            const char axis, float feed,
                                            int direction) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->_continuous_move) {
        active_mach->_continuous_move(active_mach, axis, feed, direction);
    }
}

static void _multi_machine_move_continuous_stop(machine_interface_t *self) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->move_continuous_stop) {
        active_mach->move_continuous_stop(active_mach);
    }
}
static void _multi_machine__continuous_stop(machine_interface_t *self) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->_continuous_stop) {
        active_mach->_continuous_stop(active_mach);
    }
}

static void _multi_machine_move(machine_interface_t *self, const char axis,
                                float feed, float value) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->move) {
        active_mach->move(active_mach, axis, feed, value);
    }
}

static void _multi_machine_home_all(machine_interface_t *self) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->home_all) {
        active_mach->home_all(active_mach);
    }
}

static void _multi_machine_home(machine_interface_t *self, const char *axes) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->home) {
        active_mach->home(active_mach, axes);
    }
}

static void _multi_machine_set_wcs(machine_interface_t *self, int wcs) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->set_wcs) {
        active_mach->set_wcs(active_mach, wcs);
    }
}

static void _multi_machine_set_wcs_zero(machine_interface_t *self, int wcs,
                                        const char *axes) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->set_wcs_zero) {
        active_mach->set_wcs_zero(active_mach, wcs, axes);
    }
}

static void _multi_machine_next_wcs(machine_interface_t *self) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->next_wcs) {
        active_mach->next_wcs(active_mach);
    }
}

static void _multi_machine_modal_cancel(machine_interface_t *self,
                                        int modal_id) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->modal_cancel) {
        active_mach->modal_cancel(active_mach, modal_id);
    }
}

static void _multi_machine_modal_ok(machine_interface_t *self, int modal_id) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->modal_ok) {
        active_mach->modal_ok(active_mach, modal_id);
    }
}
static void _multi_machine_modal_choice(machine_interface_t *self, int choice,
                                        int modal_id) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->modal_choice) {
        active_mach->modal_choice(active_mach, choice, modal_id);
    }
}

static void _multi_machine_modal_int(machine_interface_t *self, int val,
                                     int modal_id) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->modal_int) {
        active_mach->modal_int(active_mach, val, modal_id);
    }
}
static void _multi_machine_modal_float(machine_interface_t *self, float val,
                                       int modal_id) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->modal_float) {
        active_mach->modal_float(active_mach, val, modal_id);
    }
}
static void _multi_machine_modal_str(machine_interface_t *self, const char *val,
                                     int modal_id) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->modal_str) {
        active_mach->modal_str(active_mach, val, modal_id);
    }
}
static void _multi_machine_probe(machine_interface_t *self,
                                 const char *probe_gcode) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->probe) {
        active_mach->probe(active_mach, probe_gcode);
    }
}

static void _multi_machine_set_connected(machine_interface_t *self, bool connected) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->set_connected) {
        active_mach->set_connected(active_mach, connected);
    }
}

static void _multi_machine_process_machine_state_response(
    machine_interface_t *self, void *data, size_t len) {
    GET_ACTIVE_MACHINE(self, active_mach);
    if(active_mach->process_machine_state_response) {
        active_mach->process_machine_state_response(active_mach, data, len);
    }
}

static char * IRAM_ATTR _multi_machine_debug_print(machine_interface_t *self) {
  return NULL;  // Not implemented for multi-machine.
}

// --- Constructor/Destructor ---

multi_machine_interface_t *multi_machine_interface_create() {
  multi_machine_interface_t *self =
      (multi_machine_interface_t *)malloc(sizeof(multi_machine_interface_t));
  if (!self) {
    LOGE(TAG, "Failed to allocate memory for multi_machine_interface");
    return NULL;
  }
  return multi_machine_interface_init(self);
}

multi_machine_interface_t *multi_machine_interface_init(
    multi_machine_interface_t *self) {
  machine_interface_init(&self->base, MACHINE_SEND_GCODE_INTERVAL_MS);

  self->num_machines = 0;
  self->active_machine_idx = -1;
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

  self->base.set_connected = _multi_machine_set_connected;
  self->base.process_machine_state_response =
      _multi_machine_process_machine_state_response;

  self->base.debug_print = _multi_machine_debug_print;

  return self;
}

void multi_machine_interface_deinit(multi_machine_interface_t *self) {
  machine_interface_deinit(&self->base);
}
void multi_machine_interface_destroy(multi_machine_interface_t *self) {
  if (self) {
    multi_machine_interface_deinit(self);

    free(self);
  }
}

#define MACH_MEMCPY(prop) \
  (memcpy(mm->base.prop, mach->prop, sizeof(mach->prop)))
#define MACH_ARRCPY(prop)                                        \
  do {                                                           \
    if (mm->base.num_##prop > 0) {                               \
      free(mm->base.prop);                                       \
    }                                                            \
    if (mach->num_##prop > 0) {                                  \
      size_t __len_a = sizeof(mach->prop[0]) * mach->num_##prop; \
      mm->base.prop = malloc(__len_a);                           \
      memcpy(mm->base.prop, mach->prop, __len_a);                \
    }                                                            \
  } while (0)

static void _mach_copy_homed(multi_machine_interface_t *mm,
                             machine_interface_t *mach) {
  MACH_MEMCPY(axes_homed);
  MACH_MEMCPY(position);
  MACH_MEMCPY(wcs_position);
  MACH_MEMCPY(target_position);
}

static void _mach_copy_wcs(multi_machine_interface_t *mm,
                           machine_interface_t *mach) {
  mm->base.wcs = mach->wcs;
}

static void _mach_copy_feed(multi_machine_interface_t *mm,
                            machine_interface_t *mach) {
  mm->base.feed = mach->feed;
  mm->base.feed_req = mach->feed_req;
}

static void _mach_copy_pos(multi_machine_interface_t *mm,
                           machine_interface_t *mach) {
  MACH_MEMCPY(axes_homed);
  MACH_MEMCPY(position);
  MACH_MEMCPY(wcs_position);
  MACH_MEMCPY(target_position);
  mm->base.wcs = mach->wcs;
  mm->base.z_offs = mach->z_offs;
  mm->base.feed = mach->feed;
  mm->base.feed_req = mach->feed_req;
}

static void _mach_copy_probes(multi_machine_interface_t *mm,
                              machine_interface_t *mach) {
  MACH_ARRCPY(probes);
}

static void _mach_copy_files(multi_machine_interface_t *mm,
                             machine_interface_t *mach) {
  for (size_t i = 0; i < MAX_FILE_LISTS; ++i) {
    if (mm->base.filelists[i].fdir) {
      free((void *)mm->base.filelists[i].fdir);
    }
    if (mm->base.filelists[i].files) {
      for (size_t j = 0; mm->base.filelists[i].files[j] != NULL; ++j) {
        free((void *)mm->base.filelists[i].files[j]);
      }
    }
    mm->base.filelists[i].fdir = NULL;
    mm->base.filelists[i].files = NULL;
  }
  for (size_t i = 0; i < MAX_FILE_LISTS; ++i) {
    if (mach->filelists[i].fdir != NULL) {
      size_t n_files = 0;
      for (n_files = 0; mach->filelists[i].files != NULL; ++n_files) {
      }
      mm->base.filelists[i].fdir = strdup(mach->filelists[i].fdir);
      mm->base.filelists[i].files = malloc(sizeof(char *) * n_files + 1);
      for (size_t j = 0; j < n_files; ++j) {
        mm->base.filelists[i].files[j] = strdup(mach->filelists[i].files[j]);
      }
      mm->base.filelists[i].files[n_files] = NULL;
    }
  }
}

static void _mach_copy_end_stops(multi_machine_interface_t *mm,
                                 machine_interface_t *mach) {
  MACH_ARRCPY(end_stops);
}

static void _mach_copy_spindles(multi_machine_interface_t *mm,
                                machine_interface_t *mach) {
  MACH_ARRCPY(spindles);
}

static void _mach_copy_message_box(multi_machine_interface_t *mm,
                                   machine_interface_t *mach) {
  if (mm->base.message_box) {
    free_message_box_t(mm->base.message_box);
    mm->base.message_box = NULL;
  }
  if (mach->message_box) {
    mm->base.message_box = (message_box_t *)calloc(1, sizeof(message_box_t));
    if (!mm->base.message_box) return;
    mm->base.message_box->mode = mach->message_box->mode;
    mm->base.message_box->seq = mach->message_box->seq;
    if (mach->message_box->title) mm->base.message_box->title = strdup(mach->message_box->title);
    if (mach->message_box->text) mm->base.message_box->text = strdup(mach->message_box->text);
    mm->base.message_box->num_choices = mach->message_box->num_choices;
    if (mach->message_box->num_choices > 0 && mach->message_box->choices) {
      mm->base.message_box->choices = (char **)calloc(mach->message_box->num_choices, sizeof(char *));
      if (mm->base.message_box->choices) {
        for (size_t i = 0; i < mach->message_box->num_choices; ++i) {
          if (mach->message_box->choices[i])
            mm->base.message_box->choices[i] = strdup(mach->message_box->choices[i]);
        }
      }
    }
    mm->base.message_box->machine = &mm->base;
  }
}

static void _mach_copy_state(multi_machine_interface_t *mm,
                             machine_interface_t *mach) {
  mm->base.machine_status = mach->machine_status;
  MACH_MEMCPY(axes_homed);
  MACH_MEMCPY(position);
  MACH_MEMCPY(wcs_position);
  MACH_MEMCPY(target_position);
  mm->base.wcs = mach->wcs;
  mm->base.z_offs = mach->z_offs;
  mm->base.feed = mach->feed;
  mm->base.feed_req = mach->feed_req;
  if (mach->tool) {
    if (mm->base.tool) free((void *)mm->base.tool);
    mm->base.tool = strdup(mach->tool);
  }
  MACH_ARRCPY(probes);
  MACH_ARRCPY(end_stops);
  MACH_ARRCPY(spindles);
  _mach_copy_message_box(mm, mach);
}

#undef MACH_MEMCPY
#undef MACH_ARRCPY

#define IS_ACTIVE_MACHINE(self, mach) (self->active_machine_idx != -1 && self->machines[self->active_machine_idx] == mach)

void _mach_cb_state(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_state(self, mach);
  machine_interface_state_updated(&self->base);
}

void _mach_cb_pos(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_pos(self, mach);
  machine_interface_position_updated(&self->base);
}

void _mach_cb_home(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_pos(self, mach);
  machine_interface_home_updated(&self->base);
}

void _mach_cb_wcs(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_wcs(self, mach);
  machine_interface_wcs_updated(&self->base);
}

void _mach_cb_feed(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_feed(self, mach);
  machine_interface_feed_updated(&self->base);
}

void _mach_cb_sensors(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_probes(self, mach);
  _mach_copy_end_stops(self, mach);
  machine_interface_sensors_updated(&self->base);
}

void _mach_cb_dialogs(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_message_box(self, mach);
  machine_interface_dialogs_updated(&self->base);
}

void _mach_cb_spindles(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_spindles(self, mach);
  machine_interface_spindles_tools_updated(&self->base);
}

void _mach_cb_files(machine_interface_t *mach, void *user_data,
                    const char *path, char **files) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  if (!IS_ACTIVE_MACHINE(self, mach)) return;
  _mach_copy_files(self, mach);
  machine_interface_files_updated(&self->base, path);
}

static void _mach_cb_connected(machine_interface_t* mach, void* user_data) {
    multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
    bool is_connected = mach->is_connected(mach);

    int mach_idx = -1;
    for (int i = 0; i < self->num_machines; i++) {
        if (self->machines[i] == mach) {
            mach_idx = i;
            break;
        }
    }
    if (mach_idx == -1) return;

    if (is_connected) {
        if (self->active_machine_idx == -1) {
            LOGI(TAG, "Machine %d connected and is now the ACTIVE machine.", mach_idx);
            self->active_machine_idx = mach_idx;
            _mach_copy_state(self, mach);
            machine_interface_state_updated(&self->base);
            machine_interface_position_updated(&self->base);
            machine_interface_home_updated(&self->base);
            machine_interface_wcs_updated(&self->base);
            machine_interface_feed_updated(&self->base);
            machine_interface_sensors_updated(&self->base);
            machine_interface_dialogs_updated(&self->base);
            machine_interface_spindles_tools_updated(&self->base);
            machine_interface_connected_updated(&self->base);
        } else {
             LOGW(TAG, "Machine %d connected, but machine %d is already active. Ignoring.", mach_idx, self->active_machine_idx);
        }
    } else {
        if (self->active_machine_idx == mach_idx) {
            LOGI(TAG, "Active machine %d disconnected. No active machine.", mach_idx);
            self->active_machine_idx = -1;
            machine_interface_connected_updated(&self->base);
        }
    }
}

bool multi_machine_add_impl(multi_machine_interface_t *self,
                            machine_interface_t *machine) {
  if (!self || !machine) return false;
  if (self->num_machines >= MAX_MACHINES) {
    LOGE(TAG, "Maximum number of machines reached");
    return false;
  }
  self->machines[self->num_machines++] = machine;
  machine->procrate_ms = self->base.procrate_ms;

  machine_interface_add_state_change_cb(machine, self, _mach_cb_state);
  machine_interface_add_pos_changed_cb(machine, self, _mach_cb_pos);
  machine_interface_add_home_changed_cb(machine, self, _mach_cb_home);
  machine_interface_add_wcs_changed_cb(machine, self, _mach_cb_wcs);
  machine_interface_add_feed_changed_cb(machine, self, _mach_cb_feed);
  machine_interface_add_sensors_changed_cb(machine, self, _mach_cb_sensors);
  machine_interface_add_spindles_tools_changed_cb(machine, self, _mach_cb_spindles);
  machine_interface_add_dialogs_changed_cb(machine, self, _mach_cb_dialogs);
  machine_interface_add_files_changed_cb(machine, NULL, self, _mach_cb_files);
  machine_interface_add_connected_changed_cb(machine, self, _mach_cb_connected);

  LOGI(TAG, "Added machine interface, total: %u", self->num_machines);
  return true;
}
