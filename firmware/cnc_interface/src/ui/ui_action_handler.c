#define UI_DEBUG_LOCAL_LEVEL D_VERBOSE
#include "ui/ui_action_handler.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "lvgl_ui.h"  // For data_binding functions
#include "machine/machine_interface.h"
#include "ui/components/lv_probing_wizard.h"


static const char* TAG = "ui_action_handler";

// --- Forward Declarations ---
void on_dialogs_change(machine_interface_t* machine, void* user_data);

// --- Action Handler ---
// This function is the central point for handling all actions triggered from
// the UI.
static void ui_action_handler(const char* action_name, binding_value_t value,
                              void* user_data) {
  interface_t* interface = (interface_t*)user_data;
  machine_interface_t* machine = interface->machine;

  if (strncmp(action_name, "probe_start_", 12) == 0) {
      lv_obj_t* selection_view = obj_registry_get("probe_selection_view");
      lv_obj_t* wizard_container = obj_registry_get("probe_wizard_view_container");
      
      if (!selection_view || !wizard_container || !interface->probing_wizard) {
          return;
      }
      
      const char* type = action_name + 12;
      
      if (strcmp(type, "rect_pocket") == 0) {
          lv_probing_wizard_set_mode(interface->probing_wizard, LV_PROBING_WIZARD_MODE_RECTANGLE, true);
      } else if (strcmp(type, "rect_outside") == 0) {
          lv_probing_wizard_set_mode(interface->probing_wizard, LV_PROBING_WIZARD_MODE_RECTANGLE, false);
      } else if (strcmp(type, "circ_bore") == 0) {
          lv_probing_wizard_set_mode(interface->probing_wizard, LV_PROBING_WIZARD_MODE_CIRCLE, true);
      } else if (strcmp(type, "circ_boss") == 0) {
          lv_probing_wizard_set_mode(interface->probing_wizard, LV_PROBING_WIZARD_MODE_CIRCLE, false);
      } else if (strcmp(type, "corner_inside") == 0) {
          lv_probing_wizard_set_mode(interface->probing_wizard, LV_PROBING_WIZARD_MODE_CORNER, true);
      } else if (strcmp(type, "corner_outside") == 0) {
          lv_probing_wizard_set_mode(interface->probing_wizard, LV_PROBING_WIZARD_MODE_CORNER, false);
      } else {
          return; // Unknown probe type
      }

      lv_obj_add_flag(selection_view, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(wizard_container, LV_OBJ_FLAG_HIDDEN);
      
      return; // Action handled
  }


  LOGI(TAG, "action '%s'.", action_name);
  // --- Special Case: Homing Dialog Response ---
  // Check if a dialog action corresponds to our temporary homing dialog.
  bool is_homing_dialog =
      machine->message_box && machine->message_box->seq == 99;
  if (is_homing_dialog) {
    if (strcmp(action_name, "dialog_ok") == 0) {
      machine->home_all(machine);
    }
    // For both OK and Cancel, we must manually clear the UI-generated dialog.
    free_message_box_t(machine->message_box);
    machine->message_box = NULL;
    on_dialogs_change(machine,
                      interface);  // Trigger UI update to hide the dialog
    return;                        // Action is fully handled.
  }

  // --- Homing & Machine Control ---
  if (strcmp(action_name, "home_machine") == 0) {
    machine->home_all(machine);
  } else if (strcmp(action_name, "home_machine_x") == 0) {
    machine->home(machine, "X");
  } else if (strcmp(action_name, "home_machine_y") == 0) {
    machine->home(machine, "Y");
  } else if (strcmp(action_name, "home_machine_z") == 0) {
    machine->home(machine, "Z");
  } else if (strcmp(action_name, "feed_hold") == 0) {
    if (machine->machine_status == MACHINE_STATUS_PAUSED)
      machine->send_gcode(machine, "~", JOB_STATUS);  // Resume
    else
      machine->send_gcode(machine, "!", JOB_STATUS);  // Feed Hold
  } else if (strcmp(action_name, "program_run") == 0) {
    machine->send_gcode(machine, "M24", JOB_STATUS);
  } else if (strcmp(action_name, "program_stop") == 0) {
    machine->send_gcode(machine, "\x18", JOB_STATUS);
  }

  // --- Overrides ---
  else if (strcmp(action_name, "set_feed_override") == 0 &&
           value.type == BINDING_TYPE_FLOAT) {
    char gcode[32];
    snprintf(gcode, sizeof(gcode), "M220 S%.0f", value.as.f_val);
    machine->send_gcode(machine, gcode, 0);
  } else if (strcmp(action_name, "set_speed_override") == 0 &&
             value.type == BINDING_TYPE_FLOAT) {
    char gcode[32];
    snprintf(gcode, sizeof(gcode), "M221 S%.0f", value.as.f_val);
    machine->send_gcode(machine, gcode, 0);
  }

  // --- Active Axis Selection ---
  else if (strncmp(action_name, "set_active_", 11) == 0) {
    char axis_char = action_name[11];  // e.g., 'x' from "set_active_x"
    int axis_idx = machine_interface_axis_idx(machine, axis_char);

    if (axis_idx < 0) {
      LOGI(TAG, "action '%s' => invalid axis %c (%d)", action_name, axis_char,
           axis_idx);
      return;  // Invalid axis character
    }

    if (!machine->axes_homed[axis_idx]) {
      // Not homed: create a confirmation dialog.
      if (machine->message_box) {
        free_message_box_t(machine->message_box);  // Clear any existing dialog
      }
      machine->message_box = (message_box_t*)calloc(1, sizeof(message_box_t));
      machine->message_box->title = strdup("Axis Not Homed");
      char text_buf[128];
      snprintf(text_buf, sizeof(text_buf),
               "Axis %c is not homed. Home all axes now?", axis_char);
      machine->message_box->text = strdup(text_buf);
      machine->message_box->mode =
          MESSAGE_OK_CANCEL;  // "OK" maps to Yes, "Cancel" to No
      machine->message_box->seq =
          99;  // Use a special sequence to identify this dialog

      // Manually trigger the dialog update since this is a UI-initiated state
      // change
      on_dialogs_change(machine, interface);
    } else {
      // Homed: toggle the active move axis.
      axis_t current_axis = machine_interface_get_current_move_axis(machine);
      axis_t new_axis = (axis_t)(axis_idx);  // AXIS_X=0, AXIS_Y=1, etc.

      LOGI(TAG, "action '%s' => curr axis %d => next %d", action_name,
           current_axis, new_axis);
      if (current_axis == new_axis) {
        machine_interface_set_current_move_axis(machine, AXIS_OFF);
      } else {
        machine_interface_set_current_move_axis(machine, new_axis);
      }
    }
  }

  // --- Coordinate Systems ---
  else if (strcmp(action_name, "cycle_wcs") == 0) {
    machine->next_wcs(machine);
  } else if (strcmp(action_name, "zero_wcs_x") == 0) {
    machine->set_wcs_zero(machine, machine->wcs, "X");
  } else if (strcmp(action_name, "zero_wcs_y") == 0) {
    machine->set_wcs_zero(machine, machine->wcs, "Y");
  } else if (strcmp(action_name, "zero_wcs_z") == 0) {
    machine->set_wcs_zero(machine, machine->wcs, "Z");
  }

  // --- Generic G-Code ---
  else if (strcmp(action_name, "send_gcode") == 0 &&
           value.type == BINDING_TYPE_STRING) {
    machine->send_gcode(machine, value.as.s_val, 0);
  }
}

void ui_action_handler_init(interface_t* interface) {
  data_binding_register_action_handler(ui_action_handler, interface);
}
