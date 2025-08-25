#include "ui/interface.h"
#include "lvgl_ui.h"
#include "ui_gen/ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// --- UI-Managed State ---
// This state is controlled by the UI logic itself.
static float jog_step_values[] = {0.01f, 0.1f, 1.0f, 5.0f, 10.0f, 25.0f, 50.0f};
static int jog_step_idx = 2; // Default to 1.0f

// --- Forward Declarations for Callbacks & Action Handler ---
static void ui_action_handler(const char* action_name, binding_value_t value, void* user_data);
static void on_machine_state_change(machine_interface_t* machine, void* user_data);
static void on_position_change(machine_interface_t* machine, void* user_data);
static void on_homed_change(machine_interface_t* machine, void* user_data);
static void on_wcs_change(machine_interface_t* machine, void* user_data);
static void on_feed_change(machine_interface_t* machine, void* user_data);
static void on_spindle_tool_change(machine_interface_t* machine, void* user_data);
static void on_sensors_change(machine_interface_t* machine, void* user_data);
static void on_dialogs_change(machine_interface_t* machine, void* user_data);
static void on_connected_change(machine_interface_t* machine, void* user_data);
static void on_current_move_axis_change(machine_interface_t* machine, void* user_data);
static void on_files_change(machine_interface_t* machine, void* user_data, const char* path, char** files);


