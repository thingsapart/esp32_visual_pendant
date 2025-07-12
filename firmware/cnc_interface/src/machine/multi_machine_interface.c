// multi_machine_interface.c
#include "multi_machine_interface.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "debug.h"

static const char *TAG = "multi_machine";

// --- Forward Declarations (for internal "virtual" methods) ---
// These are *static* because they're only used within this file.

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
static char *_multi_machine_debug_print(
    machine_interface_t *self);  // Not yet implemented.
static void _multi_machine_modal_cancel(machine_interface_t *self,
                                        int modal_id);
static void _multi_machine_modal_ok(machine_interface_t *self, int modal_id);
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
static void _multi_machine__continuous_stop(machine_interface_t *self);
static void _multi_machine__continuous_move(machine_interface_t *self,
                                            const char axis, float feed,
                                            int direction);

// --- Method Implementations ---

static void _multi_machine_send_gcode(machine_interface_t *self,
                                      const char *gcode, uint32_t poll_state) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    LOGI(TAG, "Sending machine %d => %s (conn %d)", i, gcode,
         multi_self->machines[i]->is_connected(multi_self->machines[i]));

    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->send_gcode) {  // Always check for NULL
        multi_self->machines[i]->send_gcode(multi_self->machines[i], gcode,
                                            poll_state);
      }
    }
  }
}

static void _multi_machine__send_gcode(machine_interface_t *self,
                                       const char *gcode) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    LOGI(TAG, "Sending machine %d => %s (conn %d)", i, gcode,
         multi_self->machines[i]->is_connected(multi_self->machines[i]));
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->_send_gcode) {  // Always check for NULL
        multi_self->machines[i]->_send_gcode(multi_self->machines[i], gcode);
      }
    }
  }
}

static void _multi_machine_update_machine_state(machine_interface_t *self,
                                                uint32_t poll_state) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]
              ->_update_machine_state) {  // Always check for NULL
        multi_self->machines[i]->_update_machine_state(multi_self->machines[i],
                                                       poll_state);
      }
    }
  }
}

static bool _multi_machine_is_connected(machine_interface_t *self) {
  // Return true if ANY of the child machines are connected.
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  bool any_connected = false;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    // Check for NULL and then call is_connected
    LOGI(TAG, "Connected machine %d => (conn %d)", i,
         multi_self->machines[i]->is_connected(multi_self->machines[i]));
    if (multi_self->machines[i] && multi_self->machines[i]->is_connected) {
      if (multi_self->machines[i]->is_connected(multi_self->machines[i])) {
        any_connected = true;
      }
    }
  }
  return any_connected;
}

static void _multi_machine_list_files(machine_interface_t *self,
                                      const char *path) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->list_files) {  // Always check for NULL
        multi_self->machines[i]->list_files(multi_self->machines[i], path);
      }
    }
  }
}
static void _multi_machine_run_macro(machine_interface_t *self,
                                     const char *macro_name) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->run_macro) {  // Always check for NULL
        multi_self->machines[i]->run_macro(multi_self->machines[i], macro_name);
      }
    }
  }
}

static void _multi_machine_start_job(machine_interface_t *self,
                                     const char *job_name) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->start_job) {  // Always check for NULL
        multi_self->machines[i]->start_job(multi_self->machines[i], job_name);
      }
    }
  }
}

static void _multi_machine_move_continuous(machine_interface_t *self,
                                           const char axis, float feed,
                                           int direction) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->move_continuous) {  // Always check for NULL
        multi_self->machines[i]->move_continuous(multi_self->machines[i], axis,
                                                 feed, direction);
      }
    }
  }
}

static void _multi_machine__continuous_move(machine_interface_t *self,
                                            const char axis, float feed,
                                            int direction) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->_continuous_move) {  // Always check for NULL
        multi_self->machines[i]->_continuous_move(multi_self->machines[i], axis,
                                                  feed, direction);
      }
    }
  }
}

static void _multi_machine_move_continuous_stop(machine_interface_t *self) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]
              ->move_continuous_stop) {  // Always check for NULL
        multi_self->machines[i]->move_continuous_stop(multi_self->machines[i]);
      }
    }
  }
}
static void _multi_machine__continuous_stop(machine_interface_t *self) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->_continuous_stop) {  // Always check for NULL
        multi_self->machines[i]->_continuous_stop(multi_self->machines[i]);
      }
    }
  }
}

static void _multi_machine_move(machine_interface_t *self, const char axis,
                                float feed, float value) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->move) {  // Always check for NULL
        multi_self->machines[i]->move(multi_self->machines[i], axis, feed,
                                      value);
      }
    }
  }
}

static void _multi_machine_home_all(machine_interface_t *self) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->home_all) {  // Always check for NULL
        multi_self->machines[i]->home_all(multi_self->machines[i]);
      }
    }
  }
}

