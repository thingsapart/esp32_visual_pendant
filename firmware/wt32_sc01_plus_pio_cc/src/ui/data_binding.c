#include "ui/data_binding.h" // Changed from data_binding.h to ui/data_binding.h
#include <string.h>
#include <stdio.h> // For snprintf if used in format_str related functions
#include "esp_log.h" // For logging, if needed

#define MAX_GCODE_STR_LEN 128 // Maximum length for G-code strings

typedef struct {
    cnc_ui_viewmodel_t* vm; // Pointer to the main viewmodel
    data_action_t action_type; // Specific action this event data is for
} action_user_data_t;

// Define cnc_ui_viewmodel_t which will hold the machine interface and the registry
// This is the "derived" machine_interface_data_binding_registry_t mentioned in the issue
struct cnc_ui_viewmodel_t {
    machine_interface_t* machine;
    data_binding_registry_t registry;
    axis_t stored_jog_axis; // For ACTION_JOG_STORE_AXIS / ACTION_JOG_RESTORE_AXIS
    char current_probe_gcode[MAX_GCODE_STR_LEN]; // For ACTION_PROBE_START after ACTION_PROBE_MODE
    // Add any other necessary fields for the viewmodel
};

static const char *TAG = "data_binding"; // For logging

// Helper function to map data_binding_value_t to string for debugging or mapping
const char* data_binding_value_to_string(data_binding_value_t val) {
    switch (val) {
        case DATA_MACHINE_STATUS_TEXT: return "DATA_MACHINE_STATUS_TEXT";
        case DATA_IS_CONNECTED: return "DATA_IS_CONNECTED";
        case DATA_IS_HOMED_X: return "DATA_IS_HOMED_X";
        case DATA_IS_HOMED_Y: return "DATA_IS_HOMED_Y";
        case DATA_IS_HOMED_Z: return "DATA_IS_HOMED_Z";
        case DATA_IS_HOMED_ALL: return "DATA_IS_HOMED_ALL";
        case DATA_POS_MACHINE_X: return "DATA_POS_MACHINE_X";
        case DATA_POS_MACHINE_Y: return "DATA_POS_MACHINE_Y";
        case DATA_POS_MACHINE_Z: return "DATA_POS_MACHINE_Z";
        case DATA_POS_WORK_X: return "DATA_POS_WORK_X";
        case DATA_POS_WORK_Y: return "DATA_POS_WORK_Y";
        case DATA_POS_WORK_Z: return "DATA_POS_WORK_Z";
        case DATA_POS_DISTANCE_TO_GO_X: return "DATA_POS_DISTANCE_TO_GO_X";
        case DATA_POS_DISTANCE_TO_GO_Y: return "DATA_POS_DISTANCE_TO_GO_Y";
        case DATA_POS_DISTANCE_TO_GO_Z: return "DATA_POS_DISTANCE_TO_GO_Z";
        case DATA_FEED_CURRENT: return "DATA_FEED_CURRENT";
        case DATA_FEED_REQUESTED: return "DATA_FEED_REQUESTED";
        case DATA_FEED_OVERRIDE_PCT: return "DATA_FEED_OVERRIDE_PCT";
        case DATA_SPINDLE_SPEED_CURRENT: return "DATA_SPINDLE_SPEED_CURRENT";
        case DATA_SPINDLE_SPEED_REQUESTED: return "DATA_SPINDLE_SPEED_REQUESTED";
        case DATA_CURRENT_TOOL: return "DATA_CURRENT_TOOL";
        case DATA_JOG_CURRENT_AXIS: return "DATA_JOG_CURRENT_AXIS";
        case DATA_JOG_CURRENT_AXIS_TEXT: return "DATA_JOG_CURRENT_AXIS_TEXT";
        case DATA_JOG_CURRENT_STEP: return "DATA_JOG_CURRENT_STEP";
        case DATA_WCS_CURRENT_TEXT: return "DATA_WCS_CURRENT_TEXT";
        case DATA_WCS_CURRENT: return "DATA_WCS_CURRENT";
        case DATA_JOB_FILENAME: return "DATA_JOB_FILENAME";
        case DATA_JOB_PROGRESS: return "DATA_JOB_PROGRESS";
        case DATA_JOB_ELAPSED_TIME: return "DATA_JOB_ELAPSED_TIME";
        case DATA_JOB_REMAINING_TIME: return "DATA_JOB_REMAINING_TIME";
        case DATA_FILE_TEXT_LIST: return "DATA_FILE_TEXT_LIST";
        case DATA_MACRO_LIST: return "DATA_MACRO_LIST";
        case DATA_ENDSTOP_STATE_X: return "DATA_ENDSTOP_STATE_X";
        case DATA_ENDSTOP_STATE_Y: return "DATA_ENDSTOP_STATE_Y";
        case DATA_ENDSTOP_STATE_Z: return "DATA_ENDSTOP_STATE_Z";
        case DATA_ENDSTOP_STATE_A: return "DATA_ENDSTOP_STATE_A";
        case DATA_PROBE_1: return "DATA_PROBE_1";
        case DATA_PROBE_2: return "DATA_PROBE_2";
        case DATA_MODAL_DIALOG: return "DATA_MODAL_DIALOG";
        case _DATA_COUNT: return "_DATA_COUNT";
        default: return "UNKNOWN_DATA_BINDING_VALUE";
    }
}

