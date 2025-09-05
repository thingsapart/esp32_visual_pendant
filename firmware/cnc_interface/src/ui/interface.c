#include "ui/interface.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "lvgl_ui.h"
#include "ui/ui_action_handler.h"
#include "ui/ui_setup_dwc.h"

static const char *TAG = "UI_INTERFACE";

#ifdef DWC_MACHINE_MODE
// Global flags from main.c to indicate startup connection failure
extern bool g_dwc_startup_connection_failed;
extern char g_dwc_startup_host[65];
#endif

// --- Machine State -> UI Callbacks ---
// These callbacks are executed in the machine thread. They should only
// set a dirty flag to notify the UI thread that an update is needed.

static void on_machine_state_change(machine_interface_t *machine,
                                    void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_MACHINE_STATE;
}

static void on_dialogs_change(machine_interface_t *machine, void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_DIALOGS;
}

static void on_position_change(machine_interface_t *machine, void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_POSITION;
}

static void on_homed_change(machine_interface_t *machine, void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_HOMING;
}

static void on_wcs_change(machine_interface_t *machine, void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_WCS;
}

static void on_feed_change(machine_interface_t *machine, void *user_data) {
  ((interface_t *)user_data)->dirty_flags |=
      (UI_DIRTY_FEEDRATE | UI_DIRTY_OVERRIDES);
}

static void on_spindle_tool_change(machine_interface_t *machine,
                                   void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_SPINDLE;
}

static void on_connected_change(machine_interface_t *machine, void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_CONNECTION;
}

static void on_current_move_axis_change(machine_interface_t *machine,
                                        void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_JOG_STATE;
}

static void on_files_change(machine_interface_t *machine, void *user_data,
                            const char *path, char **files) {
  interface_t *interface = (interface_t *)user_data;
  if (strstr(path, "gcode")) {
    interface->dirty_flags |= UI_DIRTY_FILES_GCODES;
  } else if (strstr(path, "macro")) {
    interface->dirty_flags |= UI_DIRTY_FILES_MACROS;
  }
}

/**
 * @brief Converts a machine_status_t enum to a human-readable string.
 */
static const char *machine_status_to_string(machine_status_t status) {
  switch (status) {
    case MACHINE_STATUS_RUNNING:
      return "RUNNING";
    case MACHINE_STATUS_PAUSED:
    case MACHINE_STATUS_PAUSED_DEC:
    case MACHINE_STATUS_PAUSED_RESUME:
      return "PAUSED";
    case MACHINE_STATUS_TOOL_CHANGING:
      return "TOOL CHANGE";
    case MACHINE_STATUS_BUSY:
      return "BUSY";
    case MACHINE_STATUS_INITIALIZING:
      return "INIT";
    case MACHINE_STATUS_EMERGENCY_HALTED:
      return "HALTED";
    case MACHINE_STATUS_OFF:
      return "OFF";
    default:
      return "IDLE";
  }
}

// --- Public API ---

void interface_init(interface_t *interface, machine_interface_t *machine) {
  interface->machine = machine;
  interface->dirty_flags = UI_DIRTY_ALL;  // Mark all as dirty for initial sync

  lvgl_ui_init();
  create_ui(lv_screen_active());

  interface->probing_wizard = obj_registry_get("probing_wizard");
  if (!interface->probing_wizard) {
    LOGW(TAG, "Failed to find 'probing_wizard' widget in registry!");
  }

  ui_action_handler_init(interface);

  // Register callbacks to get state updates from the machine
  machine_interface_add_state_change_cb(machine, interface,
                                        on_machine_state_change);
  machine_interface_add_pos_changed_cb(machine, interface, on_position_change);
  machine_interface_add_home_changed_cb(machine, interface, on_homed_change);
  machine_interface_add_wcs_changed_cb(machine, interface, on_wcs_change);
  machine_interface_add_feed_changed_cb(machine, interface, on_feed_change);
  machine_interface_add_spindles_tools_changed_cb(machine, interface,
                                                  on_spindle_tool_change);
  machine_interface_add_dialogs_changed_cb(machine, interface,
                                           on_dialogs_change);
  machine_interface_add_connected_changed_cb(machine, interface,
                                             on_connected_change);
  machine_interface_add_current_move_axis_changed_cb(
      machine, interface, on_current_move_axis_change);
  machine_interface_add_files_changed_cb(machine, "gcodes", interface,
                                         on_files_change);
  machine_interface_add_files_changed_cb(machine, "macros", interface,
                                         on_files_change);

#ifdef DWC_MACHINE_MODE
  // Check if we need to show the DWC setup screen
  bool needs_config =
      !dwc_settings_are_valid() || g_dwc_startup_connection_failed;
  data_binding_notify_state_changed(
      "dwc.needs_config", (binding_value_t){.type = BINDING_TYPE_BOOL,
                                            .as.b_val = needs_config});
  if (needs_config) {
    memset(&interface->setup_settings, 0, sizeof(dwc_settings_t));
    ui_start_dwc_setup_flow(interface);

    if (g_dwc_startup_connection_failed) {
      char error_msg[128];
      snprintf(error_msg, sizeof(error_msg), "Failed to auto-connect to\n%s",
               g_dwc_startup_host);
      data_binding_notify_state_changed(
          "dwc.startup_error_text",
          (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = error_msg});
    }
  }
#endif
}