// --- Action Handler ---
// This function is the central point for handling all actions triggered from the UI.
static void ui_action_handler(const char* action_name, binding_value_t value, void* user_data) {
    interface_t* interface = (interface_t*)user_data;
    machine_interface_t* machine = interface->machine;

    // --- Homing & Machine Control ---
    if (strcmp(action_name, "home_machine") == 0) { machine->home_all(machine); }
    else if (strcmp(action_name, "home_machine_x") == 0) { machine->home(machine, "X"); }
    else if (strcmp(action_name, "home_machine_y") == 0) { machine->home(machine, "Y"); }
    else if (strcmp(action_name, "home_machine_z") == 0) { machine->home(machine, "Z"); }
    else if (strcmp(action_name, "feed_hold") == 0) {
        if (machine->machine_status == MACHINE_STATUS_PAUSED) machine->send_gcode(machine, "~", JOB_STATUS); // Resume
        else machine->send_gcode(machine, "!", JOB_STATUS); // Feed Hold
    }
    else if (strcmp(action_name, "program_run") == 0) { machine->send_gcode(machine, "M24", JOB_STATUS); }
    else if (strcmp(action_name, "program_stop") == 0) { machine->send_gcode(machine, "\x18", JOB_STATUS); }

    // --- Overrides ---
    else if (strcmp(action_name, "set_feed_override") == 0 && value.type == BINDING_TYPE_FLOAT) {
        char gcode[32];
        snprintf(gcode, sizeof(gcode), "M220 S%.0f", value.as.f_val);
        machine->send_gcode(machine, gcode, 0);
    } else if (strcmp(action_name, "set_speed_override") == 0 && value.type == BINDING_TYPE_FLOAT) {
        char gcode[32];
        snprintf(gcode, sizeof(gcode), "M221 S%.0f", value.as.f_val);
        machine->send_gcode(machine, gcode, 0);
    }

    // --- Movement & Jogging ---
    else if (strcmp(action_name, "jog_continuous_start_x_plus") == 0) { machine->move_continuous(machine, 'X', machine->feed_req, 1); }
    else if (strcmp(action_name, "jog_continuous_start_x_minus") == 0) { machine->move_continuous(machine, 'X', machine->feed_req, -1); }
    else if (strcmp(action_name, "jog_continuous_start_y_plus") == 0) { machine->move_continuous(machine, 'Y', machine->feed_req, 1); }
    else if (strcmp(action_name, "jog_continuous_start_y_minus") == 0) { machine->move_continuous(machine, 'Y', machine->feed_req, -1); }
    else if (strcmp(action_name, "jog_continuous_start_z_plus") == 0) { machine->move_continuous(machine, 'Z', machine->feed_req, 1); }
    else if (strcmp(action_name, "jog_continuous_start_z_minus") == 0) { machine->move_continuous(machine, 'Z', machine->feed_req, -1); }
    else if (strcmp(action_name, "jog_continuous_stop") == 0) { machine->move_continuous_stop(machine); }
    else if (strcmp(action_name, "jog_step_x_plus") == 0) { machine->move(machine, 'X', machine->feed_req, machine->current_move_step); }
    else if (strcmp(action_name, "jog_step_x_minus") == 0) { machine->move(machine, 'X', machine->feed_req, -machine->current_move_step); }
    else if (strcmp(action_name, "jog_step_y_plus") == 0) { machine->move(machine, 'Y', machine->feed_req, machine->current_move_step); }
    else if (strcmp(action_name, "jog_step_y_minus") == 0) { machine->move(machine, 'Y', machine->feed_req, -machine->current_move_step); }
    else if (strcmp(action_name, "jog_step_z_plus") == 0) { machine->move(machine, 'Z', machine->feed_req, machine->current_move_step); }
    else if (strcmp(action_name, "jog_step_z_minus") == 0) { machine->move(machine, 'Z', machine->feed_req, -machine->current_move_step); }
    else if (strcmp(action_name, "cycle_jog_step") == 0) {
        jog_step_idx = (jog_step_idx + 1) % (sizeof(jog_step_values)/sizeof(float));
        machine->current_move_step = jog_step_values[jog_step_idx];
        data_binding_notify_state_changed("jog_step", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->current_move_step});
    }
    else if (strcmp(action_name, "set_move_axis_x") == 0) { machine_interface_set_current_move_axis(machine, AXIS_X); }
    else if (strcmp(action_name, "set_move_axis_y") == 0) { machine_interface_set_current_move_axis(machine, AXIS_Y); }
    else if (strcmp(action_name, "set_move_axis_z") == 0) { machine_interface_set_current_move_axis(machine, AXIS_Z); }
    else if (strcmp(action_name, "set_move_axis_off") == 0) { machine_interface_set_current_move_axis(machine, AXIS_OFF); }
    else if (strcmp(action_name, "cycle_move_axis") == 0) { machine_interface_next_move_axis(machine); }
    else if (strcmp(action_name, "jog_step_current_axis_plus") == 0) { machine_interface_step_current_axis(machine, machine->feed_req, 1); }
    else if (strcmp(action_name, "jog_step_current_axis_minus") == 0) { machine_interface_step_current_axis(machine, machine->feed_req, -1); }


    // --- Coordinate Systems ---
    else if (strcmp(action_name, "set_wcs") == 0 && value.type == BINDING_TYPE_FLOAT) { machine->set_wcs(machine, (int)value.as.f_val); }
    else if (strcmp(action_name, "cycle_wcs") == 0) { machine->next_wcs(machine); }
    else if (strcmp(action_name, "zero_wcs_x") == 0) { machine->set_wcs_zero(machine, machine->wcs, "X"); }
    else if (strcmp(action_name, "zero_wcs_y") == 0) { machine->set_wcs_zero(machine, machine->wcs, "Y"); }
    else if (strcmp(action_name, "zero_wcs_z") == 0) { machine->set_wcs_zero(machine, machine->wcs, "Z"); }

    // --- Files & Macros ---
    else if (strcmp(action_name, "list_files") == 0 && value.type == BINDING_TYPE_STRING) { machine->list_files(machine, value.as.s_val); }
    else if (strcmp(action_name, "run_macro") == 0 && value.type == BINDING_TYPE_STRING) { machine->run_macro(machine, value.as.s_val); }
    else if (strcmp(action_name, "start_job") == 0 && value.type == BINDING_TYPE_STRING) { machine->start_job(machine, value.as.s_val); }

    // --- Probing ---
    else if (strcmp(action_name, "probe") == 0 && value.type == BINDING_TYPE_STRING) { machine->probe(machine, value.as.s_val); }

    // --- Dialogs ---
    else if (strcmp(action_name, "dialog_ok") == 0) { if (machine->message_box) machine->modal_ok(machine, machine->message_box->seq); }
    else if (strcmp(action_name, "dialog_cancel") == 0) { if (machine->message_box) machine->modal_cancel(machine, machine->message_box->seq); }
    else if (strcmp(action_name, "dialog_choice") == 0 && value.type == BINDING_TYPE_FLOAT) { if (machine->message_box) machine->modal_choice(machine, (int)value.as.f_val, machine->message_box->seq); }
    else if (strcmp(action_name, "dialog_input_int") == 0 && value.type == BINDING_TYPE_FLOAT) { if (machine->message_box) machine->modal_int(machine, (int)value.as.f_val, machine->message_box->seq); }
    else if (strcmp(action_name, "dialog_input_float") == 0 && value.type == BINDING_TYPE_FLOAT) { if (machine->message_box) machine->modal_float(machine, value.as.f_val, machine->message_box->seq); }
    else if (strcmp(action_name, "dialog_input_str") == 0 && value.type == BINDING_TYPE_STRING) { if (machine->message_box) machine->modal_str(machine, value.as.s_val, machine->message_box->seq); }

    // --- Generic G-Code ---
    else if (strcmp(action_name, "send_gcode") == 0 && value.type == BINDING_TYPE_STRING) { machine->send_gcode(machine, value.as.s_val, 0); }
}