// Helper function to map action_type_s to data_action_t
data_action_t map_string_to_action(const char* action_type_s) {
    if (strcmp(action_type_s, "HOME_ALL") == 0) return ACTION_HOME_ALL;
    if (strcmp(action_type_s, "HOME_AXIS_X") == 0) return ACTION_HOME_AXIS_X;
    if (strcmp(action_type_s, "HOME_AXIS_Y") == 0) return ACTION_HOME_AXIS_Y;
    if (strcmp(action_type_s, "HOME_AXIS_Z") == 0) return ACTION_HOME_AXIS_Z;
    if (strcmp(action_type_s, "HOME_AXIS_A") == 0) return ACTION_HOME_AXIS_A;
    if (strcmp(action_type_s, "CONNECT") == 0) return ACTION_CONNECT;
    if (strcmp(action_type_s, "DISCONNECT") == 0) return ACTION_DISCONNECT;
    if (strcmp(action_type_s, "EMERGENCY_STOP") == 0) return ACTION_EMERGENCY_STOP;
    if (strcmp(action_type_s, "RESET_ALARM") == 0) return ACTION_RESET_ALARM;
    if (strcmp(action_type_s, "JOB_START") == 0) return ACTION_JOB_START;
    if (strcmp(action_type_s, "JOB_PAUSE") == 0) return ACTION_JOB_PAUSE;
    if (strcmp(action_type_s, "JOB_RESUME") == 0) return ACTION_JOB_RESUME;
    if (strcmp(action_type_s, "JOB_STOP") == 0) return ACTION_JOB_STOP;
    if (strcmp(action_type_s, "MACRO_RUN") == 0) return ACTION_MACRO_RUN;
    if (strcmp(action_type_s, "JOG_CONTINUOUS_START_POS") == 0) return ACTION_JOG_CONTINUOUS_START_POS;
    if (strcmp(action_type_s, "JOG_CONTINUOUS_START_NEG") == 0) return ACTION_JOG_CONTINUOUS_START_NEG;
    if (strcmp(action_type_s, "JOG_CONTINUOUS_STOP") == 0) return ACTION_JOG_CONTINUOUS_STOP;
    if (strcmp(action_type_s, "JOG_STEP") == 0) return ACTION_JOG_STEP;
    if (strcmp(action_type_s, "JOG_SET_AXIS") == 0) return ACTION_JOG_SET_AXIS;
    if (strcmp(action_type_s, "JOG_SET_AXIS_X") == 0) return ACTION_JOG_SET_AXIS_X;
    if (strcmp(action_type_s, "JOG_SET_AXIS_Y") == 0) return ACTION_JOG_SET_AXIS_Y;
    if (strcmp(action_type_s, "JOG_SET_AXIS_Z") == 0) return ACTION_JOG_SET_AXIS_Z;
    if (strcmp(action_type_s, "JOG_SET_AXIS_A") == 0) return ACTION_JOG_SET_AXIS_A;
    if (strcmp(action_type_s, "JOG_STORE_AXIS") == 0) return ACTION_JOG_STORE_AXIS;
    if (strcmp(action_type_s, "JOG_RESTORE_AXIS") == 0) return ACTION_JOG_RESTORE_AXIS;
    if (strcmp(action_type_s, "JOG_SET_STEP") == 0) return ACTION_JOG_SET_STEP;
    if (strcmp(action_type_s, "WCS_SET") == 0) return ACTION_WCS_SET;
    if (strcmp(action_type_s, "WCS_NEXT") == 0) return ACTION_WCS_NEXT;
    if (strcmp(action_type_s, "WCS_ZERO_AXIS_X") == 0) return ACTION_WCS_ZERO_AXIS_X;
    if (strcmp(action_type_s, "WCS_ZERO_AXIS_Y") == 0) return ACTION_WCS_ZERO_AXIS_Y;
    if (strcmp(action_type_s, "WCS_ZERO_AXIS_Z") == 0) return ACTION_WCS_ZERO_AXIS_Z;
    if (strcmp(action_type_s, "WCS_ZERO_AXIS_A") == 0) return ACTION_WCS_ZERO_AXIS_A;
    if (strcmp(action_type_s, "WCS_ZERO_ALL") == 0) return ACTION_WCS_ZERO_ALL;
    if (strcmp(action_type_s, "SPINDLE_ON") == 0) return ACTION_SPINDLE_ON;
    if (strcmp(action_type_s, "SPINDLE_OFF") == 0) return ACTION_SPINDLE_OFF;
    if (strcmp(action_type_s, "SPINDLE_SET_SPEED") == 0) return ACTION_SPINDLE_SET_SPEED;
    if (strcmp(action_type_s, "FEED_OVERRIDE_SET") == 0) return ACTION_FEED_OVERRIDE_SET;
    if (strcmp(action_type_s, "FILES_LIST") == 0) return ACTION_FILES_LIST;
    if (strcmp(action_type_s, "MACROS_LIST") == 0) return ACTION_MACROS_LIST;
    if (strcmp(action_type_s, "PROBE_MODE") == 0) return ACTION_PROBE_MODE;
    if (strcmp(action_type_s, "PROBE_START") == 0) return ACTION_PROBE_START;
    if (strcmp(action_type_s, "MODAL_OK") == 0) return ACTION_MODAL_OK;
    if (strcmp(action_type_s, "MODAL_CANCEL") == 0) return ACTION_MODAL_CANCEL;
    if (strcmp(action_type_s, "MODAL_CHOICE") == 0) return ACTION_MODAL_CHOICE;
    if (strcmp(action_type_s, "MODAL_INPUT_INT") == 0) return ACTION_MODAL_INPUT_INT;
    if (strcmp(action_type_s, "MODAL_INPUT_FLOAT") == 0) return ACTION_MODAL_INPUT_FLOAT;
    if (strcmp(action_type_s, "MODAL_INPUT_STR") == 0) return ACTION_MODAL_INPUT_STR;
    return _ACTION_COUNT; // Invalid action
}

// Helper function to map data_type_s to data_binding_value_t
data_binding_value_t map_string_to_data_type(const char* data_type_s) {
    if (strcmp(data_type_s, "MACHINE_STATUS_TEXT") == 0) return DATA_MACHINE_STATUS_TEXT;
    if (strcmp(data_type_s, "IS_CONNECTED") == 0) return DATA_IS_CONNECTED;
    if (strcmp(data_type_s, "IS_HOMED_X") == 0) return DATA_IS_HOMED_X;
    if (strcmp(data_type_s, "IS_HOMED_Y") == 0) return DATA_IS_HOMED_Y;
    if (strcmp(data_type_s, "IS_HOMED_Z") == 0) return DATA_IS_HOMED_Z;
    if (strcmp(data_type_s, "IS_HOMED_ALL") == 0) return DATA_IS_HOMED_ALL;
    if (strcmp(data_type_s, "POS_MACHINE_X") == 0) return DATA_POS_MACHINE_X;
    if (strcmp(data_type_s, "POS_MACHINE_Y") == 0) return DATA_POS_MACHINE_Y;
    if (strcmp(data_type_s, "POS_MACHINE_Z") == 0) return DATA_POS_MACHINE_Z;
    if (strcmp(data_type_s, "POS_WORK_X") == 0) return DATA_POS_WORK_X;
    if (strcmp(data_type_s, "POS_WORK_Y") == 0) return DATA_POS_WORK_Y;
    if (strcmp(data_type_s, "POS_WORK_Z") == 0) return DATA_POS_WORK_Z;
    if (strcmp(data_type_s, "POS_DISTANCE_TO_GO_X") == 0) return DATA_POS_DISTANCE_TO_GO_X;
    if (strcmp(data_type_s, "POS_DISTANCE_TO_GO_Y") == 0) return DATA_POS_DISTANCE_TO_GO_Y;
    if (strcmp(data_type_s, "POS_DISTANCE_TO_GO_Z") == 0) return DATA_POS_DISTANCE_TO_GO_Z;
    if (strcmp(data_type_s, "FEED_CURRENT") == 0) return DATA_FEED_CURRENT;
    if (strcmp(data_type_s, "FEED_REQUESTED") == 0) return DATA_FEED_REQUESTED;
    if (strcmp(data_type_s, "FEED_OVERRIDE_PCT") == 0) return DATA_FEED_OVERRIDE_PCT;
    if (strcmp(data_type_s, "SPINDLE_SPEED_CURRENT") == 0) return DATA_SPINDLE_SPEED_CURRENT;
    if (strcmp(data_type_s, "SPINDLE_SPEED_REQUESTED") == 0) return DATA_SPINDLE_SPEED_REQUESTED;
    if (strcmp(data_type_s, "CURRENT_TOOL") == 0) return DATA_CURRENT_TOOL;
    if (strcmp(data_type_s, "JOG_CURRENT_AXIS") == 0) return DATA_JOG_CURRENT_AXIS;
    if (strcmp(data_type_s, "JOG_CURRENT_AXIS_TEXT") == 0) return DATA_JOG_CURRENT_AXIS_TEXT;
    if (strcmp(data_type_s, "JOG_CURRENT_STEP") == 0) return DATA_JOG_CURRENT_STEP;
    if (strcmp(data_type_s, "WCS_CURRENT_TEXT") == 0) return DATA_WCS_CURRENT_TEXT;
    if (strcmp(data_type_s, "WCS_CURRENT") == 0) return DATA_WCS_CURRENT;
    if (strcmp(data_type_s, "JOB_FILENAME") == 0) return DATA_JOB_FILENAME;
    if (strcmp(data_type_s, "JOB_PROGRESS") == 0) return DATA_JOB_PROGRESS;
    if (strcmp(data_type_s, "JOB_ELAPSED_TIME") == 0) return DATA_JOB_ELAPSED_TIME;
    if (strcmp(data_type_s, "JOB_REMAINING_TIME") == 0) return DATA_JOB_REMAINING_TIME;
    if (strcmp(data_type_s, "FILE_TEXT_LIST") == 0) return DATA_FILE_TEXT_LIST;
    if (strcmp(data_type_s, "MACRO_LIST") == 0) return DATA_MACRO_LIST;
    if (strcmp(data_type_s, "ENDSTOP_STATE_X") == 0) return DATA_ENDSTOP_STATE_X;
    if (strcmp(data_type_s, "ENDSTOP_STATE_Y") == 0) return DATA_ENDSTOP_STATE_Y;
    if (strcmp(data_type_s, "ENDSTOP_STATE_Z") == 0) return DATA_ENDSTOP_STATE_Z;
    if (strcmp(data_type_s, "ENDSTOP_STATE_A") == 0) return DATA_ENDSTOP_STATE_A;
    if (strcmp(data_type_s, "PROBE_1") == 0) return DATA_PROBE_1;
    if (strcmp(data_type_s, "PROBE_2") == 0) return DATA_PROBE_2;
    if (strcmp(data_type_s, "MODAL_DIALOG") == 0) return DATA_MODAL_DIALOG;

    if (strncmp(data_type_s, "DATA_", 5) == 0) {
        const char* actual_key = data_type_s + 5;
        if (strcmp(actual_key, "MACHINE_STATUS_TEXT") == 0) return DATA_MACHINE_STATUS_TEXT;
        if (strcmp(actual_key, "IS_CONNECTED") == 0) return DATA_IS_CONNECTED;
        if (strcmp(actual_key, "IS_HOMED_X") == 0) return DATA_IS_HOMED_X;
        if (strcmp(actual_key, "IS_HOMED_Y") == 0) return DATA_IS_HOMED_Y;
        if (strcmp(actual_key, "IS_HOMED_Z") == 0) return DATA_IS_HOMED_Z;
        if (strcmp(actual_key, "IS_HOMED_ALL") == 0) return DATA_IS_HOMED_ALL;
        if (strcmp(actual_key, "POS_MACHINE_X") == 0) return DATA_POS_MACHINE_X;
        if (strcmp(actual_key, "POS_MACHINE_Y") == 0) return DATA_POS_MACHINE_Y;
        if (strcmp(actual_key, "POS_MACHINE_Z") == 0) return DATA_POS_MACHINE_Z;
        if (strcmp(actual_key, "POS_WORK_X") == 0) return DATA_POS_WORK_X;
        if (strcmp(actual_key, "POS_WORK_Y") == 0) return DATA_POS_WORK_Y;
        if (strcmp(actual_key, "POS_WORK_Z") == 0) return DATA_POS_WORK_Z;
        if (strcmp(actual_key, "POS_DISTANCE_TO_GO_X") == 0) return DATA_POS_DISTANCE_TO_GO_X;
        if (strcmp(actual_key, "POS_DISTANCE_TO_GO_Y") == 0) return DATA_POS_DISTANCE_TO_GO_Y;
        if (strcmp(actual_key, "POS_DISTANCE_TO_GO_Z") == 0) return DATA_POS_DISTANCE_TO_GO_Z;
        if (strcmp(actual_key, "FEED_CURRENT") == 0) return DATA_FEED_CURRENT;
        if (strcmp(actual_key, "FEED_REQUESTED") == 0) return DATA_FEED_REQUESTED;
        if (strcmp(actual_key, "FEED_OVERRIDE_PCT") == 0) return DATA_FEED_OVERRIDE_PCT;
        if (strcmp(actual_key, "SPINDLE_SPEED_CURRENT") == 0) return DATA_SPINDLE_SPEED_CURRENT;
        if (strcmp(actual_key, "SPINDLE_SPEED_REQUESTED") == 0) return DATA_SPINDLE_SPEED_REQUESTED;
        if (strcmp(actual_key, "CURRENT_TOOL") == 0) return DATA_CURRENT_TOOL;
        if (strcmp(actual_key, "JOG_CURRENT_AXIS") == 0) return DATA_JOG_CURRENT_AXIS;
        if (strcmp(actual_key, "JOG_CURRENT_AXIS_TEXT") == 0) return DATA_JOG_CURRENT_AXIS_TEXT;
        if (strcmp(actual_key, "JOG_CURRENT_STEP") == 0) return DATA_JOG_CURRENT_STEP;
        if (strcmp(actual_key, "WCS_CURRENT_TEXT") == 0) return DATA_WCS_CURRENT_TEXT;
        if (strcmp(actual_key, "WCS_CURRENT") == 0) return DATA_WCS_CURRENT;
        if (strcmp(actual_key, "JOB_FILENAME") == 0) return DATA_JOB_FILENAME;
        if (strcmp(actual_key, "JOB_PROGRESS") == 0) return DATA_JOB_PROGRESS;
        if (strcmp(actual_key, "JOB_ELAPSED_TIME") == 0) return DATA_JOB_ELAPSED_TIME;
        if (strcmp(actual_key, "JOB_REMAINING_TIME") == 0) return DATA_JOB_REMAINING_TIME;
        if (strcmp(actual_key, "FILE_TEXT_LIST") == 0) return DATA_FILE_TEXT_LIST;
        if (strcmp(actual_key, "MACRO_LIST") == 0) return DATA_MACRO_LIST;
        if (strcmp(actual_key, "ENDSTOP_STATE_X") == 0) return DATA_ENDSTOP_STATE_X;
        if (strcmp(actual_key, "ENDSTOP_STATE_Y") == 0) return DATA_ENDSTOP_STATE_Y;
        if (strcmp(actual_key, "ENDSTOP_STATE_Z") == 0) return DATA_ENDSTOP_STATE_Z;
        if (strcmp(actual_key, "ENDSTOP_STATE_A") == 0) return DATA_ENDSTOP_STATE_A;
        if (strcmp(actual_key, "PROBE_1") == 0) return DATA_PROBE_1;
        if (strcmp(actual_key, "PROBE_2") == 0) return DATA_PROBE_2;
        if (strcmp(actual_key, "MODAL_DIALOG") == 0) return DATA_MODAL_DIALOG;
    }
    return _DATA_COUNT; // Invalid data type
}

