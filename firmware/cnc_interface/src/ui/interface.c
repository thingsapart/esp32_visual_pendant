#include "ui/interface.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "lvgl_ui.h"
#include "lvgl.h"
#include "ui/ui_action_handler.h"
#include "ui/ui_setup_dwc.h"
#include "ui/components/mos_machine_handler.h"
#include "ui/components/lv_cam_positioning.h"
#include "ui/components/lv_cnc_io_panel.h"
#include "ui/components/lv_settings.h"
#include "ui/components/lv_gcode_viewer.h"
#include "machine/machine_interface.h"
#include "config/app_settings.h"

static const char *TAG = "UI_INTERFACE";

#ifdef DWC_MACHINE_MODE
// Global flags from main.c to indicate startup connection failure
extern bool g_dwc_startup_connection_failed;
extern char g_dwc_startup_host[65];
#endif

// --- RRF Machine Modal support ---

// Context passed to every msgbox button event callback.
typedef struct {
  machine_interface_t *machine;
  int seq;
  int button_index;  // Index for choice-mode buttons
} modal_btn_ctx_t;

// Handler for "OK / close" buttons on machine modals.
static void _modal_btn_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  modal_btn_ctx_t *ctx = (modal_btn_ctx_t *)lv_event_get_user_data(e);
  if (code == LV_EVENT_DELETE) {
    free(ctx);
    return;
  }
  if (code != LV_EVENT_CLICKED) return;
  if (!ctx || !ctx->machine) return;

  lv_obj_t *btn = lv_event_get_current_target(e);
  lv_obj_t *mbox = lv_obj_get_parent(lv_obj_get_parent(btn));  // btn -> footer -> mbox

  message_box_t *mb = ctx->machine->message_box;
  if (!mb || mb->seq != ctx->seq) {
    // Already dismissed or replaced - just close the LVGL object.
    lv_msgbox_close(mbox);
    return;
  }

  switch (mb->mode) {
    case MESSAGE_CHOICE:
      machine_interface_modal_choice(ctx->machine, ctx->button_index, ctx->seq);
      break;
    case MESSAGE_OK_CANCEL:
      if (ctx->button_index == 0)
        machine_interface_modal_ok(ctx->machine, ctx->seq);
      else
        machine_interface_modal_cancel(ctx->machine, ctx->seq);
      break;
    case MESSAGE_OK:
    case MESSAGE:
    default:
      machine_interface_modal_ok(ctx->machine, ctx->seq);
      break;
  }
  // machine_interface_modal_* implementations on the remote side will dismiss
  // the local modal (setting message_box = NULL) and send the command to hub.
  // Close the LVGL object explicitly here too in case it wasn't closed.
  lv_msgbox_close(mbox);
}

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
      (UI_DIRTY_FEEDRATE | UI_DIRTY_OVERRIDES | UI_DIRTY_CHIPLOAD);
}

static void on_spindle_tool_change(machine_interface_t *machine,
                                   void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= (UI_DIRTY_SPINDLE | UI_DIRTY_CHIPLOAD);
}

static void on_connected_change(machine_interface_t *machine, void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_CONNECTION;
}

static void on_sensors_change(machine_interface_t *machine, void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_IO_SENSORS;
}

static void on_current_move_axis_change(machine_interface_t *machine,
                                        void *user_data) {
  ((interface_t *)user_data)->dirty_flags |= UI_DIRTY_JOG_STATE;
}

