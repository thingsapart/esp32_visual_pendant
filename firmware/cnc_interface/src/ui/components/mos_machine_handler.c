#define UI_DEBUG_LOCAL_LEVEL D_VERBOSE
#include "debug.h"

/**
 * @file mos_machine_handler.c
 * @brief Implements the machine-specific logic for MillenniumOS.
 *
 * This file acts as the "glue" layer between the generic Probing Wizard UI and a
 * machine running RepRapFirmware with the MillenniumOS probing macros. It handles
 * the two-way, asynchronous communication required for probing operations.
 *
 * Interaction Flow:
 *
 * The key challenge is that machine operations are not instant. When the UI needs
 * to run a probe, it cannot simply block and wait. This handler implements the
 * necessary state management to handle this asynchronous flow.
 *
 *           Probing Wizard (UI Task)          |      MOS Machine Handler (Callback Context)
 *   ============================================================================================
 *   1. User clicks "Next" to start a probe.   |
 *      Wizard calls `exec_probe_cb` which is  |
 *      mapped to `mos_execute_probe()`.       |
 *                                             |
 *   2. `lv_probing_wizard_...`                |  `mos_execute_probe()`
 *      -------------------------------------> |  - Builds G-Code string (e.g., M98 P"G6503.1.g")
 *                                             |  - Sends G-Code to machine.
 *                                             |  - Sets internal state to PROBE_STATE_PENDING_*.
 *                                             |  - Returns immediately.
 *                                             |
 *   3. UI remains responsive.                 |  (Machine is now running the probe macro)
 *                                             |
 *                                             |  (Time passes...)
 *                                             |
 *   4. (Machine Interface Task)               |  `_mos_log_message_cb()`
 *      Receives log messages from machine.    |  <-------------------------------------------------
 *      Invokes this handler's callback.       |  - Parses log messages for results (e.g., "global.mosWPCtrPos[0]={...}")
 *                                             |  - Stores parsed values in `handler_state`.
 *                                             |
 *   5.                                        |  - Sees completion message ("MillenniumOS: ...")
 *                                             |  - Sets internal state back to PROBE_STATE_IDLE.
 *                                             |  - Calls back to the wizard using deferred functions.
 *                                             |
 *   6. `lv_probing_wizard_*_deferred()`       |  `lv_probing_wizard_report_final_result_deferred()`
 *      <------------------------------------- |
 *      - This sets a flag and schedules a     |
 *        one-shot LVGL timer.                 |
 *                                             |
 *   7. (LVGL Task, a few ms later)            |
 *      The deferred timer callback fires,     |
 *      safely updating the UI with the        |
 *      results and advancing to the           |
 *      "Complete" screen.                     |
 *
 */

#include "mos_machine_handler.h"
#include "lv_probing_wizard.h"
#include <stdio.h>
#include <math.h>
#include "cJSON.h"

static const char * TAG = "mos_machine_handler";

// --- Constants for Probing Parameters ---
// These values are used to construct the G-Code commands. They could be
// made configurable in a real-world application.

// Clearance from the expected surface to start the probing move.
#define PROBE_DEFAULT_CLEARANCE 2.0f
// Overtravel distance past the expected surface before the probe errors out.
#define PROBE_DEFAULT_OVERTRAVEL 2.0f
// For circular probing, the wizard UI doesn't have a diameter input.
// We must use a default value. This is a known limitation.
#define PROBE_DEFAULT_CIRCLE_DIAMETER 20.0f
// The distance to probe downwards when finding the Z surface.
#define PROBE_DEFAULT_Z_DISTANCE 10.0f
#define MAX_PROBE_RESULTS 4

// Backoff distance calculation for XY probing moves
#define PROBE_TIP_RADIUS 1.0f // Assumed 2mm diameter probe tip
#define PROBE_BACKOFF_MULTIPLIER 6.0f
#define PROBE_XY_BACKOFF_DISTANCE (PROBE_TIP_RADIUS * PROBE_BACKOFF_MULTIPLIER)

extern const uint8_t probe_routine_sizes[];