// --- Machine State -> UI Callbacks ---

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

static void on_machine_state_change(machine_interface_t* machine, void* user_data) {
    const char* mode_str = machine_status_to_mode_string(machine->machine_status);
    data_binding_notify_state_changed("machine_mode", (binding_value_t){.type = BINDING_TYPE_STRING, .as.s_val = mode_str});

    bool is_running = (machine->machine_status == MACHINE_STATUS_RUNNING || machine->machine_status == MACHINE_STATUS_SIMULATING);
    data_binding_notify_state_changed("program_running", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = is_running});

    bool is_paused = (machine->machine_status == MACHINE_STATUS_PAUSED ||
                      machine->machine_status == MACHINE_STATUS_PAUSED_DEC ||
                      machine->machine_status == MACHINE_STATUS_PAUSED_RESUME);
    data_binding_notify_state_changed("program_paused", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = is_paused});

    data_binding_notify_state_changed("move_is_relative", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = machine->move_relative});
    data_binding_notify_state_changed("move_is_step", (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = machine->move_step});
}

static void on_position_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("pos_x", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[0]});
    data_binding_notify_state_changed("pos_y", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[1]});
    data_binding_notify_state_changed("pos_z", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->position[2]});
    data_binding_notify_state_changed("wcs_pos_x", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->wcs_position[0]});
    data_binding_notify_state_changed("wcs_pos_y", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->wcs_position[1]});
    data_binding_notify_state_changed("wcs_pos_z", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->wcs_position[2]});
    data_binding_notify_state_changed("target_pos_x", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->target_position[0]});
    data_binding_notify_state_changed("target_pos_y", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->target_position[1]});
    data_binding_notify_state_changed("target_pos_z", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->target_position[2]});
}

static void on_homed_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("x_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[0]});
    data_binding_notify_state_changed("y_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[1]});
    data_binding_notify_state_changed("z_is_homed", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->axes_homed[2]});
}

static void on_wcs_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("wcs_number", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=(float)machine->wcs});
    data_binding_notify_state_changed("z_offset", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->z_offs});
    const char* wcs_str = machine_interface_get_wcs_str(machine, -1);
    data_binding_notify_state_changed("wcs_name", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=wcs_str});
}

static void on_feed_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("feed", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->feed});
    data_binding_notify_state_changed("feed_requested", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->feed_req});
    data_binding_notify_state_changed("feed_override", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->feed_multiplier * 100.0f});
}

static void on_spindle_tool_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("tool_name", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=(machine->tool ? machine->tool : "N/A")});
    for(size_t i = 0; i < machine->num_spindles; ++i) {
        char name_buf[32];
        snprintf(name_buf, sizeof(name_buf), "spindle_%zu_rpm", i);
        data_binding_notify_state_changed(name_buf, (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=(float)machine->spindles[i].rpm});
        snprintf(name_buf, sizeof(name_buf), "spindle_%zu_on", i);
        data_binding_notify_state_changed(name_buf, (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=(machine->spindles[i].rpm > 0)});
        snprintf(name_buf, sizeof(name_buf), "spindle_%zu_name", i);
        data_binding_notify_state_changed(name_buf, (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=machine->spindles[i].name});
    }
}

static void on_sensors_change(machine_interface_t* machine, void* user_data) {
    for(size_t i = 0; i < machine->num_probes; ++i) {
        char name_buf[32];
        snprintf(name_buf, sizeof(name_buf), "probe_%zu_value", i);
        data_binding_notify_state_changed(name_buf, (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->probes[i].value});
    }
    for(size_t i = 0; i < machine->num_end_stops; ++i) {
        char name_buf[32];
        snprintf(name_buf, sizeof(name_buf), "end_stop_%zu_triggered", i);
        data_binding_notify_state_changed(name_buf, (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->end_stops[i].triggered});
    }
}

