#include "ui_action_handler.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "lvgl_ui.h"

static const char *TAG = "UI_ACTION_HANDLER";

// Array of jog step values to cycle through.
static const float JOG_STEPS[] = {0.01, 0.1, 0.5, 1.0, 5.0};
static const size_t NUM_JOG_STEPS = sizeof(JOG_STEPS) / sizeof(JOG_STEPS[0]);

/**
 * @brief Cycles through the predefined jog step values.
 * @param current_step The current step value.
 * @return The next step value in the cycle.
 */
static float get_next_jog_step(float current_step) {
  for (size_t i = 0; i < NUM_JOG_STEPS; i++) {
    // Use a small epsilon for float comparison
    if (fabsf(current_step - JOG_STEPS[i]) < 1e-5) {
      return JOG_STEPS[(i + 1) % NUM_JOG_STEPS];
    }
  }
  return JOG_STEPS[0];  // Default to the first step if not found
}

/**
 * @brief The single, centralized handler for all actions from the UI.
 *
 * This function is registered with the data-binding system and is called
 * whenever a user interaction triggers a named action. It dispatches the
 * action to the appropriate machine_interface function.
 *
 * @param action_name The dot-notation name of the action (e.g.,
 * "action.program.run").
 * @param value The value associated with the action (e.g., from a toggle or
 * dialog).
 * @param user_data A pointer to the global interface_t struct.
 */
static void app_action_handler(const char *action_name, binding_value_t value,
                               void *user_data) {
  interface_t *interface = (interface_t *)user_data;
  machine_interface_t *machine = interface->machine;

  if (!machine) {
    LOGE(TAG, "Action '%s' triggered but machine interface is NULL!",
         action_name);
    return;
  }

  LOGD(TAG, "Action received: %s", action_name);

  // --- Program Actions ---
  if (strcmp(action_name, "action.program.run") == 0) {
    machine->send_gcode(machine, "~", 0);  // Cycle Start
  } else if (strcmp(action_name, "action.program.stop") == 0) {
    machine->send_gcode(machine, "!", 0);  // Feed Hold
  }

  // --- Motion, WCS & Homing Actions ---
  else if (strcmp(action_name, "action.motion.wcs.cycle") == 0) {
    machine->next_wcs(machine);
  } else if (strcmp(action_name, "action.motion.jog.axis_select_x") == 0) {
    if (machine_interface_get_current_move_axis(machine) == AXIS_X) {
      machine_interface_set_current_move_axis(machine, AXIS_OFF);
    } else {
      machine_interface_set_current_move_axis(machine, AXIS_X);
    }
  } else if (strcmp(action_name, "action.motion.jog.axis_select_y") == 0) {
    if (machine_interface_get_current_move_axis(machine) == AXIS_Y) {
      machine_interface_set_current_move_axis(machine, AXIS_OFF);
    } else {
      machine_interface_set_current_move_axis(machine, AXIS_Y);
    }
  } else if (strcmp(action_name, "action.motion.jog.axis_select_z") == 0) {
    if (machine_interface_get_current_move_axis(machine) == AXIS_Z) {
      machine_interface_set_current_move_axis(machine, AXIS_OFF);
    } else {
      machine_interface_set_current_move_axis(machine, AXIS_Z);
    }
  }

  // --- Jog Step Cycling ---
  else if (strcmp(action_name, "action.jog.cycle_step_xy") == 0) {
    machine->current_move_step_xy =
        get_next_jog_step(machine->current_move_step_xy);
    interface->dirty_flags |= UI_DIRTY_JOG_STATE;
  } else if (strcmp(action_name, "action.jog.cycle_step_z") == 0) {
    machine->current_move_step_z =
        get_next_jog_step(machine->current_move_step_z);
    interface->dirty_flags |= UI_DIRTY_JOG_STATE;
  }

  // --- Override Actions ---
  else if (strcmp(action_name, "action.overrides.feed.set_pct") == 0 &&
           value.type == BINDING_TYPE_FLOAT) {
    char gcode[32];
    snprintf(gcode, sizeof(gcode), "M220 S%.0f", value.as.f_val);
    machine->send_gcode(machine, gcode, 0);
  } else if (strcmp(action_name, "action.overrides.spindle.set_pct") == 0 &&
             value.type == BINDING_TYPE_FLOAT) {
    char gcode[32];
    snprintf(gcode, sizeof(gcode), "M221 S%.0f", value.as.f_val);
    machine->send_gcode(machine, gcode, 0);
  }

  // --- Probing Actions ---
  else if (strncmp(action_name, "action.probe.start.", 19) == 0) {
    const char *probe_type = action_name + 19;
    char gcode[128];
    LOGI(TAG, "Starting probe sequence: %s", probe_type);
    snprintf(gcode, sizeof(gcode), "M98 P\"/macros/probe_%s.g\"", probe_type);
    machine->probe(machine, gcode);
  }

  else {
    LOGW(TAG, "Unhandled action: %s", action_name);
  }
}

void ui_action_handler_init(interface_t *interface) {
  data_binding_register_action_handler(app_action_handler, interface);
}