static void on_files_change(machine_interface_t *machine, void *user_data,
                            const char *path, char **files) {
  interface_t *interface = (interface_t *)user_data;
  // Count incoming files for diagnostics
  int fcount = 0;
  if (files) { while (files[fcount]) fcount++; }
  LOGI(TAG, "on_files_change: path='%s' count=%d", path, fcount);
  if (strstr(path, "gcode")) {
    LOGI(TAG, "  -> marking UI_DIRTY_FILES_GCODES");
    interface->dirty_flags |= UI_DIRTY_FILES_GCODES;
  } else if (strstr(path, "macro")) {
    LOGI(TAG, "  -> marking UI_DIRTY_FILES_MACROS");
    interface->dirty_flags |= UI_DIRTY_FILES_MACROS;
  } else {
    LOGW(TAG, "  -> path '%s' matched neither 'gcode' nor 'macro'", path);
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
      return "TOOL CHG";
    case MACHINE_STATUS_IDLE:
      return "IDLE";
    case MACHINE_STATUS_INITIALIZING:
      return "INIT";
    case MACHINE_STATUS_EMERGENCY_HALTED:
      return "HALTED";
    case MACHINE_STATUS_OFF:
      return "OFF";
    case MACHINE_STATUS_WAITING_FOR_MACHINE:
      return "--X--";
    default:
      return "IDLE";
  }
}

/**
 * @brief Event handler for the main tileview to track the active tile.
 */
static void tileview_event_handler(lv_event_t * e) {
    lv_obj_t * tileview = lv_event_get_target(e);
    interface_t *iface = (interface_t *)lv_event_get_user_data(e);
    if (tileview) {
        lv_obj_t *o = lv_tileview_get_tile_active(tileview);
        float x = lv_obj_get_x(o), y = lv_obj_get_y(o);
        float col = round(x / (float) TFT_WIDTH), row = round(y / (float) TFT_HEIGHT);
        data_binding_notify_state_changed("ui.active_tile_index", (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = (float)col});
    }
}

/**
 * @brief Event handler for the disconnected overlay - dismiss on long-press.
 * Allows testing UI without connection by bypassing the connection screen.
 */
static void disconnected_overlay_event_handler(lv_event_t * e) {
    lv_obj_t * overlay = lv_event_get_target(e);
    if (overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) {
        LOGI(TAG, "Disconnected overlay dismissed by long-press");
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

// --- Toast bar (hub error/log messages) ---

// Forward declaration
static void _show_toast(interface_t *interface, const char *message);

typedef struct {
  interface_t *interface;
  lv_timer_t  *timer;
} toast_timer_ctx_t;

static void _toast_timer_cb(lv_timer_t *timer) {
  toast_timer_ctx_t *ctx = (toast_timer_ctx_t *)lv_timer_get_user_data(timer);
  if (ctx) {
    interface_t *iface = ctx->interface;
    if (iface && iface->toast_bar && lv_obj_is_valid(iface->toast_bar)) {
      lv_obj_del(iface->toast_bar);
      iface->toast_bar = NULL;
    }
    free(ctx);
  }
  lv_timer_delete(timer);
}

static void _toast_dismiss_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  interface_t *iface = (interface_t *)lv_event_get_user_data(e);
  if (iface && iface->toast_bar && lv_obj_is_valid(iface->toast_bar)) {
    lv_obj_del_async(iface->toast_bar);
    iface->toast_bar = NULL;
  }
}

static void _show_toast(interface_t *interface, const char *message) {
  // Dismiss any existing toast first.
  if (interface->toast_bar && lv_obj_is_valid(interface->toast_bar)) {
    lv_obj_del_async(interface->toast_bar);
    interface->toast_bar = NULL;
  }

  lv_obj_t *screen  = lv_screen_active();
  int32_t screen_w  = lv_obj_get_width(screen);
  const int32_t bar_h = 50;

  // Outer container pinned to the bottom of the screen.
  lv_obj_t *bar = lv_obj_create(screen);
  lv_obj_set_size(bar, screen_w, bar_h);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFD000), LV_PART_MAIN);  // amber – high-vis
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Message label – black text, scrolls if too long.
  lv_obj_t *lbl = lv_label_create(bar);
  lv_obj_set_style_text_color(lbl, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text(lbl, message);
  lv_obj_set_width(lbl, screen_w - 52);   // leave room for X button
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);

  // X dismiss button on the right.
  lv_obj_t *btn = lv_button_create(bar);
  lv_obj_set_size(btn, 40, 40);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xCC2200), LV_PART_MAIN);
  lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
  lv_obj_add_event_cb(btn, _toast_dismiss_cb, LV_EVENT_CLICKED, interface);
  lv_obj_t *x_lbl = lv_label_create(btn);
  lv_label_set_text(x_lbl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(x_lbl, lv_color_white(), LV_PART_MAIN);
  lv_obj_center(x_lbl);

  interface->toast_bar = bar;

  // Auto-dismiss after 5 seconds.
  toast_timer_ctx_t *ctx = (toast_timer_ctx_t *)malloc(sizeof(toast_timer_ctx_t));
  if (ctx) {
    ctx->interface = interface;
    lv_timer_t *t = lv_timer_create(_toast_timer_cb, 5000, ctx);
    ctx->timer = t;
    lv_timer_set_repeat_count(t, 1);
  }
}