static void _multi_machine_home(machine_interface_t *self, const char *axes) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->home) {  // Always check for NULL
        multi_self->machines[i]->home(multi_self->machines[i], axes);
      }
    }
  }
}

static void _multi_machine_set_wcs(machine_interface_t *self, int wcs) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->set_wcs) {  // Always check for NULL
        multi_self->machines[i]->set_wcs(multi_self->machines[i], wcs);
      }
    }
  }
}

static void _multi_machine_set_wcs_zero(machine_interface_t *self, int wcs,
                                        const char *axes) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->set_wcs_zero) {
        multi_self->machines[i]->set_wcs_zero(multi_self->machines[i], wcs,
                                              axes);
      }
    }
  }
}

static void _multi_machine_next_wcs(machine_interface_t *self) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->next_wcs) {
        multi_self->machines[i]->next_wcs(multi_self->machines[i]);
      }
    }
  }
}

static void _multi_machine_modal_cancel(machine_interface_t *self,
                                        int modal_id) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->modal_cancel) {
        multi_self->machines[i]->modal_cancel(multi_self->machines[i],
                                              modal_id);
      }
    }
  }
}

static void _multi_machine_modal_ok(machine_interface_t *self, int modal_id) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->modal_ok) {
        multi_self->machines[i]->modal_ok(multi_self->machines[i], modal_id);
      }
    }
  }
}
static void _multi_machine_modal_choice(machine_interface_t *self, int choice,
                                        int modal_id) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->modal_choice) {
        multi_self->machines[i]->modal_choice(multi_self->machines[i], choice,
                                              modal_id);
      }
    }
  }
}

static void _multi_machine_modal_int(machine_interface_t *self, int val,
                                     int modal_id) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->modal_int) {
        multi_self->machines[i]->modal_int(multi_self->machines[i], val,
                                           modal_id);
      }
    }
  }
}
static void _multi_machine_modal_float(machine_interface_t *self, float val,
                                       int modal_id) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->modal_float) {
        multi_self->machines[i]->modal_float(multi_self->machines[i], val,
                                             modal_id);
      }
    }
  }
}
static void _multi_machine_modal_str(machine_interface_t *self, const char *val,
                                     int modal_id) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->modal_str) {
        multi_self->machines[i]->modal_str(multi_self->machines[i], val,
                                           modal_id);
      }
    }
  }
}
static void _multi_machine_probe(machine_interface_t *self,
                                 const char *probe_gcode) {
  multi_machine_interface_t *multi_self = (multi_machine_interface_t *)self;
  for (size_t i = 0; i < multi_self->num_machines; i++) {
    if (multi_self->machines[i]->is_connected &&
        multi_self->machines[i]->is_connected(multi_self->machines[i])) {
      if (multi_self->machines[i]->probe) {
        multi_self->machines[i]->probe(multi_self->machines[i], probe_gcode);
      }
    }
  }
}

static char *_multi_machine_debug_print(machine_interface_t *self) {
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
  // Initialize base class (important!)
  machine_interface_init(
      &self->base,
      MACHINE_SEND_GCODE_INTERVAL_MS);  // No internal processing loop for the
                                        // multi-machine

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
  // mm->base.current_move_axis = mach->current_move_axis;
  // mm->base.current_move_step = mach->current_move_step;
  mm->base.wcs = mach->wcs;
  mm->base.z_offs = mach->z_offs;
  mm->base.feed = mach->feed;
  mm->base.feed_req = mach->feed_req;
  // mm->base.move_relative = mach->move_relative;
  // mm->base.move_step = mach->move_step;
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
  if (mach->message_box) {
    if (mm->base.message_box) {
      free(mm->base.message_box->title);
      free(mm->base.message_box->text);
      for (size_t i = 0; i < mm->base.message_box->num_choices; ++i) {
        free(mm->base.message_box->choices[i]);
      }
      free(mm->base.message_box->choices);
    }
    mm->base.message_box = malloc(sizeof(message_box_t));
    mm->base.message_box->mode = mach->message_box->mode;
    mm->base.message_box->title = strndup(mach->message_box->title, 128);
    mm->base.message_box->text = strndup(mach->message_box->text, 512);
    mm->base.message_box->num_choices = mach->message_box->num_choices;
    mm->base.message_box->choices =
        malloc(sizeof(char *) * mach->message_box->num_choices);
    for (size_t i = 0; i < mm->base.message_box->num_choices; ++i) {
      mm->base.message_box->choices[i] =
          strndup(mach->message_box->choices[i], 50);
    }
    mm->base.message_box->machine = &mm->base;
  }
}

