#include "ui_action_handler.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "debug.h"
#include "lvgl_ui.h"
#include "ui/mdi_handler.h"

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
void show_home_all_modal(machine_interface_t * machine) {
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

typedef struct {
  machine_interface_t *machine;
  char *name; // strdup'd filename
  bool is_macro; // true => macro, false => job
} execute_confirm_ctx_t;

static void execute_confirm_event_handler(lv_event_t * e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  lv_obj_t *mbox = lv_obj_get_parent(lv_obj_get_parent(btn));
  lv_obj_t *label = lv_obj_get_child(btn, 0);
  const char *lbl = lv_label_get_text(label);
  execute_confirm_ctx_t *ctx = (execute_confirm_ctx_t *)lv_obj_get_user_data(mbox);
  if (!ctx || !ctx->machine) {
    lv_msgbox_close(mbox);
    return;
  }
  if (strcmp(lbl, "OK") == 0 || strcmp(lbl, "Ok") == 0 || strcmp(lbl, "Yes") == 0) {
    if (ctx->is_macro) {
      ctx->machine->run_macro(ctx->machine, ctx->name);
    } else {
      ctx->machine->start_job(ctx->machine, ctx->name);
    }
  }
  lv_msgbox_close(mbox);
  free(ctx->name);
  free(ctx);
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

  // --- MDI Actions ---
  // Forward all "MDI.*" actions to the MDI handler.
  else if (strncmp(action_name, "MDI.", 4) == 0) {
    mdi_handle_action(&interface->mdi, action_name, value);
  }

  // --- File list actions: Jobs and Macros ---
  else if (strcmp(action_name, "Job.execute") == 0 || strcmp(action_name, "Macro.execute") == 0) {
    // value is a float index (0-based)
    if (value.type != BINDING_TYPE_FLOAT) return;
    int row = (int)(value.as.f_val + 0.001f);
    bool is_macro = (strcmp(action_name, "Macro.execute") == 0);

    // Find the matching filelist index
    int fl_idx = -1;
    for (int i = 0; i < MAX_FILE_LISTS; ++i) {
      const char *fdir = machine->filelists[i].fdir;
      if (!fdir) continue;
      if (is_macro) {
        if (strstr(fdir, "macro") || strstr(fdir, "macros")) { fl_idx = i; break; }
      } else {
        if (strstr(fdir, "gcode") || strstr(fdir, "gcodes")) { fl_idx = i; break; }
      }
    }
    if (fl_idx < 0) {
      LOGW(TAG, "No filelist for %s", is_macro ? "macros" : "gcodes");
      return;
    }

    char **files = machine->filelists[fl_idx].files;
    const char *fdir = machine->filelists[fl_idx].fdir ? machine->filelists[fl_idx].fdir : "";

    // Account for optional parent entry added by UI (inserted when deeper than root)
    bool add_parent = (strchr(fdir + 1, '/') != NULL);
    if (add_parent && row == 0) {
      // Parent selected — compute parent of fdir (strip after last '/')
      char parent[256];
      strncpy(parent, fdir, sizeof(parent) - 1);
      parent[sizeof(parent) - 1] = '\0';
      char *last = strrchr(parent, '/');
      if (last && last != parent) {
        *last = '\0';
      } else {
        // already at root — nothing to do
        return;
      }
      machine->list_files(machine, parent);
      return;
    }

    int file_idx = row - (add_parent ? 1 : 0);
    if (!files || file_idx < 0 || !files[file_idx]) return;

    const char *sel = files[file_idx];
    size_t L = strlen(sel);

    // If the selected entry itself encodes a parent (e.g. "<fullpath>/../"),
    // handle it by computing the parent from the selection string.
    if (L >= 3 && L >= 3 && strcmp(sel + (L - 3), "../") == 0) {
      // copy sel without trailing "/../"
      char tmp[256];
      size_t tlen = L - 3; // leave room for NUL
      if (tlen >= sizeof(tmp)) tlen = sizeof(tmp) - 1;
      memcpy(tmp, sel, tlen);
      tmp[tlen] = '\0';
      // strip trailing '/'
      if (tlen > 0 && tmp[tlen - 1] == '/') tmp[tlen - 1] = '\0';
      // compute parent of tmp
      char parent[256];
      strncpy(parent, tmp, sizeof(parent) - 1);
      parent[sizeof(parent) - 1] = '\0';
      char *last = strrchr(parent, '/');
      if (last && last != parent) {
        *last = '\0';
        machine->list_files(machine, parent);
      }
      return;
    }

    // Directory selection is indicated by trailing '/'
    bool is_dir = (L > 0 && sel[L - 1] == '/');

    if (is_dir) {
      // Build new path: join fdir and sel (strip trailing '/')
      char newpath[512];
      char name[256];
      strncpy(name, sel, sizeof(name) - 1);
      name[sizeof(name) - 1] = '\0';
      size_t nl = strlen(name);
      if (nl > 0 && name[nl - 1] == '/') name[nl - 1] = '\0';
      if (fdir[0] == '\0') snprintf(newpath, sizeof(newpath), "/%s", name);
      else snprintf(newpath, sizeof(newpath), "%s/%s", fdir, name);
      machine->list_files(machine, newpath);
      return;
    }

    // It's a file — show confirmation then execute
    const char *title = is_macro ? "Run Macro?" : "Run Job?";
    const char *text = sel;
    lv_obj_t *mbox = lv_msgbox_create(lv_screen_active());
    lv_msgbox_add_title(mbox, title);
    lv_msgbox_add_text(mbox, text);
    lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");
    lv_obj_t *cancel_btn = lv_msgbox_add_footer_button(mbox, "Cancel");

    execute_confirm_ctx_t *ctx = malloc(sizeof(execute_confirm_ctx_t));
    if (!ctx) { lv_msgbox_close(mbox); return; }
    ctx->machine = machine;
    ctx->is_macro = is_macro;
    ctx->name = strdup(sel);
    lv_obj_set_user_data(mbox, ctx);
    lv_obj_add_event_cb(ok_btn, execute_confirm_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(cancel_btn, execute_confirm_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_center(mbox);
    return;
  }

  // --- Titlebar ---
  else if (strcmp(action_name, "View.select") == 0) {
    // value is a float index (0-based)
    if (value.type != BINDING_TYPE_FLOAT) return;
    int tile = (int)(value.as.f_val + 0.001f);
    lv_obj_t* tileview = obj_registry_get("main_tileview");
    if (!tileview) { return; }
    lv_obj_set_tile_id(tileview, 0, tile, LV_ANIM_ON);
  }

  // --- Probe dimension settings (from the probe-mode tab numeric dialogs) ---
  // Each action receives the user-entered float value and:
  //   1. Notifies the bound observable so the label refreshes.
  //   2. (Future) Could persist the value here.
  else if (strcmp(action_name, "set_probe_width") == 0 &&
           value.type == BINDING_TYPE_FLOAT) {
    data_binding_notify_state_changed(
        "probe_w",
        (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = value.as.f_val});
  }
  else if (strcmp(action_name, "set_probe_height") == 0 &&
           value.type == BINDING_TYPE_FLOAT) {
    data_binding_notify_state_changed(
        "probe_h",
        (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = value.as.f_val});
  }
  else if (strcmp(action_name, "set_probe_cl") == 0 &&
           value.type == BINDING_TYPE_FLOAT) {
    data_binding_notify_state_changed(
        "probe_cl",
        (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = value.as.f_val});
  }
  else if (strcmp(action_name, "set_probe_ov") == 0 &&
           value.type == BINDING_TYPE_FLOAT) {
    data_binding_notify_state_changed(
        "probe_ov",
        (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = value.as.f_val});
  }
  else if (strcmp(action_name, "set_probe_quick") == 0) {
    // Boolean toggle from the quick-mode switch widget.
    bool quick = (value.type == BINDING_TYPE_BOOL) ? value.as.b_val
                                                    : (value.as.f_val != 0.0f);
    data_binding_notify_state_changed(
        "probe_quick",
        (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = quick});
  }

  // --- ask_home_all: programmatically triggered (e.g. from C callbacks) ---
  else if (strcmp(action_name, "action.ask_home_all") == 0) {
    show_home_all_modal(machine);
  }

  else {
    LOGW(TAG, "Unhandled action: %s", action_name);
  }
}

void ui_action_handler_init(interface_t *interface) {
  data_binding_register_action_handler(app_action_handler, interface);
}