// --- Forward declarations for callbacks ---
// static void generic_action_event_handler(lv_event_t * e); // Now defined below
static void machine_state_change_cb(machine_interface_t *machine, void *user_data);
// Add other machine interface callback forward declarations here as they are implemented

// Forward declarations for specific handlers
static void handle_home_all(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_home_axis(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_connect(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_emergency_stop(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_reset_alarm(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
// Job Execution
static void handle_job_start_stop_pause_resume(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_macro_run(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
// Jogging
static void handle_jog_continuous_start(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e);
static void handle_jog_continuous_stop(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e);
static void handle_jog_step(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e);
static void handle_jog_set_axis(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e);
static void handle_jog_store_restore_axis(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e);
static void handle_jog_set_step(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e);
// WCS
static void handle_wcs_set(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_wcs_next(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_wcs_zero_axis(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
// Spindle & Feedrate
static void handle_spindle_on_off(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_spindle_set_speed(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_feed_override_set(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
// Files & Probing
static void handle_files_macros_list(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_probe_mode(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e);
static void handle_probe_start(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e);
// Modal Responses
static void handle_modal_ok_cancel(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_modal_choice(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_modal_input_int_float(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);
static void handle_modal_input_str(machine_interface_t* machine, data_action_t action_type, lv_event_t *e);

static void generic_action_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    action_user_data_t* user_data = (action_user_data_t*)lv_event_get_user_data(e);

    if (!user_data || !user_data->vm || !user_data->vm->machine) {
        ESP_LOGE(TAG, "Action event user_data, vm, or machine is NULL.");
        return;
    }

    cnc_ui_viewmodel_t* vm = user_data->vm;
    machine_interface_t* machine = vm->machine;
    data_action_t action = user_data->action_type;

    ESP_LOGD(TAG, "Generic event: Action %d, LVGL Event %d", action, code);

    switch (action) {
        // Machine Control
        case ACTION_HOME_ALL: handle_home_all(machine, action, e); break;
        case ACTION_HOME_AXIS_X: case ACTION_HOME_AXIS_Y: case ACTION_HOME_AXIS_Z: case ACTION_HOME_AXIS_A:
            handle_home_axis(machine, action, e); break;
        case ACTION_CONNECT: case ACTION_DISCONNECT: handle_connect(machine, action, e); break;
        case ACTION_EMERGENCY_STOP: handle_emergency_stop(machine, action, e); break;
        case ACTION_RESET_ALARM: handle_reset_alarm(machine, action, e); break;
        // Job Execution
        case ACTION_JOB_START: case ACTION_JOB_PAUSE: case ACTION_JOB_RESUME: case ACTION_JOB_STOP:
            handle_job_start_stop_pause_resume(machine, action, e); break;
        case ACTION_MACRO_RUN: handle_macro_run(machine, action, e); break;
        // Manual Movement (Jogging)
        case ACTION_JOG_CONTINUOUS_START_POS: case ACTION_JOG_CONTINUOUS_START_NEG:
            handle_jog_continuous_start(vm, action, e); break;
        case ACTION_JOG_CONTINUOUS_STOP: handle_jog_continuous_stop(vm, action, e); break;
        case ACTION_JOG_STEP: handle_jog_step(vm, action, e); break;
        case ACTION_JOG_SET_AXIS: case ACTION_JOG_SET_AXIS_X: case ACTION_JOG_SET_AXIS_Y: case ACTION_JOG_SET_AXIS_Z: case ACTION_JOG_SET_AXIS_A:
            handle_jog_set_axis(vm, action, e); break;
        case ACTION_JOG_STORE_AXIS: case ACTION_JOG_RESTORE_AXIS:
            handle_jog_store_restore_axis(vm, action, e); break;
        case ACTION_JOG_SET_STEP: handle_jog_set_step(vm, action, e); break;
        // Coordinate Systems & Offsets
        case ACTION_WCS_SET: handle_wcs_set(machine, action, e); break;
        case ACTION_WCS_NEXT: handle_wcs_next(machine, action, e); break;
        case ACTION_WCS_ZERO_AXIS_X: case ACTION_WCS_ZERO_AXIS_Y: case ACTION_WCS_ZERO_AXIS_Z: case ACTION_WCS_ZERO_AXIS_A: case ACTION_WCS_ZERO_ALL:
            handle_wcs_zero_axis(machine, action, e); break;
        // Spindle & Feedrate
        case ACTION_SPINDLE_ON: case ACTION_SPINDLE_OFF: handle_spindle_on_off(machine, action, e); break;
        case ACTION_SPINDLE_SET_SPEED: handle_spindle_set_speed(machine, action, e); break;
        case ACTION_FEED_OVERRIDE_SET: handle_feed_override_set(machine, action, e); break;
        // Files & Probing
        case ACTION_FILES_LIST: case ACTION_MACROS_LIST: handle_files_macros_list(machine, action, e); break;
        case ACTION_PROBE_MODE: handle_probe_mode(vm, action, e); break;
        case ACTION_PROBE_START: handle_probe_start(vm, action, e); break;
        // Dialog/Modal Responses
        case ACTION_MODAL_OK: case ACTION_MODAL_CANCEL: handle_modal_ok_cancel(machine, action, e); break;
        case ACTION_MODAL_CHOICE: handle_modal_choice(machine, action, e); break;
        case ACTION_MODAL_INPUT_INT: case ACTION_MODAL_INPUT_FLOAT: handle_modal_input_int_float(machine, action, e); break;
        case ACTION_MODAL_INPUT_STR: handle_modal_input_str(machine, action, e); break;
        default: ESP_LOGW(TAG, "Unhandled action type: %d in generic_action_event_handler", action); break;
    }
}

// --- Specific Action Handler Implementations ---
// (All specific handlers from previous steps will be inserted here by the next diff block)


// Example predefined probe G-code commands (could be in config or elsewhere)
const char* predefined_probe_commands[] = {
    "G38.2 Z-20 F100", // Probe Z downwards, example 0
    "G38.2 X-20 F100", // Probe X negative, example 1
    "G38.2 Y20 F100"   // Probe Y positive, example 2
};
const int num_predefined_probe_commands = sizeof(predefined_probe_commands) / sizeof(predefined_probe_commands[0]);

// Helper to convert axis_t to char
static char axis_to_char(axis_t axis) {
    switch (axis) {
        case AXIS_X: return 'X';
        case AXIS_Y: return 'Y';
        case AXIS_Z: return 'Z';
        default: return '?';
    }
}

cnc_ui_viewmodel_t* cnc_ui_viewmodel_create(machine_interface_t* machine) {
    if (!machine) {
        ESP_LOGE(TAG, "Machine interface cannot be NULL.");
        return NULL;
    }
    cnc_ui_viewmodel_t* vm = (cnc_ui_viewmodel_t*)malloc(sizeof(cnc_ui_viewmodel_t));
    if (!vm) {
        ESP_LOGE(TAG, "Failed to allocate memory for cnc_ui_viewmodel_t.");
        return NULL;
    }
    memset(vm, 0, sizeof(cnc_ui_viewmodel_t));
    vm->machine = machine;

    // Initialize display_registry
    for (int i = 0; i < _DATA_COUNT; i++) {
        vm->registry.display_registry[i].value = (data_binding_value_t)i;
        vm->registry.display_registry[i].count = 0;
    }

    // Initialize action_handlers to point to the generic handler
    for (int i = 0; i < _ACTION_COUNT; i++) {
        vm->registry.action_handlers[i] = generic_action_event_handler;
    }

    vm->stored_jog_axis = AXIS_OFF; // Initialize stored_jog_axis
    strncpy(vm->current_probe_gcode, predefined_probe_commands[0], MAX_GCODE_STR_LEN -1 ); // Default probe
    vm->current_probe_gcode[MAX_GCODE_STR_LEN -1] = '\0';

    ESP_LOGI(TAG, "CNC UI ViewModel created. Generic action handler assigned.");
    return vm;
}

void cnc_ui_viewmodel_destroy(cnc_ui_viewmodel_t* vm) {
    if (!vm) {
        return;
    }
    free(vm);
    ESP_LOGI(TAG, "CNC UI ViewModel destroyed.");
}

// --- Specific action handler implementations ---

static void handle_home_all(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Action: HOME_ALL");
        if (machine && machine->home_all) {
            machine->home_all(machine);
        } else {
            ESP_LOGE(TAG, "home_all not available for machine %p", machine);
        }
    }
}

static void handle_home_axis(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        const char* axis_str = NULL;
        switch (action_type) {
            case ACTION_HOME_AXIS_X: axis_str = "X"; break;
            case ACTION_HOME_AXIS_Y: axis_str = "Y"; break;
            case ACTION_HOME_AXIS_Z: axis_str = "Z"; break;
            case ACTION_HOME_AXIS_A: axis_str = "A"; break;
            default: ESP_LOGE(TAG, "Invalid action type %d in handle_home_axis", action_type); return;
        }
        ESP_LOGI(TAG, "Action: HOME_AXIS %s", axis_str);
        if (machine && machine->home) {
            machine->home(machine, axis_str);
        } else {
            ESP_LOGE(TAG, "home_axis not available for machine %p axis %s", machine, axis_str);
        }
    }
}

static void handle_connect(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        bool do_connect = (action_type == ACTION_CONNECT);
        ESP_LOGI(TAG, "Action: %s", do_connect ? "CONNECT" : "DISCONNECT");
        if (machine && machine->set_connected) {
            machine->set_connected(machine, do_connect);
        } else {
            ESP_LOGE(TAG, "set_connected not available for machine %p", machine);
        }
    }
}

static void handle_emergency_stop(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Action: EMERGENCY_STOP");
        if (machine && machine->send_gcode) {
            machine->send_gcode(machine, "!", 0);
        } else {
            ESP_LOGE(TAG, "send_gcode for emergency_stop not available for machine %p", machine);
        }
    }
}

static void handle_reset_alarm(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Action: RESET_ALARM");
        if (machine && machine->send_gcode) {
            machine->send_gcode(machine, "$X", 0);
        } else {
            ESP_LOGE(TAG, "send_gcode for reset_alarm not available for machine %p", machine);
        }
    }
}

static void handle_job_start_stop_pause_resume(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->send_gcode || !machine->start_job) {
            ESP_LOGE(TAG, "Machine or required functions (send_gcode, start_job) not available for job action %d.", action_type);
            return;
        }
        switch (action_type) {
            case ACTION_JOB_START: {
                const char* job_file = "selected_job.gcode";
                ESP_LOGI(TAG, "Action: JOB_START, File: %s", job_file);
                machine->start_job(machine, job_file);
                break;
            }
            case ACTION_JOB_PAUSE: ESP_LOGI(TAG, "Action: JOB_PAUSE"); machine->send_gcode(machine, "!", 0); break;
            case ACTION_JOB_RESUME: ESP_LOGI(TAG, "Action: JOB_RESUME"); machine->send_gcode(machine, "~", 0); break;
            case ACTION_JOB_STOP: ESP_LOGI(TAG, "Action: JOB_STOP"); machine->send_gcode(machine, "", 0); break;
            default: ESP_LOGW(TAG, "Unhandled action in handle_job_start_stop_pause_resume: %d", action_type); break;
        }
    }
}

static void handle_macro_run(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->run_macro) {
            ESP_LOGE(TAG, "Machine or run_macro function not available for ACTION_MACRO_RUN.");
            return;
        }
        int macro_to_run_idx = 0; // Placeholder
        ESP_LOGI(TAG, "Action: MACRO_RUN, Index: %d (Placeholder value used)", macro_to_run_idx);
        char macro_filename[64];
        snprintf(macro_filename, sizeof(macro_filename), "run_macro_%d.gcode", macro_to_run_idx);
        machine->run_macro(machine, macro_filename);
    } else {
        ESP_LOGD(TAG, "ACTION_MACRO_RUN triggered by event %d, not CLICKED/VALUE_CHANGED. Ignoring.", code);
    }
}

static void handle_jog_continuous_start(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        machine_interface_t* machine = vm->machine;
        if (!machine || !machine->move_continuous) { ESP_LOGE(TAG, "Machine or move_continuous not available."); return; }
        int direction = (action_type == ACTION_JOG_CONTINUOUS_START_POS) ? 1 : -1;
        axis_t current_axis = machine_interface_get_current_move_axis(machine);
        char axis_char = axis_to_char(current_axis);
        if (axis_char == '?') { ESP_LOGE(TAG, "Invalid current jog axis: %d", current_axis); return; }
        float jog_feed = machine->feed > 0 ? machine->feed : 1000.0f;
        ESP_LOGI(TAG, "Action: JOG_CONTINUOUS_START, Axis: %c, Dir: %d, Feed: %.2f", axis_char, direction, jog_feed);
        machine->move_continuous(machine, axis_char, jog_feed, direction);
    }
}

static void handle_jog_continuous_stop(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        machine_interface_t* machine = vm->machine;
        if (!machine || !machine->move_continuous_stop) { ESP_LOGE(TAG, "Machine or move_continuous_stop not available."); return; }
        ESP_LOGI(TAG, "Action: JOG_CONTINUOUS_STOP");
        machine->move_continuous_stop(machine);
    }
}

static void handle_jog_step(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        machine_interface_t* machine = vm->machine;
        if (!machine || !machine->move) { ESP_LOGE(TAG, "Machine or move function not available for jog step."); return; }
        axis_t current_axis = machine_interface_get_current_move_axis(machine);
        char axis_char = axis_to_char(current_axis);
        float step_dist = machine->current_move_step;
        if (axis_char == '?') { ESP_LOGE(TAG, "Invalid current jog axis for step: %d", current_axis); return; }
        if (step_dist <= 0) { ESP_LOGW(TAG, "Jog step distance is zero or negative (%.3f).", step_dist); return; }
        int direction = 1; // Placeholder
        float move_value = (float)direction * step_dist;
        float jog_feed = machine->feed > 0 ? machine->feed : 1000.0f;
        ESP_LOGI(TAG, "Action: JOG_STEP, Axis: %c, StepValue: %.4f, Feed: %.2f", axis_char, move_value, jog_feed);
        machine->move(machine, axis_char, jog_feed, move_value);
    }
}

static void handle_jog_set_axis(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e) {
    machine_interface_t* machine = vm->machine;
    if (!machine) { ESP_LOGE(TAG, "Machine not available for JOG_SET_AXIS."); return; }
    axis_t new_axis = machine_interface_get_current_move_axis(machine);
    lv_event_code_t code = lv_event_get_code(e);
    if (action_type == ACTION_JOG_SET_AXIS) {
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_obj_t* target = lv_event_get_target(e); int axis_val = 0;
            if (lv_obj_is_valid(target) && lv_obj_check_type(target, &lv_dropdown_class)) { axis_val = lv_dropdown_get_selected(target); }
            else { ESP_LOGW(TAG, "ACTION_JOG_SET_AXIS from unhandled widget type"); return; }
            if (axis_val >= AXIS_X && axis_val < AXIS_OFF) { new_axis = (axis_t)axis_val; }
            else { ESP_LOGW(TAG, "Invalid axis value from event: %d", axis_val); return; }
        } else { return; }
    } else {
        if (code == LV_EVENT_CLICKED) {
            switch (action_type) {
                case ACTION_JOG_SET_AXIS_X: new_axis = AXIS_X; break;
                case ACTION_JOG_SET_AXIS_Y: new_axis = AXIS_Y; break;
                case ACTION_JOG_SET_AXIS_Z: new_axis = AXIS_Z; break;
                default: ESP_LOGW(TAG, "Unhandled specific set axis action: %d", action_type); return;
            }
        } else { return; }
    }
    ESP_LOGI(TAG, "Action: JOG_SET_AXIS to %d (%c)", new_axis, axis_to_char(new_axis));
    machine_interface_set_current_move_axis(machine, new_axis);
}

static void handle_jog_store_restore_axis(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        machine_interface_t* machine = vm->machine;
        if (!machine) { ESP_LOGE(TAG, "Machine not available for JOG_STORE/RESTORE_AXIS."); return; }
        if (action_type == ACTION_JOG_STORE_AXIS) {
            vm->stored_jog_axis = machine_interface_get_current_move_axis(machine);
            ESP_LOGI(TAG, "Action: JOG_STORE_AXIS, stored: %d (%c)", vm->stored_jog_axis, axis_to_char(vm->stored_jog_axis));
        } else if (action_type == ACTION_JOG_RESTORE_AXIS) {
            ESP_LOGI(TAG, "Action: JOG_RESTORE_AXIS to %d (%c)", vm->stored_jog_axis, axis_to_char(vm->stored_jog_axis));
            if (vm->stored_jog_axis != AXIS_OFF) { machine_interface_set_current_move_axis(machine, vm->stored_jog_axis); }
            else { ESP_LOGW(TAG, "No valid jog axis stored or stored axis is OFF."); }
        }
    }
}

static void handle_jog_set_step(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        machine_interface_t* machine = vm->machine;
        if (!machine) { ESP_LOGE(TAG, "Machine not available for JOG_SET_STEP."); return; }
        lv_obj_t* target = lv_event_get_target(e); int32_t value_micrometers = 0;
        if (lv_obj_is_valid(target) && lv_obj_check_type(target, &lv_slider_class)) { value_micrometers = lv_slider_get_value(target); }
        else if (lv_obj_is_valid(target) && lv_obj_check_type(target, &lv_spinbox_class)) { value_micrometers = lv_spinbox_get_value(target); }
        else { ESP_LOGW(TAG, "JOG_SET_STEP expects slider/spinbox"); return; }
        machine->current_move_step = (float)value_micrometers / 1000.0f;
        ESP_LOGI(TAG, "Action: JOG_SET_STEP, Value_um: %d, Set machine->current_move_step to: %.4f mm", value_micrometers, machine->current_move_step);
    }
}

