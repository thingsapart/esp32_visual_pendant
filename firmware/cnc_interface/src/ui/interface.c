#include "ui/interface.h"
#include "lvgl_ui.h"
#include "ui_gen/ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// --- UI-Managed State ---
// This state is controlled by the UI logic itself, as defined in the YAML `actions`.
// It is not directly sourced from the machine.
static float jog_step_xy_values[] = {0.01f, 0.1f, 1.0f, 5.0f, 10.0f};
static int jog_step_xy_idx = 2; // Default to 1.0

static float jog_step_z_values[] = {0.01f, 0.1f, 1.0f, 5.0f};
static int jog_step_z_idx = 2; // Default to 1.0

// --- Forward Declarations for Callbacks & Action Handler ---
static void ui_action_handler(const char* action_name, binding_value_t value, void* user_data);
static void on_machine_state_change(machine_interface_t* machine, void* user_data);
static void on_position_change(machine_interface_t* machine, void* user_data);
static void on_homed_change(machine_interface_t* machine, void* user_data);
static void on_wcs_change(machine_interface_t* machine, void* user_data);
static void on_feed_change(machine_interface_t* machine, void* user_data);
static void on_spindle_tool_change(machine_interface_t* machine, void* user_data);

// --- Action Handler ---
// This function is the central point for handling all actions triggered from the UI.
static void ui_action_handler(const char* action_name, binding_value_t value, void* user_data) {
    interface_t* interface = (interface_t*)user_data;
    machine_interface_t* machine = interface->machine;

    // --- Machine Control Actions ---
    if (strcmp(action_name, "home_machine") == 0) {
        machine->home_all(machine);
        data_binding_notify_state_changed("machine_mode", (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = "HOMING"});
    } else if (strcmp(action_name, "home_machine_x") == 0) {
        machine->home(machine, "X");
        data_binding_notify_state_changed("machine_mode", (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = "HOMING"});
    } else if (strcmp(action_name, "home_machine_y") == 0) {
        machine->home(machine, "Y");
        data_binding_notify_state_changed("machine_mode", (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = "HOMING"});
    } else if (strcmp(action_name, "home_machine_z") == 0) {
        machine->home(machine, "Z");
        data_binding_notify_state_changed("machine_mode", (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = "HOMING"});
    } else if (strcmp(action_name, "feed_hold") == 0) {
        if (machine->machine_status == MACHINE_STATUS_PAUSED) {
            machine->send_gcode(machine, "~", JOB_STATUS); // Resume
        } else {
            machine->send_gcode(machine, "!", JOB_STATUS); // Feed Hold
        }
    } else if (strcmp(action_name, "program_run") == 0) {
        if (machine_interface_is_homed(machine, "XYZ")) {
            machine->send_gcode(machine, "M24", JOB_STATUS); // Start/Resume SD print
        }
    } else if (strcmp(action_name, "program_stop") == 0) {
        machine->send_gcode(machine, "\x18", JOB_STATUS); // Ctrl+X soft reset
    } else if (strcmp(action_name, "set_feed_override") == 0 && value.type == BINDING_TYPE_FLOAT) {
        char gcode[32];
        snprintf(gcode, sizeof(gcode), "M220 S%.0f", value.as.f_val);
        machine->send_gcode(machine, gcode, 0);
    } else if (strcmp(action_name, "set_speed_override") == 0 && value.type == BINDING_TYPE_FLOAT) {
        char gcode[32];
        snprintf(gcode, sizeof(gcode), "M221 S%.0f", value.as.f_val);
        machine->send_gcode(machine, gcode, 0);

    // --- UI State Management Actions ---
    } else if (strcmp(action_name, "cycle_wcs") == 0) {
        int next_wcs = (machine->wcs == 1) ? 2 : 1; // Cycle between G54 (1) and G55 (2)
        machine->set_wcs(machine, next_wcs);
    } else if (strcmp(action_name, "cycle_step_xy") == 0) {
        jog_step_xy_idx = (jog_step_xy_idx + 1) % (sizeof(jog_step_xy_values)/sizeof(float));
        float new_val = jog_step_xy_values[jog_step_xy_idx];
        data_binding_notify_state_changed("jog_step_xy", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=new_val});
    } else if (strcmp(action_name, "cycle_step_z") == 0) {
        jog_step_z_idx = (jog_step_z_idx + 1) % (sizeof(jog_step_z_values)/sizeof(float));
        float new_val = jog_step_z_values[jog_step_z_idx];
        data_binding_notify_state_changed("jog_step_z", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=new_val});
    } else if (strcmp(action_name, "zero_wcs_x") == 0) {
        machine->set_wcs_zero(machine, machine->wcs, "X");
        if (machine->wcs == 1) data_binding_notify_state_changed("wcs54_x_zero", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[0]});
        if (machine->wcs == 2) data_binding_notify_state_changed("wcs55_x_zero", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[0]});
    } else if (strcmp(action_name, "zero_wcs_y") == 0) {
        machine->set_wcs_zero(machine, machine->wcs, "Y");
        if (machine->wcs == 1) data_binding_notify_state_changed("wcs54_y_zero", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[1]});
        if (machine->wcs == 2) data_binding_notify_state_changed("wcs55_y_zero", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[1]});
    } else if (strcmp(action_name, "zero_wcs_z") == 0) {
        machine->set_wcs_zero(machine, machine->wcs, "Z");
        if (machine->wcs == 1) data_binding_notify_state_changed("wcs54_z_zero", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[2]});
        if (machine->wcs == 2) data_binding_notify_state_changed("wcs55_z_zero", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[2]});

    // --- Probe UI state setters ---
    } else if (strcmp(action_name, "set_probe_quick") == 0) {
        data_binding_notify_state_changed("probe_quick", value);
    } else if (strcmp(action_name, "set_probe_width") == 0) {
        data_binding_notify_state_changed("probe_w", value);
    } else if (strcmp(action_name, "set_probe_height") == 0) {
        data_binding_notify_state_changed("probe_h", value);
    } else if (strcmp(action_name, "set_probe_ov") == 0) {
        data_binding_notify_state_changed("probe_ov", value);
    } else if (strcmp(action_name, "set_probe_cl") == 0) {
        data_binding_notify_state_changed("probe_cl", value);
    }
}