/**
 * @brief Represents the current state of an asynchronous probing operation from the handler's perspective.
 *
 * This is a simple state machine to track whether the handler has sent a command
 * to the machine and is currently awaiting a result. It prevents new probe commands
 * from being sent while one is already in progress.
 *
 * State Machine Flow:
 *
 *  .--------------------------------------------------------------------.
 *  | [ PROBE_STATE_IDLE ]                                               |
 *  | - The handler is not waiting for any probe to complete.            |
 *  | - It is safe to accept new commands from the wizard.               |
 *  '--------------------------------------------------------------------'
 *      |
 *      | `mos_execute_probe()` is called by the wizard.
 *      | A probe command (M98 P"...") is sent to the machine.
 *      V
 *  .--------------------------------------------------------------------.
 *  | [ PROBE_STATE_PENDING_*_COMPLETE ]                                 |
 *  | - The handler is actively listening for log messages from the      |
 *  |   machine to parse results.                                        |
 *  | - It will ignore any new probe commands from the wizard.           |
 *  '--------------------------------------------------------------------'
 *      |
 *      | `_mos_log_message_cb()` receives the "MillenniumOS: ..." completion message.
 *      V
 *    (Back to IDLE)
 *
 */
typedef enum {
    PROBE_STATE_IDLE,                 /**< Not currently probing. */
    PROBE_STATE_PENDING_Z_COMPLETE,   /**< Waiting for a Z-probe macro to finish. */
    PROBE_STATE_PENDING_XY_COMPLETE,  // Waiting for a complex XY-probe macro to finish
} probe_fsm_state_t;

/**
 * @brief A static struct to hold the state of the machine handler.
 * This includes pointers to the wizard and machine, and the state of the
 * current probing operation.
 */
static struct {
    machine_interface_t* machine;
    lv_obj_t* wizard_obj;
    probe_fsm_state_t probe_state;
    bool probe_was_running;
    
    // Storage for results parsed from log messages
    lv_probing_wizard_point_float_t parsed_result;
    lv_probing_wizard_point_float_t parsed_dims;
    float parsed_radius;
    float parsed_rotation;
    float parsed_z_result;
    char last_parsed_axis[MAX_PROBE_RESULTS];
    uint8_t probe_axes_reported_mask; // Bitmask: 1=X, 2=Y, 4=Z
} handler_state = {0};


// --- Forward Declarations ---
static lv_probing_wizard_point_float_t mos_get_current_jogged_position(void);
static void mos_execute_probe(lv_obj_t* wizard_obj, const lv_probing_action_t* action);
static void mos_set_wcs_origin(lv_obj_t* wizard_obj, uint8_t wcs_index, float x, float y, float z, bool apply_z);
static void mos_install_probe(lv_obj_t* wizard_obj);
static void _mos_state_changed_cb(machine_interface_t* machine, void* user_data);
static void _mos_conn_changed_cb(machine_interface_t* machine, void* user_data);
static void _mos_log_message_cb(machine_interface_t* machine, void* user_data, const char* message);

/**
 * @brief Registration function to connect the MOS handler callbacks to the wizard.
 */
void lv_probing_wizard_register_mos_callbacks(lv_obj_t* wizard_obj, machine_interface_t* machine) {
    if (!wizard_obj || !machine) {
        LOGE(TAG, "Cannot register MOS callbacks with NULL objects.");
        return;
    }

    // Store pointers to the wizard and machine interface for use in callbacks.
    handler_state.wizard_obj = wizard_obj;
    handler_state.machine = machine;
    handler_state.probe_state = PROBE_STATE_IDLE;
    handler_state.probe_axes_reported_mask = 0;

    // Register the three main callbacks with the wizard.
    lv_probing_wizard_register_callbacks(wizard_obj, mos_get_current_jogged_position, mos_execute_probe, mos_set_wcs_origin, mos_install_probe);

    // Register callbacks with the machine interface to receive status updates.
    // This is crucial for the asynchronous operation of the handler.
    machine_interface_add_state_change_cb(machine, NULL, _mos_state_changed_cb);
    machine_interface_add_connected_changed_cb(machine, NULL, _mos_conn_changed_cb);
    machine_interface_add_log_message_cb(machine, NULL, _mos_log_message_cb);

    LOGI(TAG, "MillenniumOS machine handler callbacks registered successfully.");

    // Update the connected state.
    _mos_conn_changed_cb(machine, NULL);
}

/**
 * @brief Callback to get the current jogged position from the machine.
 * This is called by the wizard when it needs to record a setup point.
 */
static lv_probing_wizard_point_float_t mos_get_current_jogged_position(void) {
    if (!handler_state.machine) {
        return (lv_probing_wizard_point_float_t){-9999, -9999};
    }
    // The machine interface stores the current position in WCS coordinates.
    return (lv_probing_wizard_point_float_t){
        //.x = handler_state.machine->wcs_position[0],
        //.y = handler_state.machine->wcs_position[1]
        .x = handler_state.machine->position[0],
        .y = handler_state.machine->position[1]
    };
}