static void handle_wcs_set(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (!machine || !machine->set_wcs) { ESP_LOGE(TAG, "Machine or set_wcs not available."); return; }
        lv_obj_t* target = lv_event_get_target(e); int wcs_index = -1;
        if (lv_obj_is_valid(target) && lv_obj_check_type(target, &lv_dropdown_class)) { wcs_index = lv_dropdown_get_selected(target); }
        else { ESP_LOGW(TAG, "ACTION_WCS_SET expects dropdown"); return; }
        if (wcs_index >= 0) { ESP_LOGI(TAG, "Action: WCS_SET, Index: %d", wcs_index); machine->set_wcs(machine, wcs_index); }
        else { ESP_LOGW(TAG, "Invalid WCS index: %d", wcs_index); }
    }
}

static void handle_wcs_next(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->next_wcs) { ESP_LOGE(TAG, "Machine or next_wcs not available."); return; }
        ESP_LOGI(TAG, "Action: WCS_NEXT"); machine->next_wcs(machine);
    }
}

static void handle_wcs_zero_axis(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->set_wcs_zero) { ESP_LOGE(TAG, "Machine or set_wcs_zero not available."); return; }
        const char* axes_to_zero = NULL;
        switch (action_type) {
            case ACTION_WCS_ZERO_AXIS_X: axes_to_zero = "X"; break;
            case ACTION_WCS_ZERO_AXIS_Y: axes_to_zero = "Y"; break;
            case ACTION_WCS_ZERO_AXIS_Z: axes_to_zero = "Z"; break;
            case ACTION_WCS_ZERO_AXIS_A: axes_to_zero = "A"; break;
            case ACTION_WCS_ZERO_ALL: axes_to_zero = "XYZA"; break;
            default: ESP_LOGE(TAG, "Invalid action type in handle_wcs_zero_axis: %d", action_type); return;
        }
        ESP_LOGI(TAG, "Action: WCS_ZERO_AXIS for '%s' on WCS %d", axes_to_zero, machine->wcs);
        machine->set_wcs_zero(machine, machine->wcs, axes_to_zero);
    }
}