// --- Log message callback (called from machine thread) ---

static bool on_log_message(machine_interface_t *machine, void *user_data,
                           const char *message) {
  interface_t *interface = (interface_t *)user_data;
  // Log verbatim to the pendant's serial output regardless.
  LOGI(TAG, "Hub msg: %s", message);

  // Let the MDI handler append to its log buffer and check for 'ok' acks.
  // This runs in the machine thread: mdi_handler_on_log() only sets flags.
  mdi_handler_on_log(&interface->mdi, message);

  // Suppress noisy "Error: Bad command:" lines from the toast bar — these are
  // echoed back by the machine when it doesn't recognise a polled M409 query
  // and do not represent actionable errors for the operator.
  if (strncmp(message, "Error: Bad command:", 19) == 0) {
    return true; // handled (suppress further UI toasts)
  }

  // If a prior handler already claimed this message, avoid showing the toast.
  if (machine && machine->last_log_message_handled) {
    LOGI(TAG, "Skipped log entry: %s", message);
    return false; // not handled by UI
  }

  // Store and request UI toast.
  snprintf(interface->log_message_buf, sizeof(interface->log_message_buf),
           "%s", message);
  interface->dirty_flags |= UI_DIRTY_LOG_MESSAGE;
  return true;
}

/**
 * @brief Long-press handler on the "all axes homed" LED in the title bar.
 * Opens the "Home all axes?" confirmation modal.
 */
static void homed_led_longpress_cb(lv_event_t *e) {
  machine_interface_t *machine = (machine_interface_t *)lv_event_get_user_data(e);
  if (machine) show_home_all_modal(machine);
}

// --- Public API ---

// callback used by lv_settings when any value changes; we only care about the
// material-type key so that the live UI can update the chipload immediately.
static void _settings_changed_cb(int group, int key, void *user_data) {
    if (group == APP_SETTINGS_GROUP_MATERIALS &&
        key == APP_SETTINGS_MATERIAL_TYPE && user_data) {
        interface_t *iface = (interface_t *)user_data;
        int m = app_settings_get_int(APP_SETTINGS_GROUP_MATERIALS,
                                     APP_SETTINGS_MATERIAL_TYPE);
        if (m < 0 || m >= TOOL_MATERIAL_COUNT)
            m = TOOL_MATERIAL_ALUMINIUM;
        interface_set_material(iface, (tool_material_t)m);
    }
}