static void mos_install_probe(lv_obj_t* wizard_obj) {
  machine_interface_send_gcode(handler_state.machine, "T T{global.mosPTID}", TOOLS);

  // TODO: Wait and check for probe installation success!
  lv_probing_wizard_probe_intalled(wizard_obj);
}

/**
 * @brief The main execution callback, triggered by the wizard for each probing step.
 * This function is the core of the handler, translating wizard actions into G-Code.
 */
static void mos_execute_probe(lv_obj_t* wizard_obj, const lv_probing_action_t* action) {
    if (!handler_state.machine || handler_state.probe_state != PROBE_STATE_IDLE) {
        LOGW(TAG, "Probe command ignored: machine not ready or probe already active.");
        return;
    }

    // Reset probe result state for the new operation
    handler_state.parsed_result = (lv_probing_wizard_point_float_t){.x = NAN, .y = NAN};
    handler_state.parsed_dims = (lv_probing_wizard_point_float_t){.x = NAN, .y = NAN};
    handler_state.parsed_radius = NAN;
    handler_state.parsed_rotation = NAN;
    handler_state.parsed_z_result = NAN;
    handler_state.probe_was_running = false;
    handler_state.probe_axes_reported_mask = 0;
    memset(handler_state.last_parsed_axis, 0, sizeof(handler_state.last_parsed_axis));

    char gcode_buf[200];
    const probe_point_t* setup_points = lv_probing_wizard_get_setup_points(wizard_obj);
    float z_top = lv_probing_wizard_get_z_top(wizard_obj);
    lv_probing_wizard_point_float_t current_pos = mos_get_current_jogged_position();

    // Handle single Z-surface probing.
    if (action->type == ACTION_PROBE_Z_TOP) {
        LOGI(TAG, "Executing Z-Top probe.");
        // G6510.1: Probe a single surface. Called via M98 for async execution.
        // J, K, L: Starting position (current XY, current Z in machine coordinates).
        // H4: Probe the top surface (+Z direction).
        // I: Max travel distance.
        // O: Overtravel allowance.
        snprintf(gcode_buf, sizeof(gcode_buf), "M98 P\"G6510.1.g\" J%.3f K%.3f L%.3f H4 I%.3f O%.3f",
                 current_pos.x, current_pos.y, handler_state.machine->position[2], 
                 PROBE_DEFAULT_Z_DISTANCE, PROBE_DEFAULT_OVERTRAVEL);
        
        handler_state.probe_state = PROBE_STATE_PENDING_Z_COMPLETE;
        handler_state.machine->probe(handler_state.machine, gcode_buf);
    
    // Handle complex XY probing routines.
    } else if (action->type == ACTION_PROBE_POINT) {
        // This logic triggers the entire MOS routine. The wizard UI has been simplified
        // to have only one "probe point" action per routine to match this.
        LOGI(TAG, "Executing XY probe routine for mode %d.", lv_probing_wizard_get_mode(wizard_obj));
        
        bool is_inside = lv_probing_wizard_get_is_inside(wizard_obj);
        
        switch (lv_probing_wizard_get_mode(wizard_obj)) {
            case LV_PROBING_WIZARD_MODE_RECTANGLE:
                if (!setup_points[0].is_set || !setup_points[1].is_set) {
                    LOGE(TAG, "Cannot start rectangle probe: setup points not set.");
                    return;
                }
                // Calculate center and dimensions from the two corner setup points.
                float center_x = (setup_points[0].x + setup_points[1].x) / 2.0f;
                float center_y = (setup_points[0].y + setup_points[1].y) / 2.0f;
                float dim_x = fabsf(setup_points[0].x - setup_points[1].x);
                float dim_y = fabsf(setup_points[0].y - setup_points[1].y);

                // G6502.1 for an inside rectangle (pocket), G6503.1 for an outside one (block).
                snprintf(gcode_buf, sizeof(gcode_buf), "M98 P\"%s\" J%.3f K%.3f L%.3f H%.3f I%.3f T%.3f O%.3f",
                         is_inside ? "G6502.1.g" : "G6503.1.g",
                         center_x, center_y, z_top - 2.0, dim_x, dim_y,
                         PROBE_XY_BACKOFF_DISTANCE, PROBE_DEFAULT_OVERTRAVEL);
                break;

            case LV_PROBING_WIZARD_MODE_CIRCLE:
                if (!setup_points[0].is_set) {
                    LOGE(TAG, "Cannot start circle probe: center point not set.");
                    return;
                }
                // G6500.1 for an inside circle (bore), G6501.1 for an outside one (boss).
                snprintf(gcode_buf, sizeof(gcode_buf), "M98 P\"%s\" J%.3f K%.3f L%.3f H%.3f T%.3f O%.3f",
                         is_inside ? "G6500.1.g" : "G6501.1.g",
                         setup_points[0].x, setup_points[0].y, z_top - 2.0,
                         PROBE_DEFAULT_CIRCLE_DIAMETER, PROBE_XY_BACKOFF_DISTANCE, PROBE_DEFAULT_OVERTRAVEL);
                break;
            
            case LV_PROBING_WIZARD_MODE_CORNER:
                if (is_inside) {
                    LOGE(TAG, "Inside corner probing is not supported by MillenniumOS (G6509).");
                    return;
                }
                lv_probing_wizard_corner_t corner = lv_probing_wizard_get_corner_type(wizard_obj);
                if (corner == LV_PROBING_CORNER_NONE) {
                    LOGE(TAG, "Cannot start corner probe: corner not selected.");
                    return;
                }
                // Map the wizard's corner enum to the G-code's numeric value (0-3).
                int corner_n = corner - 1; 

                // G6508.1: Probe an outside corner in "quick mode".
                snprintf(gcode_buf, sizeof(gcode_buf), "M98 P\"/macros/G6508.1.g\" J%.3f K%.3f L%.3f N%d Q1 T%.3f O%.3f",
                         current_pos.x, current_pos.y, z_top, corner_n,
                         PROBE_XY_BACKOFF_DISTANCE, PROBE_DEFAULT_OVERTRAVEL);
                break;
        }

        handler_state.probe_state = PROBE_STATE_PENDING_XY_COMPLETE;
        handler_state.machine->probe(handler_state.machine, gcode_buf);
    }
}