static void handle_spindle_on_off(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->send_gcode) { ESP_LOGE(TAG, "Machine or send_gcode not available."); return; }
        const char* gcode_to_send = (action_type == ACTION_SPINDLE_ON) ? "M3" : "M5";
        ESP_LOGI(TAG, "Action: %s (%s)", (action_type == ACTION_SPINDLE_ON) ? "SPINDLE_ON" : "SPINDLE_OFF", gcode_to_send);
        machine->send_gcode(machine, gcode_to_send, 0);
    }
}

static void handle_spindle_set_speed(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (!machine || !machine->send_gcode) { ESP_LOGE(TAG, "Machine or send_gcode not available."); return; }
        lv_obj_t* target = lv_event_get_target(e); int spindle_rpm = 0;
        if (lv_obj_is_valid(target)) {
            if (lv_obj_check_type(target, &lv_slider_class)) { spindle_rpm = lv_slider_get_value(target); }
            else if (lv_obj_check_type(target, &lv_spinbox_class)) { spindle_rpm = lv_spinbox_get_value(target); }
            else { ESP_LOGW(TAG, "ACTION_SPINDLE_SET_SPEED: Unhandled widget type."); return; }
        } else { ESP_LOGE(TAG, "ACTION_SPINDLE_SET_SPEED: Invalid target."); return; }
        if (spindle_rpm >= 0) {
            char gcode_buffer[32]; snprintf(gcode_buffer, sizeof(gcode_buffer), "S%d", spindle_rpm);
            ESP_LOGI(TAG, "Action: SPINDLE_SET_SPEED, RPM: %d (G-code: %s)", spindle_rpm, gcode_buffer);
            machine->send_gcode(machine, gcode_buffer, 0);
        } else { ESP_LOGW(TAG, "Invalid spindle RPM: %d", spindle_rpm); }
    }
}