// --- Machine State -> UI Callbacks ---

static const char* machine_status_to_mode_string(machine_status_t status) {
    switch (status) {
        case MACHINE_STATUS_RUNNING:
        case MACHINE_STATUS_SIMULATING:
            return "AUTO";
        case MACHINE_STATUS_PAUSED:
        case MACHINE_STATUS_PAUSED_DEC:
        case MACHINE_STATUS_PAUSED_RESUME:
            return "PAUSED";
        case MACHINE_STATUS_OFF:
        case MACHINE_STATUS_EMERGENCY_HALTED:
        case MACHINE_STATUS_TOOL_CHANGING:
        case MACHINE_STATUS_BUSY:
        case MACHINE_STATUS_INITIALIZING:
        default:
            return "IDLE";
    }
}

static void on_machine_state_change(machine_interface_t* machine, void* user_data) {
    const char* mode_str = machine_status_to_mode_string(machine->machine_status);
    data_binding_notify_state_changed("machine_mode", (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = mode_str});

    bool is_running = (machine->machine_status == MACHINE_STATUS_RUNNING || machine->machine_status == MACHINE_STATUS_SIMULATING);
    data_binding_notify_state_changed("program_running", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = is_running});

    bool is_paused = (machine->machine_status == MACHINE_STATUS_PAUSED ||
                      machine->machine_status == MACHINE_STATUS_PAUSED_DEC ||
                      machine->machine_status == MACHINE_STATUS_PAUSED_RESUME);
    data_binding_notify_state_changed("program_paused", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = is_paused});
}

static void on_position_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("pos_x", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[0]});
    data_binding_notify_state_changed("pos_y", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[1]});
    data_binding_notify_state_changed("pos_z", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[2]});
}

static void on_homed_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("x_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[0]});
    data_binding_notify_state_changed("y_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[1]});
    data_binding_notify_state_changed("z_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[2]});
}

static void on_wcs_change(machine_interface_t* machine, void* user_data) {
    char wcs_name[8];
    // This logic matches the RRF machine implementation for G54-G59.3
    if (machine->wcs >= 1 && machine->wcs <= 6) {
        snprintf(wcs_name, sizeof(wcs_name), "G%d", 53 + machine->wcs);
    } else if (machine->wcs >= 7 && machine->wcs <= 9) {
        snprintf(wcs_name, sizeof(wcs_name), "G59.%d", machine->wcs - 6);
    } else {
        strcpy(wcs_name, "G54"); // Default
    }
    data_binding_notify_state_changed("wcs_name", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=wcs_name});
}

static void on_feed_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("feed", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->feed});
    data_binding_notify_state_changed("feed_override", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->feed_multiplier});
}

static void on_spindle_tool_change(machine_interface_t* machine, void* user_data) {
    if (machine->num_spindles > 0 && machine->spindles) {
        data_binding_notify_state_changed("spindle_rpm", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=(float)machine->spindles[0].rpm});
        data_binding_notify_state_changed("spindle_on", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=(machine->spindles[0].rpm > 0)});
    } else {
        data_binding_notify_state_changed("spindle_rpm", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=0.0f});
        data_binding_notify_state_changed("spindle_on", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=false});
    }
}

// --- Public API ---

void interface_init(interface_t* interface, machine_interface_t* machine) {
    interface->machine = machine;

    lvgl_ui_init();
    create_ui(lv_screen_active());

    data_binding_register_action_handler(ui_action_handler, interface);

    // Register callbacks to get state updates from the machine
    machine_interface_add_state_change_cb(machine, interface, on_machine_state_change);
    machine_interface_add_pos_changed_cb(machine, interface, on_position_change);
    machine_interface_add_home_changed_cb(machine, interface, on_homed_change);
    machine_interface_add_wcs_changed_cb(machine, interface, on_wcs_change);
    machine_interface_add_feed_changed_cb(machine, interface, on_feed_change);
    machine_interface_add_spindles_tools_changed_cb(machine, interface, on_spindle_tool_change);

    // Push the initial state to the UI once everything is set up
    on_machine_state_change(machine, interface);
    on_position_change(machine, interface);
    on_homed_change(machine, interface);
    on_wcs_change(machine, interface);
    on_feed_change(machine, interface);
    on_spindle_tool_change(machine, interface);
}

void interface_tick(interface_t* interface) {
    // This tick is primarily for UI state that is not directly driven by the machine,
    // like the 'time' variable used for animations in the YAML.
    float time_s = lv_tick_get() / 1000.0f;
    //data_binding_notify_state_changed("time", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=time_s});
}