void interface_tick(interface_t *interface) {
  if (interface->dirty_flags == UI_DIRTY_NONE) {
    return;
  }

  // Atomically copy and clear flags
  uint32_t flags_to_process = interface->dirty_flags;
  interface->dirty_flags = UI_DIRTY_NONE;

  machine_interface_t *machine = interface->machine;
  if (!machine) {
    return;
  }

  // --- Update UI based on dirty flags ---

  if (flags_to_process & UI_DIRTY_CONNECTION) {
    data_binding_notify_state_changed(
        "machine.connection_status",
        (binding_value_t){.type = BINDING_TYPE_BOOL,
                          .as.b_val = machine->is_connected(machine)});
  }

  if (flags_to_process & UI_DIRTY_MACHINE_STATE) {
    data_binding_notify_state_changed(
        "machine.status_text",
        (binding_value_t){.type = BINDING_TYPE_STRING,
                          .as.s_val =
                              machine_status_to_string(machine->machine_status)});
    data_binding_notify_state_changed(
        "machine.program_is_running",
        (binding_value_t){.type = BINDING_TYPE_BOOL,
                          .as.b_val = (machine->machine_status ==
                                       MACHINE_STATUS_RUNNING)});
    data_binding_notify_state_changed(
        "machine.program_is_paused",
        (binding_value_t){.type = BINDING_TYPE_BOOL,
                          .as.b_val = (machine->machine_status ==
                                       MACHINE_STATUS_PAUSED)});
  }

  if (flags_to_process & UI_DIRTY_POSITION) {
    data_binding_notify_state_changed(
        "motion.position.machine_x",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->position[0]});
    data_binding_notify_state_changed(
        "motion.position.machine_y",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->position[1]});
    data_binding_notify_state_changed(
        "motion.position.machine_z",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->position[2]});
    data_binding_notify_state_changed(
        "motion.position.work_x",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->wcs_position[0]});
    data_binding_notify_state_changed(
        "motion.position.work_y",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->wcs_position[1]});
    data_binding_notify_state_changed(
        "motion.position.work_z",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->wcs_position[2]});
  }

  if (flags_to_process & UI_DIRTY_HOMING) {
    data_binding_notify_state_changed(
        "motion.homed.x", (binding_value_t){.type = BINDING_TYPE_BOOL,
                                            .as.b_val = machine->axes_homed[0]});
    data_binding_notify_state_changed(
        "motion.homed.y", (binding_value_t){.type = BINDING_TYPE_BOOL,
                                            .as.b_val = machine->axes_homed[1]});
    data_binding_notify_state_changed(
        "motion.homed.z", (binding_value_t){.type = BINDING_TYPE_BOOL,
                                            .as.b_val = machine->axes_homed[2]});
  }

  if (flags_to_process & UI_DIRTY_WCS) {
    data_binding_notify_state_changed(
        "motion.wcs.active_name",
        (binding_value_t){.type = BINDING_TYPE_STRING,
                          .as.s_val =
                              machine_interface_get_wcs_str(machine, -1)});
  }

  if (flags_to_process & UI_DIRTY_OVERRIDES) {
    data_binding_notify_state_changed(
        "overrides.feed_pct",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->feed_multiplier * 100.0f});
  }

  if (flags_to_process & UI_DIRTY_FEEDRATE) {
    data_binding_notify_state_changed(
        "motion.feedrate_current",
        (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = machine->feed});
  }

  if (flags_to_process & UI_DIRTY_SPINDLE) {
    float rpm = 0.0f;
    if (machine->num_spindles > 0 && machine->spindles) {
      rpm = (float)machine->spindles[0].rpm;
    }
    data_binding_notify_state_changed(
        "spindle.speed_rpm",
        (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = rpm});
    data_binding_notify_state_changed(
        "spindle.is_on",
        (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = (rpm > 1e-5)});
  }

  if (flags_to_process & UI_DIRTY_DIALOGS) {
    bool active = (machine->message_box != NULL);
    data_binding_notify_state_changed(
        "dialog.is_active",
        (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = active});
    if (active) {
      data_binding_notify_state_changed(
          "dialog.title", (binding_value_t){.type = BINDING_TYPE_STRING,
                                             .as.s_val =
                                                 machine->message_box->title});
      data_binding_notify_state_changed(
          "dialog.text",
          (binding_value_t){.type = BINDING_TYPE_STRING,
                            .as.s_val = machine->message_box->text});
      data_binding_notify_state_changed(
          "dialog.mode",
          (binding_value_t){.type = BINDING_TYPE_FLOAT,
                            .as.f_val = (float)machine->message_box->mode});
    }
  }

  if (flags_to_process & UI_DIRTY_JOG_STATE) {
    axis_t current_axis = machine_interface_get_current_move_axis(machine);
    data_binding_notify_state_changed(
        "motion.jog.axis_is_x",
        (binding_value_t){.type = BINDING_TYPE_BOOL,
                          .as.b_val = (current_axis == AXIS_X)});
    data_binding_notify_state_changed(
        "motion.jog.axis_is_y",
        (binding_value_t){.type = BINDING_TYPE_BOOL,
                          .as.b_val = (current_axis == AXIS_Y)});
    data_binding_notify_state_changed(
        "motion.jog.axis_is_z",
        (binding_value_t){.type = BINDING_TYPE_BOOL,
                          .as.b_val = (current_axis == AXIS_Z)});

    data_binding_notify_state_changed(
        "motion.jog.step_xy",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->current_move_step_xy});
    data_binding_notify_state_changed(
        "motion.jog.step_z", (binding_value_t){.type = BINDING_TYPE_FLOAT,
                                               .as.f_val = machine->current_move_step_z});
  }
}