static void on_dialogs_change(machine_interface_t* machine, void* user_data) {
    bool active = (machine->message_box != NULL);
    data_binding_notify_state_changed("dialog_active", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=active});
    if (active) {
        data_binding_notify_state_changed("dialog_title", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=machine->message_box->title});
        data_binding_notify_state_changed("dialog_text", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=machine->message_box->text});
        data_binding_notify_state_changed("dialog_mode", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=(float)machine->message_box->mode});
        // Serialize choices to a newline-separated string
        if (machine->message_box->num_choices > 0) {
            char choice_buf[256] = {0};
            for(size_t i = 0; i < machine->message_box->num_choices; ++i) {
                strncat(choice_buf, machine->message_box->choices[i], sizeof(choice_buf) - strlen(choice_buf) - 1);
                if (i < machine->message_box->num_choices - 1) {
                    strncat(choice_buf, "\n", sizeof(choice_buf) - strlen(choice_buf) - 1);
                }
            }
            data_binding_notify_state_changed("dialog_choices", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=choice_buf});
        }
    }
}

static void on_connected_change(machine_interface_t* machine, void* user_data) {
    data_binding_notify_state_changed("is_connected", (binding_value_t){.type=BINDING_TYPE_BOOL, .as.b_val=machine->is_connected(machine)});
}

static void on_current_move_axis_change(machine_interface_t* machine, void* user_data) {
    const char* axis_str = "OFF";
    switch(machine->current_move_axis) {
        case AXIS_X: axis_str = "X"; break;
        case AXIS_Y: axis_str = "Y"; break;
        case AXIS_Z: axis_str = "Z"; break;
        case AXIS_OFF: axis_str = "OFF"; break;
    }
    data_binding_notify_state_changed("current_move_axis", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=axis_str});
}

static void on_files_change(machine_interface_t* machine, void* user_data, const char* path, char** files) {
    char file_buf[1024] = {0}; // Buffer for newline-separated list
    if (files) {
        for(int i = 0; files[i] != NULL; ++i) {
            strncat(file_buf, files[i], sizeof(file_buf) - strlen(file_buf) - 1);
            if (files[i+1] != NULL) {
                strncat(file_buf, "\n", sizeof(file_buf) - strlen(file_buf) - 1);
            }
        }
    }
    
    if (strstr(path, "gcode")) {
        data_binding_notify_state_changed("filelist_gcodes", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=file_buf});
    } else if (strstr(path, "macro")) {
        data_binding_notify_state_changed("filelist_macros", (binding_value_t){.type=BINDING_TYPE_STRING, .as.s_val=file_buf});
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
    machine_interface_add_sensors_changed_cb(machine, interface, on_sensors_change);
    machine_interface_add_dialogs_changed_cb(machine, interface, on_dialogs_change);
    machine_interface_add_connected_changed_cb(machine, interface, on_connected_change);
    machine_interface_add_current_move_axis_changed_cb(machine, interface, on_current_move_axis_change);
    machine_interface_add_files_changed_cb(machine, "gcodes", interface, on_files_change);
    machine_interface_add_files_changed_cb(machine, "macros", interface, on_files_change);


    // Push the initial state to the UI once everything is set up
    on_machine_state_change(machine, interface);
    on_position_change(machine, interface);
    on_homed_change(machine, interface);
    on_wcs_change(machine, interface);
    on_feed_change(machine, interface);
    on_spindle_tool_change(machine, interface);
    on_sensors_change(machine, interface);
    on_dialogs_change(machine, interface);
    on_connected_change(machine, interface);
    on_current_move_axis_change(machine, interface);

    // Set initial UI-managed state
    machine->current_move_step = jog_step_values[jog_step_idx];
    data_binding_notify_state_changed("jog_step", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=machine->current_move_step});

}

void interface_tick(interface_t* interface) {
    // This tick is primarily for UI state that is not directly driven by the machine.
    // float time_s = lv_tick_get() / 1000.0f;
    // data_binding_notify_state_changed("time", (binding_value_t){.type=BINDING_TYPE_FLOAT, .as.f_val=time_s});
}
