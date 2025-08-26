#include "ui/interface.h"
#include "lvgl_ui.h"
#include "ui/ui_action_handler.h"
#include "ui/ui_setup_dwc.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef DWC_MACHINE_MODE
#include "config/dwc_settings.h"
// Global flags from main.cpp to indicate startup connection failure
extern bool g_dwc_startup_connection_failed;
extern char g_dwc_startup_host[65];
#endif


// --- Machine State -> UI Callbacks ---
// These callbacks are executed in the machine thread. They should only
// set a dirty flag to notify the UI thread that an update is needed.

static void on_machine_state_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_STATE;
}

void on_dialogs_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_DIALOGS;
}

static void on_position_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_POS;
}

static void on_homed_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_HOME;
}

static void on_wcs_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_WCS;
}

static void on_feed_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_FEED;
}

static void on_spindle_tool_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_SPINDLES_TOOLS;
}

static void on_sensors_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_SENSORS;
}

static void on_connected_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_CONNECTED;
}

static void on_current_move_axis_change(machine_interface_t* machine, void* user_data) {
    ((interface_t*)user_data)->dirty_flags |= UI_DIRTY_MOVE_AXIS;
}

static void on_files_change(machine_interface_t* machine, void* user_data, const char* path, char** files) {
    interface_t* interface = (interface_t*)user_data;
    if (strstr(path, "gcode")) {
        interface->dirty_flags |= UI_DIRTY_FILES_GCODES;
    } else if (strstr(path, "macro")) {
        interface->dirty_flags |= UI_DIRTY_FILES_MACROS;
    }
}

// --- Helper Functions ---
static const char* machine_status_to_mode_string(machine_status_t status) {
    switch (status) {
        case MACHINE_STATUS_RUNNING: return "AUTO";
        case MACHINE_STATUS_SIMULATING: return "SIMULATE";
        case MACHINE_STATUS_PAUSED:
        case MACHINE_STATUS_PAUSED_DEC:
        case MACHINE_STATUS_PAUSED_RESUME: return "PAUSED";
        case MACHINE_STATUS_TOOL_CHANGING: return "TOOL CHANGE";
        case MACHINE_STATUS_BUSY: return "BUSY";
        case MACHINE_STATUS_INITIALIZING: return "INIT";
        case MACHINE_STATUS_EMERGENCY_HALTED: return "HALTED";
        case MACHINE_STATUS_OFF: return "OFF";
        default: return "IDLE";
    }
}

// --- Public API ---

void interface_init(interface_t* interface, machine_interface_t* machine) {
    interface->machine = machine;
    interface->dirty_flags = 0xFFFFFFFF; // Mark all as dirty for initial update

    lvgl_ui_init();
    create_ui(lv_screen_active());

    ui_action_handler_init(interface);

    // Register callbacks to get state updates from the machine
    machine_interface_add_state_change_cb(machine, interface, on_machine_state_change);
    machine_interface_add_pos_changed_cb(machine, interface, on_position_change);
    machine_interface_add_home_changed_cb(machine, interface, on_homed_change);
    machine_interface_add_wcs_changed_cb(machine, interface, on_wcs_change);
    machine_interface_add_feed_changed_cb(machine, interface, on_feed_change);
    machine_interface_add_spindles_tools_changed_cb(machine, interface, on_spindle_tool_change);
    machine_interface_add_sensors_changed_cb(machine, interface, on_sensors_change);
    machine_interface_add_dialogs_changed_cb(machine, interface, on_dialogs_change);
    machine_interface_add_connected_changed_cb(machine, interface, on_connected_change);
    machine_interface_add_current_move_axis_changed_cb(machine, interface, on_current_move_axis_change);
    machine_interface_add_files_changed_cb(machine, "gcodes", interface, on_files_change);
    machine_interface_add_files_changed_cb(machine, "macros", interface, on_files_change);

#ifdef DWC_MACHINE_MODE
    // Check if we need to show the setup screen
    bool needs_config = !dwc_settings_are_valid() || g_dwc_startup_connection_failed;
    data_binding_notify_state_changed("dwc_needs_config", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=needs_config});
    if (needs_config) {
        memset(&interface->setup_settings, 0, sizeof(dwc_settings_t));

        ui_start_dwc_setup_flow(interface);

        if(g_dwc_startup_connection_failed) {
            char error_msg[128];
            snprintf(error_msg, sizeof(error_msg), "Failed to auto-connect to\n%s", g_dwc_startup_host);
            data_binding_notify_state_changed("dwc_startup_error", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=error_msg});
        }
    }