/**
 * @brief Callback to apply the final calculated coordinates as a WCS origin.
 */
static void mos_set_wcs_origin(lv_obj_t* wizard_obj, uint8_t wcs_index, float x, float y, float z, bool apply_z) {
    if (!handler_state.machine) return;

    char gcode_buf[128];
    char z_buf[32] = "";
    if (apply_z) {
        snprintf(z_buf, sizeof(z_buf), " Z%.4f", z);
    }

    // G10 L20 sets a work coordinate offset.
    // The wizard's wcs_index is 0-based for the button matrix (G55=0),
    // but G10 P... is 1-based (G54=P1, G55=P2). The wizard UI starts at G55.
    // So, we need to add 2 to the button index to get the correct P value.
    snprintf(gcode_buf, sizeof(gcode_buf), "G10 L20 P%d X%.4f Y%.4f%s",
             wcs_index, x, y, z_buf);

    LOGI(TAG, "Setting WCS: %s", gcode_buf);
    handler_state.machine->send_gcode(handler_state.machine, gcode_buf, 0);
}

/**
 * @brief Callback for machine state changes. Used to detect when a probe macro finishes.
 */
static void _mos_conn_changed_cb(machine_interface_t* machine, void* user_data) {
    bool connected = machine->is_connected ? machine->is_connected(machine) : false;

    LOGI(TAG, "MACHINE CONNECTED! %d", connected);
    lv_probing_wizard_set_connected(handler_state.wizard_obj, connected);
}

/**
 * @brief Callback for machine state changes. Used to detect when a probe macro finishes.
 */
static void _mos_state_changed_cb(machine_interface_t* machine, void* user_data) {
    // This callback is now only used to detect when a probe has *started*.
    // Completion is detected via log messages for greater reliability.
    if (handler_state.probe_state != PROBE_STATE_IDLE && machine->machine_status == MACHINE_STATUS_RUNNING) {
        if (!handler_state.probe_was_running) {
            handler_state.probe_was_running = true;
            LOGD(TAG, "Probe macro has started running.");
        }
    }
}