static void handle_feed_override_set(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (!machine || !machine->send_gcode) { ESP_LOGE(TAG, "Machine or send_gcode not available."); return; }
        lv_obj_t* target = lv_event_get_target(e); int feed_override_pct = 0;
        if (lv_obj_is_valid(target) && lv_obj_check_type(target, &lv_slider_class)) { feed_override_pct = lv_slider_get_value(target); }
        else { ESP_LOGW(TAG, "ACTION_FEED_OVERRIDE_SET expects slider."); return; }
        if (feed_override_pct >= 0) {
            char gcode_buffer[32]; snprintf(gcode_buffer, sizeof(gcode_buffer), "M220 S%d", feed_override_pct);
            ESP_LOGI(TAG, "Action: FEED_OVERRIDE_SET, Percentage: %d%% (G-code: %s)", feed_override_pct, gcode_buffer);
            machine->send_gcode(machine, gcode_buffer, 0);
        } else { ESP_LOGW(TAG, "Invalid feed override percentage: %d", feed_override_pct); }
    }
}

static void handle_files_macros_list(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->list_files) { ESP_LOGE(TAG, "Machine or list_files not available."); return; }
        const char* path_to_list = (action_type == ACTION_FILES_LIST) ? "/gcodes" : "/macros";
        ESP_LOGI(TAG, "Action: %s, Path: %s", (action_type == ACTION_FILES_LIST) ? "FILES_LIST" : "MACROS_LIST", path_to_list);
        machine->list_files(machine, path_to_list);
    }
}

static void handle_probe_mode(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (!vm) { ESP_LOGE(TAG, "VM not available for ACTION_PROBE_MODE."); return; }
        lv_obj_t* target = lv_event_get_target(e); int probe_mode_idx = -1;
        if (lv_obj_is_valid(target) && lv_obj_check_type(target, &lv_dropdown_class)) { probe_mode_idx = lv_dropdown_get_selected(target); }
        else { ESP_LOGW(TAG, "ACTION_PROBE_MODE expects dropdown."); return; }
        if (probe_mode_idx >= 0 && probe_mode_idx < num_predefined_probe_commands) {
            strncpy(vm->current_probe_gcode, predefined_probe_commands[probe_mode_idx], MAX_GCODE_STR_LEN -1);
            vm->current_probe_gcode[MAX_GCODE_STR_LEN -1] = '\0';
            ESP_LOGI(TAG, "Action: PROBE_MODE, Index: %d, Set G-code to: %s", probe_mode_idx, vm->current_probe_gcode);
        } else { ESP_LOGW(TAG, "Invalid probe mode index: %d", probe_mode_idx); vm->current_probe_gcode[0] = '\0'; }
    }
}

static void handle_probe_start(cnc_ui_viewmodel_t* vm, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        machine_interface_t* machine = vm->machine;
        if (!machine || !machine->probe) { ESP_LOGE(TAG, "Machine or probe not available."); return; }
        if (strlen(vm->current_probe_gcode) > 0) {
            ESP_LOGI(TAG, "Action: PROBE_START, Executing G-code: %s", vm->current_probe_gcode);
            machine->probe(machine, vm->current_probe_gcode);
        } else { ESP_LOGW(TAG, "ACTION_PROBE_START: No probe G-code set."); }
    }
}

static void handle_modal_ok_cancel(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->message_box) { ESP_LOGW(TAG, "Modal OK/Cancel: No active message_box."); return; }
        int modal_id = machine->message_box->seq;
        if (action_type == ACTION_MODAL_OK) {
            ESP_LOGI(TAG, "Action: MODAL_OK, Modal ID: %d", modal_id);
            if (machine->modal_ok) machine->modal_ok(machine, modal_id);
        } else if (action_type == ACTION_MODAL_CANCEL) {
            ESP_LOGI(TAG, "Action: MODAL_CANCEL, Modal ID: %d", modal_id);
            if (machine->modal_cancel) machine->modal_cancel(machine, modal_id);
        }
    }
}

static void handle_modal_choice(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->message_box || !machine->modal_choice) { ESP_LOGW(TAG, "Modal Choice: Func not available."); return; }
        int modal_id = machine->message_box->seq; int choice_idx = 0; // Placeholder
        ESP_LOGW(TAG, "ACTION_MODAL_CHOICE: Index extraction placeholder (using %d).", choice_idx);
        ESP_LOGI(TAG, "Action: MODAL_CHOICE, Index: %d, Modal ID: %d", choice_idx, modal_id);
        machine->modal_choice(machine, choice_idx, modal_id);
    }
}

static void handle_modal_input_int_float(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->message_box) { ESP_LOGW(TAG, "Modal Input Int/Float: No active message_box."); return; }
        int modal_id = machine->message_box->seq; lv_obj_t* input_field = NULL; // Placeholder
        if (!input_field || !lv_obj_is_valid(input_field)) {
             ESP_LOGE(TAG, "Modal Input Int/Float: Could not find input field. Placeholder value used.", modal_id);
             const char* text_value = (action_type == ACTION_MODAL_INPUT_INT) ? "0" : "0.0";
             if (action_type == ACTION_MODAL_INPUT_INT) {
                 if (!machine->modal_int) { ESP_LOGE(TAG, "machine->modal_int is NULL"); return; }
                 machine->modal_int(machine, atoi(text_value), modal_id);
             } else if (action_type == ACTION_MODAL_INPUT_FLOAT) {
                 if (!machine->modal_float) { ESP_LOGE(TAG, "machine->modal_float is NULL"); return; }
                 machine->modal_float(machine, (int)(atof(text_value) * 100.0f), modal_id);
             } return;
        }
        const char* text_value = "";
        if (lv_obj_check_type(input_field, &lv_textarea_class)) { text_value = lv_textarea_get_text(input_field); }
        else { ESP_LOGE(TAG, "Modal Input Int/Float: Input field not textarea."); return; }
        if (action_type == ACTION_MODAL_INPUT_INT) {
            if (!machine->modal_int) { ESP_LOGE(TAG, "machine->modal_int is NULL"); return; }
            machine->modal_int(machine, atoi(text_value), modal_id);
        } else if (action_type == ACTION_MODAL_INPUT_FLOAT) {
            if (!machine->modal_float) { ESP_LOGE(TAG, "machine->modal_float is NULL"); return; }
            machine->modal_float(machine, (int)(atof(text_value) * 100.0f), modal_id);
        }
    }
}