static void _mach_copy_state(multi_machine_interface_t *mm,
                             machine_interface_t *mach) {
  MACH_MEMCPY(axes_homed);
  MACH_MEMCPY(position);
  MACH_MEMCPY(wcs_position);
  MACH_MEMCPY(target_position);
  // mm->base.current_move_axis = mach->current_move_axis;
  // mm->base.current_move_step = mach->current_move_step;
  mm->base.wcs = mach->wcs;
  mm->base.z_offs = mach->z_offs;
  mm->base.feed = mach->feed;
  mm->base.feed_req = mach->feed_req;
  // mm->base.move_relative = mach->move_relative;
  // mm->base.move_step = mach->move_step;

  if (mach->tool) {
    if (mm->base.tool) {
      free((void *)mm->base.tool);
    }
    mm->base.tool = strdup(mach->tool);
  }

  MACH_ARRCPY(probes);
  MACH_ARRCPY(end_stops);
  MACH_ARRCPY(spindles);

  if (mach->message_box) {
    if (!mm->base.message_box) {
      free(mm->base.message_box->title);
      free(mm->base.message_box->text);
      for (size_t i = 0; i < mm->base.message_box->num_choices; ++i) {
        free(mm->base.message_box->choices[i]);
      }
      free(mm->base.message_box->choices);
    }
    mm->base.message_box = malloc(sizeof(message_box_t));
    mm->base.message_box->title = strndup(mach->message_box->title, 128);
    mm->base.message_box->text = strndup(mach->message_box->text, 512);
    mm->base.message_box->num_choices = mach->message_box->num_choices;
    mm->base.message_box->choices =
        malloc(sizeof(char *) * mach->message_box->num_choices);
    for (size_t i = 0; i < mm->base.message_box->num_choices; ++i) {
      mm->base.message_box->choices[i] =
          strndup(mach->message_box->choices[i], 50);
    }
    mm->base.message_box->machine = &mm->base;
  }
}

#undef MACH_MEMCPY
#undef MACH_ARRCPY

void _mach_cb_state(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  // NOP, done on every task loop iter.
  // ? _mach_copy_state(self, mach);
  _mach_copy_pos(self, mach);
}

void _mach_cb_pos(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;

  _mach_copy_pos(self, mach);
  LOGI(TAG, "Pos updated.");
  machine_interface_position_updated(&self->base);
}

void _mach_cb_home(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;

  _mach_copy_homed(self, mach);
  machine_interface_home_updated(&self->base);
}

void _mach_cb_wcs(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;

  _mach_copy_wcs(self, mach);
  machine_interface_wcs_updated(&self->base);
}

void _mach_cb_feed(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  _mach_copy_feed(self, mach);
  machine_interface_feed_updated(&self->base);
}

void _mach_cb_sensors(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;

  _mach_copy_probes(self, mach);
  _mach_copy_end_stops(self, mach);

  machine_interface_sensors_updated(&self->base);
}

void _mach_cb_dialogs(machine_interface_t *mach, void *user_data) {
  LOGI(TAG, "MESSAGE BOX UPDATED CB");
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  _mach_copy_message_box(self, mach);
  LOGI(TAG, "MESSAGE BOX UPDATED: %s / %s", mach->message_box->title,
       mach->message_box->text);

  machine_interface_dialogs_updated(&self->base);
}

void _mach_cb_spindles(machine_interface_t *mach, void *user_data) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;
  _mach_copy_spindles(self, mach);
  machine_interface_spindles_tools_updated(&self->base);
}

void _mach_cb_files(machine_interface_t *mach, void *user_data,
                    const char *path, char **files) {
  multi_machine_interface_t *self = (multi_machine_interface_t *)user_data;

  _mach_copy_files(self, mach);

  machine_interface_files_updated(&self->base, path);
}

bool multi_machine_add_impl(multi_machine_interface_t *self,
                            machine_interface_t *machine) {
  if (!self || !machine) {
    return false;
  }
  if (self->num_machines >= MAX_MACHINES) {
    LOGE(TAG, "Maximum number of machines reached");
    return false;  // Too many machines
  }
  self->machines[self->num_machines++] = machine;

  machine_interface_add_state_change_cb(machine, self, _mach_cb_state);
  machine_interface_add_pos_changed_cb(machine, self, _mach_cb_pos);
  machine_interface_add_home_changed_cb(machine, self, _mach_cb_home);
  machine_interface_add_wcs_changed_cb(machine, self, _mach_cb_wcs);
  machine_interface_add_feed_changed_cb(machine, self, _mach_cb_feed);
  machine_interface_add_sensors_changed_cb(machine, self, _mach_cb_sensors);
  machine_interface_add_spindles_tools_changed_cb(machine, self,
                                                  _mach_cb_spindles);
  machine_interface_add_dialogs_changed_cb(machine, self, _mach_cb_dialogs);
  machine_interface_add_files_changed_cb(machine, NULL, self, _mach_cb_files);

  LOGI(TAG, "Added machine interface, total: %u", self->num_machines);
  return true;
}