void interface_init(interface_t *interface, machine_interface_t *machine) {
  interface->machine = machine;
  interface->dirty_flags = UI_DIRTY_ALL;  // Mark all as dirty for initial sync
  interface->current_msgbox = NULL;
  interface->log_message_buf[0] = '\0';
  interface->toast_bar = NULL;
  interface->current_material = TOOL_MATERIAL_ALUMINIUM;  // sensible default

  lvgl_ui_init();
  create_ui(lv_screen_active());

  /* After UI is created we can read material setting and apply it, and
   * register a callback so that changes made via the settings panel are
   * propagated live. */
  int mat = app_settings_get_int(APP_SETTINGS_GROUP_MATERIALS,
                                 APP_SETTINGS_MATERIAL_TYPE);
  if (mat < 0 || mat >= TOOL_MATERIAL_COUNT) mat = TOOL_MATERIAL_ALUMINIUM;
  interface_set_material(interface, (tool_material_t)mat);

  lv_obj_t *settings = obj_registry_get("settings_panel");
  if (settings) {
      lv_settings_set_changed_cb(settings, _settings_changed_cb, interface);
  }

  interface->probing_wizard = obj_registry_get("probing_wizard");
  if (!interface->probing_wizard) {
    LOGW(TAG, "Failed to find 'probing_wizard' widget in registry!");
  } else {
    lv_probing_wizard_register_mos_callbacks(interface->probing_wizard, interface->machine);
  }

  ui_action_handler_init(interface);

  // Initialise the MDI backend (allocates log buffer, finds widget IDs,
  // registers the LV_EVENT_READY handler on the input textarea).
  mdi_handler_init(&interface->mdi, machine);

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
  machine_interface_add_sensors_changed_cb(machine, interface,
                                            on_sensors_change);
  machine_interface_add_current_move_axis_changed_cb(
      machine, interface, on_current_move_axis_change);
  machine_interface_add_files_changed_cb(machine, "gcodes", interface,
                                         on_files_change);
  machine_interface_add_files_changed_cb(machine, "macros", interface,
                                         on_files_change);
  machine_interface_add_log_message_cb(machine, interface, on_log_message);

  // Find the main tileview and attach an event handler to track its state
  lv_obj_t* tileview = obj_registry_get("main_tileview");
  if (tileview) {
      lv_obj_add_event_cb(tileview, tileview_event_handler, LV_EVENT_VALUE_CHANGED, interface);
  }

  // Find the disconnected overlay and attach long-press handler to bypass connection screen
  lv_obj_t* disconnected_overlay = obj_registry_get("disconnected_overlay");
  if (disconnected_overlay) {
      lv_obj_add_event_cb(disconnected_overlay, disconnected_overlay_event_handler, LV_EVENT_LONG_PRESSED, NULL);
  }

  // Attach long-press handler to the "all axes homed" LED in the title bar
  // so the operator can quickly trigger a home-all confirmation.
  lv_obj_t* homed_led = obj_registry_get("homed_all_led");
  if (homed_led) {
      lv_obj_add_flag(homed_led, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(homed_led, homed_led_longpress_cb,
                          LV_EVENT_LONG_PRESSED, machine);
  }

  // Wire the machine's axis limits into the cam_positioning overlay so the
  // physical coordinate grid can be drawn.
  lv_obj_t* cam_pos = obj_registry_get("cam_positioning");
  if (cam_pos) {
      lv_cam_positioning_set_machine(cam_pos, machine);

      // Wire the MOS probe back-end so the wizard's Execute button can issue
      // G-code and receive results via the machine log / state callbacks.
      mos_probe_handler_init(&interface->probe_handler, machine);
      probe_api_callbacks_t probe_cbs;
      memset(&probe_cbs, 0, sizeof(probe_cbs));
      probe_api_ctx_t *probe_ctx = lv_cam_positioning_get_probe_ctx(cam_pos);
      mos_probe_handler_fill_callbacks(&interface->probe_handler, probe_ctx, &probe_cbs);
      lv_cam_positioning_set_probe_cbs(cam_pos, &probe_cbs);
  }

  // Set up gcode_viewer with access to machine_interface
  lv_obj_t* gc_view = obj_registry_get("gcode_viewer");
  if (gc_view) {
    lv_gcode_viewer_set_machine(gc_view, machine);
    lv_gcode_viewer_load_gcode(gc_view,
        "G21\nG90\nG1 X10 Y20 F600\nG1 X30\nG2 X10 Y20 I-10 J0\n");
    lv_gcode_viewer_set_view(gc_view, LV_GCVIEW_ISOMETRIC);
    lv_gcode_viewer_fit(gc_view);
  }

  // Set initial overlay text (will be overwritten by data bindings as state arrives)
  data_binding_notify_state_changed(
      "machine.overlay_status_text",
      (binding_value_t){.type = BINDING_TYPE_STRING,
                        .as.s_val = "Connecting..."});
  data_binding_notify_state_changed(
      "machine.connection_status",
      (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = false});

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
  // Flush MDI log-buffer and busy-state to data-binding observers every tick.
  mdi_handler_tick(&interface->mdi);

  // Lazy-resolve probing_wizard: the tile is deferred-loaded so the widget
  // may not exist at interface_init() time.  Re-try every tick until found.
  if (!interface->probing_wizard) {
    interface->probing_wizard = obj_registry_get("probing_wizard");
    if (interface->probing_wizard) {
      lv_probing_wizard_register_mos_callbacks(interface->probing_wizard,
                                               interface->machine);
    }
  }

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
    bool connected = machine->is_connected(machine);
    // When ESP-NOW link itself is down, show a "connecting to hub" message.
    if (!connected) {
      data_binding_notify_state_changed(
          "machine.overlay_status_text",
          (binding_value_t){.type = BINDING_TYPE_STRING,
                            .as.s_val = "Connecting to hub..."});
      // Status label also reflects the disconnected state.
      data_binding_notify_state_changed(
          "machine.status_text",
          (binding_value_t){.type = BINDING_TYPE_STRING,
                            .as.s_val = "-xxx-"});
    }
    data_binding_notify_state_changed(
        "machine.connection_status",
        (binding_value_t){.type = BINDING_TYPE_BOOL,
                          .as.b_val = connected});
    if (!connected) {
      const char *wait_msg = "< waiting for connection to list files... >";
      data_binding_notify_state_changed("Job.items",
        (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = wait_msg});
      data_binding_notify_state_changed("Macro.items",
        (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = wait_msg});
    }
  }

  if (flags_to_process & UI_DIRTY_MACHINE_STATE) {
    bool waiting = (machine->machine_status == MACHINE_STATUS_WAITING_FOR_MACHINE);
    if (waiting) {
      // Hub is reachable over ESP-NOW but the CNC controller isn't responding.
      // Show the overlay with a descriptive message instead of stale data.
      data_binding_notify_state_changed(
          "machine.overlay_status_text",
          (binding_value_t){.type = BINDING_TYPE_STRING,
                            .as.s_val = "Hub connected\nWaiting for\nmachine..."});
      data_binding_notify_state_changed(
          "machine.connection_status",
          (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = false});
    } else if (machine->is_connected(machine)) {
      // Machine is connected and sending real status: ensure overlay is gone.
      data_binding_notify_state_changed(
          "machine.connection_status",
          (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = true});
    }
    // Show "-xxx-" when the machine controller is unreachable (WAITING_FOR_MACHINE
    // or any state where the connection is considered down), so the label is never
    // misleadingly stuck at "IDLE" while actually disconnected.
    bool machine_reachable = machine->is_connected(machine) && !waiting;
    data_binding_notify_state_changed(
        "machine.status_text",
        (binding_value_t){.type = BINDING_TYPE_STRING,
                          .as.s_val = machine_reachable
                              ? machine_status_to_string(machine->machine_status)
                              : "--X-X--"});
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

    data_binding_notify_state_changed(
        "motion.homed.all", (binding_value_t){.type = BINDING_TYPE_BOOL,
                                            .as.b_val = machine->axes_homed[0] && machine->axes_homed[1] && machine->axes_homed[2]});                                        
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
    bool spindle_running = (rpm > 1e-5f || rpm < -1e-5f);  // CW or CCW
    data_binding_notify_state_changed(
        "spindle.speed_rpm",
        (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = rpm});
    data_binding_notify_state_changed(
        "spindle.is_on",
        (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = spindle_running});
    // "machine.spindle_is_running" mirrors spindle.is_on for convenience.
    data_binding_notify_state_changed(
        "machine.spindle_is_running",
        (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = spindle_running});
    // Tool info.
    data_binding_notify_state_changed(
        "tool.name",
        (binding_value_t){.type = BINDING_TYPE_STRING,
                          .as.s_val = machine->tool ? machine->tool : ""});
    data_binding_notify_state_changed(
        "tool.diameter_mm",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->tool_diameter_mm});
    data_binding_notify_state_changed(
        "tool.flute_count",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = (float)machine->tool_flute_count});
  }

  if (flags_to_process & UI_DIRTY_CHIPLOAD) {
    float cl = machine_interface_compute_chipload(machine);
    bool spindle_stopped = (cl == CHIPLOAD_SPINDLE_STOPPED);
    data_binding_notify_state_changed(
        "machine.spindle_is_running",
        (binding_value_t){.type = BINDING_TYPE_BOOL,
                          .as.b_val = !spindle_stopped});
    data_binding_notify_state_changed(
        "machine.chipload",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = spindle_stopped ? 0.0f : cl});
    float cl_rel = machine_interface_compute_chipload_relative(
        machine, interface->current_material);
    data_binding_notify_state_changed(
        "machine.chipload_relative",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = (cl_rel == CHIPLOAD_SPINDLE_STOPPED)
                                          ? 0.0f : cl_rel});
  }

  if (flags_to_process & UI_DIRTY_DIALOGS) {
    bool active = (machine->message_box != NULL);
    // Keep data-bindings in sync for any bound widgets in the YAML UI.
    data_binding_notify_state_changed(
        "dialog.is_active",
        (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = active});

    if (!active) {
      // Modal was dismissed on the machine side - close any open LVGL msgbox.
      if (interface->current_msgbox) {
        if (lv_obj_is_valid(interface->current_msgbox)) {
          lv_msgbox_close(interface->current_msgbox);
        }
        interface->current_msgbox = NULL;
      }
    } else {
      // A new (or updated) modal is present. Close the old one first if it
      // belongs to a different seq.
      if (interface->current_msgbox) {
        if (lv_obj_is_valid(interface->current_msgbox)) {
          // Retrieve the stored seq from user_data on the mbox.
          int old_seq = (int)(intptr_t)lv_obj_get_user_data(interface->current_msgbox);
          if (old_seq == machine->message_box->seq) {
            // Same dialog - already visible, nothing to do.
            goto after_dialogs;
          }
          lv_msgbox_close(interface->current_msgbox);
        }
        interface->current_msgbox = NULL;
      }

      message_box_t *mb = machine->message_box;
      const char *title = mb->title ? mb->title : "";
      const char *text  = mb->text  ? mb->text  : "";

      data_binding_notify_state_changed(
          "dialog.title", (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = title});
      data_binding_notify_state_changed(
          "dialog.text",
          (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = text});
      data_binding_notify_state_changed(
          "dialog.mode",
          (binding_value_t){.type = BINDING_TYPE_FLOAT, .as.f_val = (float)mb->mode});

      // Create the LVGL msgbox on the active screen.
      lv_obj_t *mbox = lv_msgbox_create(lv_screen_active());
      if (!mbox) {
        LOGE(TAG, "Failed to create msgbox for machine modal");
        goto after_dialogs;
      }

      // Store the seq in the mbox user_data for deduplication.
      lv_obj_set_user_data(mbox, (void *)(intptr_t)mb->seq);

      if (title[0]) lv_msgbox_add_title(mbox, title);
      if (text[0])  lv_msgbox_add_text(mbox, text);

      // Add buttons depending on mode.
      if (mb->mode == MESSAGE_CHOICE && mb->num_choices > 0 && mb->choices) {
        for (size_t i = 0; i < mb->num_choices; ++i) {
          lv_obj_t *btn = lv_msgbox_add_footer_button(
              mbox, mb->choices[i] ? mb->choices[i] : "?");
          modal_btn_ctx_t *ctx = (modal_btn_ctx_t *)malloc(sizeof(modal_btn_ctx_t));
          if (ctx) {
            ctx->machine = machine;
            ctx->seq = mb->seq;
            ctx->button_index = (int)i;
            lv_obj_add_event_cb(btn, _modal_btn_event_cb, LV_EVENT_ALL, ctx);
          }
        }
      } else if (mb->mode == MESSAGE_OK_CANCEL) {
        lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");
        modal_btn_ctx_t *ctx_ok = (modal_btn_ctx_t *)malloc(sizeof(modal_btn_ctx_t));
        if (ctx_ok) {
          ctx_ok->machine = machine; ctx_ok->seq = mb->seq; ctx_ok->button_index = 0;
          lv_obj_add_event_cb(ok_btn, _modal_btn_event_cb, LV_EVENT_ALL, ctx_ok);
        }
        lv_obj_t *ca_btn = lv_msgbox_add_footer_button(mbox, "Cancel");
        modal_btn_ctx_t *ctx_ca = (modal_btn_ctx_t *)malloc(sizeof(modal_btn_ctx_t));
        if (ctx_ca) {
          ctx_ca->machine = machine; ctx_ca->seq = mb->seq; ctx_ca->button_index = 1;
          lv_obj_add_event_cb(ca_btn, _modal_btn_event_cb, LV_EVENT_ALL, ctx_ca);
        }
      } else if (mb->mode == MESSAGE_INFO || mb->mode == MESSAGE) {
        // Non-blocking info - add a close button only.
        lv_obj_t *close_btn = lv_msgbox_add_footer_button(mbox, "OK");
        modal_btn_ctx_t *ctx_ok = (modal_btn_ctx_t *)malloc(sizeof(modal_btn_ctx_t));
        if (ctx_ok) {
          ctx_ok->machine = machine; ctx_ok->seq = mb->seq; ctx_ok->button_index = 0;
          lv_obj_add_event_cb(close_btn, _modal_btn_event_cb, LV_EVENT_ALL, ctx_ok);
        }
      } else {
        // MESSAGE_OK and blocking input types: show OK button.
        lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");
        modal_btn_ctx_t *ctx_ok = (modal_btn_ctx_t *)malloc(sizeof(modal_btn_ctx_t));
        if (ctx_ok) {
          ctx_ok->machine = machine; ctx_ok->seq = mb->seq; ctx_ok->button_index = 0;
          lv_obj_add_event_cb(ok_btn, _modal_btn_event_cb, LV_EVENT_ALL, ctx_ok);
        }
      }

      lv_obj_center(mbox);
      interface->current_msgbox = mbox;
      LOGI(TAG, "Showing machine modal (seq=%d mode=%d): %s / %s",
           mb->seq, mb->mode, title, text);
    }
    after_dialogs:;
  }

  if (flags_to_process & UI_DIRTY_LOG_MESSAGE) {
    if (interface->log_message_buf[0] != '\0') {
      _show_toast(interface, interface->log_message_buf);
    }
  }

  if (flags_to_process & UI_DIRTY_IO_SENSORS) {
    lv_obj_t *io_panel = obj_registry_get("io_panel");
    LOGI(TAG, "IO panel: %p", io_panel);
    if (io_panel && lv_obj_is_valid(io_panel)) {
      LOGI(TAG, "Refreshing IO panel: %p", io_panel);
      lv_cnc_io_panel_refresh(io_panel, interface);
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
    
    const char* axis_str = "Off";
    switch(current_axis) {
        case AXIS_X: axis_str = "X"; break;
        case AXIS_Y: axis_str = "Y"; break;
        case AXIS_Z: axis_str = "Z"; break;
        default: break;
    }
    data_binding_notify_state_changed(
        "motion.jog.axis_selected_str",
        (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = axis_str});

    data_binding_notify_state_changed(
        "motion.jog.step_xy",
        (binding_value_t){.type = BINDING_TYPE_FLOAT,
                          .as.f_val = machine->current_move_step_xy});
    data_binding_notify_state_changed(
        "motion.jog.step_z", (binding_value_t){.type = BINDING_TYPE_FLOAT,
                                               .as.f_val = machine->current_move_step_z});
  }

  // --- File lists: build newline-delimited strings for Job.items and Macro.items
  if (flags_to_process & UI_DIRTY_FILES_GCODES) {
    if (!machine->is_connected(machine)) {
      const char *wait_msg = "< waiting for connection to list files... >";
      data_binding_notify_state_changed("Job.items",
          (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = wait_msg});
      goto after_files_gcodes;
    }
    // Find the filelist entry that corresponds to gcodes
    for (int i = 0; i < MAX_FILE_LISTS; ++i) {
      const char *fdir = machine->filelists[i].fdir;
      char **files = machine->filelists[i].files;
      if (!fdir || !files) continue;
      if (!strstr(fdir, "gcode") && !strstr(fdir, "gcodes")) continue;

      // Compute whether we should add a parent-entry ("..")
      bool add_parent = false;
      const char *p = strchr(fdir + 1, '/');
      if (p) add_parent = true;  // deeper than root (e.g. /gcodes/sub)

      // Calculate total buffer size
      size_t total = 1; // final NUL
      if (add_parent) total += strlen(fdir) + 5; // "<fdir>/../\n"
      for (size_t j = 0; files[j]; ++j) {
        size_t L = strlen(files[j]);
        total += L + 1; // name + '\n'
      }

      char *buf = malloc(total);
      if (!buf) break;
      buf[0] = '\0';
      char *ptr = buf;
      if (add_parent) {
        size_t fl = strlen(fdir);
        memcpy(ptr, fdir, fl);
        ptr += fl;
        memcpy(ptr, "/../\n", 5);
        ptr += 5;
      }
      for (size_t j = 0; files[j]; ++j) {
        size_t L = strlen(files[j]);
        memcpy(ptr, files[j], L);
        ptr += L;
        *ptr++ = '\n';
      }
      if (ptr != buf) *(ptr - 1) = '\0'; // replace last newline with NUL

      LOGI(TAG, "Job.items notify: fdir='%s' buf='%.200s'", fdir, buf);
      data_binding_notify_state_changed("Job.items",
          (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = buf});
      free(buf);
      break;
    }
after_files_gcodes: ;
  }

  if (flags_to_process & UI_DIRTY_FILES_MACROS) {
    if (!machine->is_connected(machine)) {
      const char *wait_msg = "< waiting for connection to list files... >";
      data_binding_notify_state_changed("Macro.items",
          (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = wait_msg});
      goto after_files_macros;
    }
    for (int i = 0; i < MAX_FILE_LISTS; ++i) {
      const char *fdir = machine->filelists[i].fdir;
      char **files = machine->filelists[i].files;
      if (!fdir || !files) continue;
      if (!strstr(fdir, "macro") && !strstr(fdir, "macros")) continue;

      bool add_parent = false;
      const char *p = strchr(fdir + 1, '/');
      if (p) add_parent = true;

      size_t total = 1;
      if (add_parent) total += strlen(fdir) + 5;
      for (size_t j = 0; files[j]; ++j) {
        size_t L = strlen(files[j]);
        total += L + 1;
      }

      char *buf = malloc(total);
      if (!buf) break;
      buf[0] = '\0';
      char *ptr = buf;
      if (add_parent) {
        size_t fl = strlen(fdir);
        memcpy(ptr, fdir, fl);
        ptr += fl;
        memcpy(ptr, "/../\n", 5);
        ptr += 5;
      }
      for (size_t j = 0; files[j]; ++j) {
        size_t L = strlen(files[j]);
        memcpy(ptr, files[j], L);
        ptr += L;
        *ptr++ = '\n';
      }
      if (ptr != buf) *(ptr - 1) = '\0';

      LOGI(TAG, "Macro.items notify: fdir='%s' buf='%.200s'", fdir, buf);
      data_binding_notify_state_changed("Macro.items",
          (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = buf});
      free(buf);
      break;
    }
after_files_macros: ;
  }
}

void interface_set_material(interface_t *interface, tool_material_t material) {
  if (!interface) return;
  if ((unsigned)material >= TOOL_MATERIAL_COUNT) return;
  interface->current_material = material;
  // Immediately recompute and publish chipload with the new material.
  interface->dirty_flags |= UI_DIRTY_CHIPLOAD;
}
