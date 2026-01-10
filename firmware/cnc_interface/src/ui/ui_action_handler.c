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
 * @brief Event handler for the "Home all?" confirmation modal.
 */
static void home_all_modal_event_handler(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_obj_t *label = lv_obj_get_child(obj, 0);
    lv_obj_t *mbox = lv_event_get_user_data(e);

    machine_interface_t * machine = lv_obj_get_user_data(mbox);

    LOGI(TAG, "CLICKED Label %s", lv_label_get_text(label));
    if (machine && strcmp(lv_label_get_text(label), "OK") == 0) {
        machine->home_all(machine);
    }

    lv_msgbox_close(mbox);
}

/**
 * @brief Creates and displays a modal dialog asking the user to home all axes.
 * @param machine A pointer to the machine interface, passed to the event handler.
 */
static void show_home_all_modal(machine_interface_t * machine) {
    static const char * btns[] = {"Ok", "Cancel", ""};
    lv_obj_t * mbox = lv_msgbox_create(lv_screen_active());
    lv_msgbox_add_title(mbox, "Home all?");
    lv_obj_set_user_data(mbox, machine); 
    lv_msgbox_add_text(mbox, "Home all axes?");
    lv_obj_t *home_btn = lv_msgbox_add_footer_button(mbox, "OK");
    lv_obj_add_event_cb(home_btn, home_all_modal_event_handler, LV_EVENT_CLICKED, mbox);
    lv_obj_t *close_btn = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(close_btn, home_all_modal_event_handler, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);
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
  } else if (strncmp(action_name, "action.motion.jog.axis_select_", 30) == 0) {
    const char* axis_char = action_name + 30;
    axis_t selected_axis = AXIS_OFF;
    int axis_idx = -1;
    if (*axis_char == 'x') { selected_axis = AXIS_X; axis_idx = 0; }
    else if (*axis_char == 'y') { selected_axis = AXIS_Y; axis_idx = 1; }
    else if (*axis_char == 'z') { selected_axis = AXIS_Z; axis_idx = 2; }
    
    if (axis_idx != -1) {
        if (!machine_interface_is_homed(machine, axis_char)) {
            show_home_all_modal(machine);
        } else {
            if (machine_interface_get_current_move_axis(machine) == selected_axis) {
                machine_interface_set_current_move_axis(machine, AXIS_OFF);
            } else {
                machine_interface_set_current_move_axis(machine, selected_axis);
            }
        }
        interface->dirty_flags |= UI_DIRTY_JOG_STATE;
    }
  } else if (strcmp(action_name, "action.motion.jog.axis_cycle") == 0) {
      machine_interface_next_move_axis(machine);
      interface->dirty_flags |= UI_DIRTY_JOG_STATE;
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
  else if ((strcmp(action_name, "action.overrides.feed.set_pct") == 0 ||
            strcmp(action_name, "set_feed_override") == 0) &&
           value.type == BINDING_TYPE_FLOAT) {
    char gcode[32];
    snprintf(gcode, sizeof(gcode), "M220 S%.0f", value.as.f_val);
    machine->send_gcode(machine, gcode, 0);
  } else if ((strcmp(action_name, "action.overrides.spindle.set_pct") == 0 ||
              strcmp(action_name, "set_speed_override") == 0) &&
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
  // Alternate naming from UI generation
  else if (strncmp(action_name, "probe_start_", 12) == 0) {
    const char *probe_type = action_name + 12;
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