#endif
}

void interface_tick(interface_t* interface) {
    if (interface->dirty_flags == UI_DIRTY_NONE) {
        return;
    }

    uint32_t flags_to_process = interface->dirty_flags;
    interface->dirty_flags = UI_DIRTY_NONE;

    machine_interface_t* machine = interface->machine;

    if (flags_to_process & UI_DIRTY_STATE) {
        data_binding_notify_state_changed("machine_mode", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=machine_status_to_mode_string(machine->machine_status)});
        data_binding_notify_state_changed("program_running", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=(machine->machine_status == MACHINE_STATUS_RUNNING)});
        data_binding_notify_state_changed("program_paused", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=(machine->machine_status == MACHINE_STATUS_PAUSED)});
    }

    if (flags_to_process & UI_DIRTY_POS) {
        data_binding_notify_state_changed("display_pos_x", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[0]});
        data_binding_notify_state_changed("display_pos_y", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[1]});
        data_binding_notify_state_changed("display_pos_z", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[2]});
        data_binding_notify_state_changed("display_wcs_pos_x", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->wcs_position[0]});
        data_binding_notify_state_changed("display_wcs_pos_y", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->wcs_position[1]});
        data_binding_notify_state_changed("display_wcs_pos_z", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->wcs_position[2]});
    }

    if (flags_to_process & UI_DIRTY_HOME) {
        data_binding_notify_state_changed("x_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[0]});
        data_binding_notify_state_changed("y_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[1]});
        data_binding_notify_state_changed("z_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[2]});
    }
    
    if (flags_to_process & UI_DIRTY_WCS) {
        data_binding_notify_state_changed("wcs_name", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=machine_interface_get_wcs_str(machine, -1)});
    }

    if (flags_to_process & UI_DIRTY_FEED) {
        data_binding_notify_state_changed("feed", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->feed});
        data_binding_notify_state_changed("feed_override", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->feed_multiplier});
    }

    if (flags_to_process & UI_DIRTY_SPINDLES_TOOLS) {
        bool spindle_on = false;
        if (machine->num_spindles > 0) {
            data_binding_notify_state_changed("spindle_rpm", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=(float)machine->spindles[0].rpm});
            spindle_on = (machine->spindles[0].rpm != 0);
        }
        data_binding_notify_state_changed("spindle_on", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=spindle_on});
    }

    if (flags_to_process & UI_DIRTY_DIALOGS) {
        bool active = (machine->message_box != NULL);
        data_binding_notify_state_changed("dialog_active", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=active});
        if (active) {
            data_binding_notify_state_changed("dialog_title", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=machine->message_box->title});
            data_binding_notify_state_changed("dialog_text", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=machine->message_box->text});
            data_binding_notify_state_changed("dialog_mode", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=(float)machine->message_box->mode});
        }
    }

    if (flags_to_process & UI_DIRTY_CONNECTED) {
        data_binding_notify_state_changed("is_connected", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->is_connected(machine)});
    }
    
    if (flags_to_process & UI_DIRTY_MOVE_AXIS) {
        axis_t current_axis = machine_interface_get_current_move_axis(machine);
        data_binding_notify_state_changed("x_is_active", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = (current_axis == AXIS_X)});
        data_binding_notify_state_changed("y_is_active", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = (current_axis == AXIS_Y)});
        data_binding_notify_state_changed("z_is_active", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = (current_axis == AXIS_Z)});
    }

    if (flags_to_process & (UI_DIRTY_FILES_GCODES | UI_DIRTY_FILES_MACROS)) {
        // Not currently used by YAML, but kept for future use
    }
}