static void handle_modal_input_str(machine_interface_t* machine, data_action_t action_type, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!machine || !machine->message_box) { ESP_LOGW(TAG, "Modal Input Str: No active message_box."); return; }
        int modal_id = machine->message_box->seq;
        ESP_LOGW(TAG, "Action: MODAL_INPUT_STR (Modal ID: %d) - Not fully implemented.", modal_id);
        if (machine->modal_str) { /* machine->modal_str(machine, "placeholder", modal_id); */ }
        else { ESP_LOGE(TAG, "machine->modal_str is NULL"); }
    }
}


// --- Data Display Implementation ---

// Forward declaration for the notifier - already present near top from previous attempt, but ensure it's there
// static void data_binding_notify_data_changed(cnc_ui_viewmodel_t* vm, data_binding_value_t data_type, const char* str_val, double float_val, bool bool_val, int int_val);

// Implementation of data_binding_notify_data_changed
static void data_binding_notify_data_changed(cnc_ui_viewmodel_t* vm, data_binding_value_t data_type, const char* str_val, double float_val, bool bool_val, int int_val) {
    if (!vm) {
        ESP_LOGE(TAG, "VM is NULL in notify_data_changed for type %d.", data_type);
        return;
    }
    if (data_type >= _DATA_COUNT) {
        ESP_LOGE(TAG, "Invalid data_type %d in notify_data_changed.", data_type);
        return;
    }

    data_binding_registry_entry_t* entry = &vm->registry.display_registry[data_type];
    ESP_LOGD(TAG, "Notifying data change for type: %s (%d), listeners: %u", data_binding_value_to_string(data_type), data_type, entry->count);

    for (size_t i = 0; i < entry->count; i++) {
        data_binding_t* listener = &entry->listeners[i];
        lv_obj_t* widget = listener->widget;

        if (!widget || !lv_obj_is_valid(widget)) {
            ESP_LOGW(TAG, "Listener %u for data_type %d has invalid widget.", i, data_type);
            continue;
        }

        // --- String types ---
        if (data_type == DATA_MACHINE_STATUS_TEXT ||
            data_type == DATA_JOB_FILENAME ||
            data_type == DATA_WCS_CURRENT_TEXT ||
            data_type == DATA_JOG_CURRENT_AXIS_TEXT) {
            if (lv_obj_check_type(widget, &lv_label_class)) {
                if (str_val) {
                    lv_label_set_text(widget, str_val);
                } else {
                    lv_label_set_text(widget, "");
                    ESP_LOGW(TAG, "str_val is NULL for text data_type %d", data_type);
                }
            } else {
                ESP_LOGW(TAG, "Widget type not lv_label for string data_type %d", data_type);
            }
            continue;
        }

        // --- Boolean types (LEDs, Labels) ---
        if (data_type == DATA_IS_CONNECTED ||
            data_type == DATA_IS_HOMED_X ||
            data_type == DATA_IS_HOMED_Y ||
            data_type == DATA_IS_HOMED_Z ||
            data_type == DATA_IS_HOMED_ALL ||
            data_type == DATA_ENDSTOP_STATE_X || data_type == DATA_ENDSTOP_STATE_Y ||
            data_type == DATA_ENDSTOP_STATE_Z || data_type == DATA_ENDSTOP_STATE_A ||
            data_type == DATA_PROBE_1 || data_type == DATA_PROBE_2 ) {
            if (lv_obj_check_type(widget, &lv_led_class)) {
                bool_val ? lv_led_on(widget) : lv_led_off(widget);
            } else if (lv_obj_check_type(widget, &lv_label_class)) {
                if (listener->format_str) {
                     lv_label_set_text_fmt(widget, listener->format_str, bool_val ? "Yes" : "No");
                } else {
                    const char* true_text = "Active";
                    const char* false_text = "Inactive";
                    if (data_type >= DATA_IS_HOMED_X && data_type <= DATA_IS_HOMED_ALL) {
                        true_text = "Homed"; false_text = "Not Homed";
                    } else if (data_type == DATA_IS_CONNECTED) {
                        true_text = "Connected"; false_text = "Disconnected";
                    } else if (data_type >= DATA_ENDSTOP_STATE_X && data_type <= DATA_PROBE_2) {
                         true_text = "Triggered"; false_text = "Clear";
                    }
                    lv_label_set_text(widget, bool_val ? true_text : false_text);
                }
            } else {
                 ESP_LOGW(TAG, "Widget type not lv_led/lv_label for bool data_type %s (%d)",data_binding_value_to_string(data_type), data_type);
            }
            continue;
        }

        // --- Integer types for Dropdowns or Labels ---
        if (data_type == DATA_WCS_CURRENT || data_type == DATA_JOG_CURRENT_AXIS || data_type == DATA_CURRENT_TOOL) {
            if (lv_obj_check_type(widget, &lv_dropdown_class)) {
                lv_dropdown_set_selected(widget, int_val);
            } else if (lv_obj_check_type(widget, &lv_label_class)) {
                 if (listener->format_str) {
                    lv_label_set_text_fmt(widget, listener->format_str, int_val);
                } else {
                    lv_label_set_text_fmt(widget, "%d", int_val);
                }
            } else {
                ESP_LOGW(TAG, "Widget type not lv_dropdown/lv_label for int data_type %s (%d)", data_binding_value_to_string(data_type), data_type);
            }
            continue;
        }

        // --- Float/Integer values for Sliders & Bars (and labels as fallback) ---
        if (data_type == DATA_JOB_PROGRESS || data_type == DATA_FEED_OVERRIDE_PCT ||
            data_type == DATA_SPINDLE_SPEED_CURRENT || data_type == DATA_SPINDLE_SPEED_REQUESTED) {
            if (lv_obj_check_type(widget, &lv_slider_class)) {
                lv_slider_set_value(widget, (int32_t)float_val, LV_ANIM_OFF);
            } else if (lv_obj_check_type(widget, &lv_bar_class)) {
                lv_bar_set_value(widget, (int32_t)float_val, LV_ANIM_OFF);
            } else if (lv_obj_check_type(widget, &lv_label_class)) {
                if (listener->format_str) {
                    lv_label_set_text_fmt(widget, listener->format_str, float_val);
                } else {
                    if (data_type == DATA_JOB_PROGRESS || data_type == DATA_FEED_OVERRIDE_PCT) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%.0f%%", float_val);
                        lv_label_set_text(widget, buf);
                    } else {
                        lv_label_set_text_fmt(widget, "%.1f", float_val);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Widget type not lv_slider/lv_bar/lv_label for progress/override/speed data_type %s (%d)", data_binding_value_to_string(data_type), data_type);
            }
            continue;
        }

        // --- Generic Float types for Labels (positions, feeds, speeds not covered above) ---
        if (data_type == DATA_POS_MACHINE_X || data_type == DATA_POS_MACHINE_Y || data_type == DATA_POS_MACHINE_Z ||
            data_type == DATA_POS_WORK_X || data_type == DATA_POS_WORK_Y || data_type == DATA_POS_WORK_Z ||
            data_type == DATA_POS_DISTANCE_TO_GO_X || data_type == DATA_POS_DISTANCE_TO_GO_Y || data_type == DATA_POS_DISTANCE_TO_GO_Z ||
            data_type == DATA_FEED_CURRENT || data_type == DATA_FEED_REQUESTED ||
            data_type == DATA_JOG_CURRENT_STEP ||
            data_type == DATA_JOB_ELAPSED_TIME || data_type == DATA_JOB_REMAINING_TIME) {
            if (lv_obj_check_type(widget, &lv_label_class)) {
                if (listener->format_str) {
                    lv_label_set_text_fmt(widget, listener->format_str, float_val);
                } else {
                    lv_label_set_text_fmt(widget, "%.3f", float_val);
                }
            }
            else {
                 ESP_LOGW(TAG, "Widget type not lv_label for generic float data_type %s (%d)", data_binding_value_to_string(data_type), data_type);
            }
             continue;
        }

        // --- TEXT_LIST types for lv_list ---
        if (data_type == DATA_FILE_TEXT_LIST || data_type == DATA_MACRO_LIST) {
            if (lv_obj_check_type(widget, &lv_list_class)) {
                lv_obj_clean(widget);
                if (str_val && strlen(str_val) > 0) {
                    char* mutable_str_val = strdup(str_val);
                    if (!mutable_str_val) {
                        ESP_LOGE(TAG, "Failed to allocate memory for tokenizing list string.");
                        continue;
                    }
                    char* token = strtok(mutable_str_val, "\n");
                    while (token != NULL) {
                        char* cr = strchr(token, '\r');
                        if (cr) *cr = '\0';
                        if (strlen(token) > 0) {
                           lv_obj_t* btn = lv_list_add_btn(widget, NULL, token);
                           if (!btn) {
                               ESP_LOGE(TAG, "Failed to add button to list for token: %s", token);
                           }
                        }
                        token = strtok(NULL, "\n");
                    }
                    free(mutable_str_val);
                } else {
                    ESP_LOGD(TAG, "List data for %s is NULL or empty. List cleared.", data_binding_value_to_string(data_type));
                }
            } else {
                ESP_LOGW(TAG, "Widget type not lv_list for _TEXT_LIST data_type %s (%d)", data_binding_value_to_string(data_type), data_type);
            }
            continue;
        }

        // --- Special Modal Dialog type ---
        if (data_type == DATA_MODAL_DIALOG) {
            ESP_LOGD(TAG, "DATA_MODAL_DIALOG update to be handled by modal display logic (not part of this generic notifier).");
        } else {
            ESP_LOGW(TAG, "Data type %s (%d) has no specific update logic implemented yet for widget at listener %u.", data_binding_value_to_string(data_type), data_type, i);
        }
    }
}

// Placeholder for machine_interface_t callbacks (to be expanded in Step 5)
static void mi_state_changed_cb(machine_interface_t *machine, void *user_data) {
    cnc_ui_viewmodel_t* vm = (cnc_ui_viewmodel_t*)user_data;
    if (!vm) return;
    ESP_LOGD(TAG, "Placeholder: mi_state_changed_cb triggered. Status: %d", machine->machine_status);
    // data_binding_notify_data_changed(vm, DATA_MACHINE_STATUS_TEXT, machine_status_to_string(machine->machine_status), 0.0, false, 0);
    // data_binding_notify_data_changed(vm, DATA_IS_CONNECTED, NULL, 0.0, machine->is_connected(machine), 0);
}

static void mi_pos_changed_cb(machine_interface_t *machine, void *user_data) {
    cnc_ui_viewmodel_t* vm = (cnc_ui_viewmodel_t*)user_data;
    if (!vm) return;
    ESP_LOGD(TAG, "Placeholder: mi_pos_changed_cb triggered. Xw: %.2f, Xm: %.2f", machine->wcs_position[0], machine->position[0]);

    data_binding_notify_data_changed(vm, DATA_POS_WORK_X, NULL, machine->wcs_position[0], false, 0);
    data_binding_notify_data_changed(vm, DATA_POS_WORK_Y, NULL, machine->wcs_position[1], false, 0);
    data_binding_notify_data_changed(vm, DATA_POS_WORK_Z, NULL, machine->wcs_position[2], false, 0);

    data_binding_notify_data_changed(vm, DATA_POS_MACHINE_X, NULL, machine->position[0], false, 0);
    data_binding_notify_data_changed(vm, DATA_POS_MACHINE_Y, NULL, machine->position[1], false, 0);
    data_binding_notify_data_changed(vm, DATA_POS_MACHINE_Z, NULL, machine->position[2], false, 0);
}

static void mi_home_changed_cb(machine_interface_t* machine, void *user_data) {
    cnc_ui_viewmodel_t* vm = (cnc_ui_viewmodel_t*)user_data;
    if(!vm) return;
    ESP_LOGD(TAG, "Placeholder: mi_home_changed_cb. X:%d Y:%d Z:%d", machine->axes_homed[0], machine->axes_homed[1], machine->axes_homed[2]);
    data_binding_notify_data_changed(vm, DATA_IS_HOMED_X, NULL, 0.0, machine->axes_homed[0],0);
    data_binding_notify_data_changed(vm, DATA_IS_HOMED_Y, NULL, 0.0, machine->axes_homed[1],0);
    data_binding_notify_data_changed(vm, DATA_IS_HOMED_Z, NULL, 0.0, machine->axes_homed[2],0);
    bool all_homed = machine->axes_homed[0] && machine->axes_homed[1] && machine->axes_homed[2];
    data_binding_notify_data_changed(vm, DATA_IS_HOMED_ALL, NULL, 0.0, all_homed,0);
}

// Original bool data_binding_register_widget and related functions follow...
bool data_binding_register_widget(data_binding_registry_t* registry, data_binding_value_t data_type, lv_obj_t* widget, const char * format_str) {
    if (!registry) { ESP_LOGE(TAG, "Registry is NULL."); return false; }
    if (data_type >= _DATA_COUNT) { ESP_LOGE(TAG, "Invalid data_type: %d", data_type); return false; }
    if (!widget) { ESP_LOGE(TAG, "Widget is NULL for data_type: %s", data_binding_value_to_string(data_type)); return false; }
    data_binding_registry_entry_t* entry = &registry->display_registry[data_type];
    if (entry->count >= CNC_UI_MAX_LISTENERS) { ESP_LOGE(TAG, "Max listeners reached for data_type: %s", data_binding_value_to_string(data_type)); return false; }
    entry->listeners[entry->count].widget = widget;
    entry->listeners[entry->count].format_str = format_str;
    entry->count++;
    ESP_LOGI(TAG, "Widget registered for data_type: %s, count: %d", data_binding_value_to_string(data_type), entry->count);
    return true;
}

bool data_binding_register_widget_s(data_binding_registry_t* registry, const char * data_type_s, lv_obj_t* widget, const char * format_str) {
    if (!registry) { ESP_LOGE(TAG, "Registry is NULL (string version)."); return false; }
    if (!data_type_s) { ESP_LOGE(TAG, "data_type_s is NULL."); return false; }
    data_binding_value_t data_type = map_string_to_data_type(data_type_s);
    if (data_type == _DATA_COUNT) { ESP_LOGE(TAG, "Invalid data_type_s: %s", data_type_s); return false; }
    return data_binding_register_widget(registry, data_type, widget, format_str);
}

lv_event_cb_t action_registry_get_handler(data_binding_registry_t* registry, data_action_t action_type) {
    if (!registry) { ESP_LOGE(TAG, "Registry is NULL for getting action handler."); return NULL; }
    if (action_type >= _ACTION_COUNT) { ESP_LOGE(TAG, "Invalid action_type: %d", action_type); return NULL; }
    if (registry->action_handlers[action_type] == NULL) {
         ESP_LOGW(TAG, "Action handler for action_type %d is not yet implemented/assigned.", action_type);
    }
    return registry->action_handlers[action_type];
}

lv_event_cb_t action_registry_get_handler_s(data_binding_registry_t* registry, const char *action_type_s) {
    if (!registry) { ESP_LOGE(TAG, "Registry is NULL (string version for action handler)."); return NULL; }
    if (!action_type_s) { ESP_LOGE(TAG, "action_type_s is NULL."); return NULL; }
    data_action_t action_type = map_string_to_action(action_type_s);
    if (action_type == _ACTION_COUNT) { ESP_LOGE(TAG, "Invalid action_type_s: %s", action_type_s); return NULL; }
    return action_registry_get_handler(registry, action_type);
}