static void _mos_log_message_cb(machine_interface_t* machine, void* user_data, const char* message) {
    // Only parse logs if we are in a pending probe state
    if (handler_state.probe_state == PROBE_STATE_IDLE) {
        return;
    }

    const char* resp_str = message;
    int index;
    char axis;
    float f1, f2;

    // Use strstr to quickly find the key, then sscanf the rest of the string.
    const char *p;
    if ((p = strstr(resp_str, "global.mosWPCtrPos[")) != NULL) {
        if (sscanf(p, "global.mosWPCtrPos[%*d]={%f,%f}", &f1, &f2) == 2) {
            handler_state.parsed_result.x = f1;
            handler_state.parsed_result.y = f2;
            handler_state.probe_axes_reported_mask |= 3; // Bits 0 and 1 for X and Y
            LOGI(TAG, "Parsed center pos: X=%.4f, Y=%.4f", f1, f2);
        }
    } else if ((p = strstr(resp_str, "global.mosWPDims[")) != NULL) {
        if (sscanf(p, "global.mosWPDims[%*d]={%f,%f}", &f1, &f2) == 2) {
            handler_state.parsed_dims.x = f1;
            handler_state.parsed_dims.y = f2;
            LOGI(TAG, "Parsed dims: W=%.4f, H=%.4f", f1, f2);
        }
    } else if ((p = strstr(resp_str, "global.mosWPRad[")) != NULL) {
        if (sscanf(p, "global.mosWPRad[%*d]=%f", &f1) == 1) {
            handler_state.parsed_radius = f1;
            LOGI(TAG, "Parsed radius: R=%.4f", f1);
        }
    } else if ((p = strstr(resp_str, "global.mosWPDeg[")) != NULL) {
        if (sscanf(p, "global.mosWPDeg[%*d]=%f", &f1) == 1) {
            handler_state.parsed_rotation = f1;
            LOGI(TAG, "Parsed rotation: %.4f deg", f1);
        }
    } else if (sscanf(resp_str, "global.mosWPSfcAxis[%d]=%c", &index, &axis) == 2) {
        if (index < MAX_PROBE_RESULTS) {
            handler_state.last_parsed_axis[index] = axis;
            LOGI(TAG, "Parsed probed axis index %d: %c", index, axis);
        }
    } else if (sscanf(resp_str, "global.mosWPSfcPos[%d]=%f", &index, &f1) == 2) {
        if (index < MAX_PROBE_RESULTS) {
            char reported_axis = handler_state.last_parsed_axis[index];
            LOGI(TAG, "Parsed probed position index %d for axis %c: %f", index, reported_axis, f1);
            if (reported_axis == 'Z') {
                handler_state.parsed_z_result = f1;
                handler_state.probe_axes_reported_mask |= 4;
            }
        }
    }
    // Check for a completion message from any MillenniumOS macro
    else if (strstr(resp_str, "MillenniumOS:") != NULL) {
        probe_fsm_state_t finished_probe_type = handler_state.probe_state;
        
        // Reset state machine immediately.
        handler_state.probe_state = PROBE_STATE_IDLE;
        handler_state.probe_was_running = false;

        if (finished_probe_type == PROBE_STATE_PENDING_Z_COMPLETE) {
            LOGI(TAG, "Z-probe completion detected from log message: %s", resp_str);
            float z_res = handler_state.parsed_z_result;
            if (isnan(z_res)) {
                LOGW(TAG, "Z Probe completed but no result was parsed. Using last machine Z.");
                z_res = machine->position[2];
            }
            lv_probing_wizard_set_z_top_deferred(handler_state.wizard_obj, z_res);
        }
        else if (finished_probe_type == PROBE_STATE_PENDING_XY_COMPLETE) {
            LOGI(TAG, "XY-probe completion detected from log message: %s", resp_str);
            float res_x = handler_state.parsed_result.x;
            float res_y = handler_state.parsed_result.y;

            if ((handler_state.probe_axes_reported_mask & 3) != 3) { // 3 = (bit 0 for X) | (bit 1 for Y)
                LOGW(TAG, "XY Probe macro finished but results were not fully parsed (mask: %d).", handler_state.probe_axes_reported_mask);
            }
            
            // Report detailed results before advancing UI
            lv_probing_wizard_details_t details = {
                .dimensions = handler_state.parsed_dims,
                .radius = handler_state.parsed_radius,
                .rotation = handler_state.parsed_rotation,
            };
            lv_probing_wizard_report_details(handler_state.wizard_obj, &details);
            
            // Report final center point and advance UI to final step
            uint8_t num_steps = probe_routine_sizes[lv_probing_wizard_get_mode(handler_state.wizard_obj)];
            lv_probing_wizard_report_final_result(handler_state.wizard_obj, res_x, res_y);
            lv_probing_wizard_set_active_step_deferred(handler_state.wizard_obj, num_steps - 1);
        }
    }
}
