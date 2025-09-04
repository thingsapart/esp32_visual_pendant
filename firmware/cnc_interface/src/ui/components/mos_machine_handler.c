#include "mos_machine_handler.h"
#include "lv_probing_wizard.h"
#include <stdio.h>
#include <math.h>

#define UI_DEBUG_LOCAL_LEVEL D_VERBOSE
#include "debug.h"

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
#define PROBE_DEFAULT_Z_DISTANCE -10.0f 

extern const uint8_t probe_routine_sizes[];

/**
 * @brief Represents the current state of an asynchronous probing operation.
 */
typedef enum {
    PROBE_STATE_IDLE,                 // Not currently probing
    PROBE_STATE_PENDING_Z_COMPLETE,   // Waiting for a Z-probe macro to finish
    PROBE_STATE_PENDING_XY_COMPLETE,  // Waiting for a complex XY-probe macro to finish
    PROBE_STATE_AWAITING_POSITION,    // Macro is finished, waiting for the position report
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
    // Flag to differentiate which type of probe is awaiting a position report
    bool was_z_probe; 
} handler_state = {0};


// --- Forward Declarations ---
static lv_probing_wizard_point_float_t mos_get_current_jogged_position(void);
static void mos_execute_probe(lv_obj_t* wizard_obj, const lv_probing_action_t* action);
static void mos_set_wcs_origin(lv_obj_t* wizard_obj, uint8_t wcs_index, float x, float y, float z, bool apply_z);
static void _mos_state_changed_cb(machine_interface_t* machine, void* user_data);
static void _mos_pos_changed_cb(machine_interface_t* machine, void* user_data);

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
    handler_state.was_z_probe = false;

    // Register the three main callbacks with the wizard.
    lv_probing_wizard_register_callbacks(wizard_obj, mos_get_current_jogged_position, mos_execute_probe, mos_set_wcs_origin);

    // Register callbacks with the machine interface to receive status updates.
    // This is crucial for the asynchronous operation of the handler.
    machine_interface_add_state_change_cb(machine, NULL, _mos_state_changed_cb);
    machine_interface_add_pos_changed_cb(machine, NULL, _mos_pos_changed_cb);

    LOGI(TAG, "MillenniumOS machine handler callbacks registered successfully.");
}

/**
 * @brief Callback to get the current jogged position from the machine.
 * This is called by the wizard when it needs to record a setup point.
 */
static lv_probing_wizard_point_float_t mos_get_current_jogged_position(void) {
    if (!handler_state.machine) {
        return (lv_probing_wizard_point_float_t){0, 0};
    }
    // The machine interface stores the current position in WCS coordinates.
    return (lv_probing_wizard_point_float_t){
        .x = handler_state.machine->wcs_position[0],
        .y = handler_state.machine->wcs_position[1]
    };
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
        snprintf(gcode_buf, sizeof(gcode_buf), "M98 P\"/macros/G6510.1.g\" J%.3f K%.3f L%.3f H4 I%.3f O%.3f",
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
                snprintf(gcode_buf, sizeof(gcode_buf), "M98 P\"/macros/%s\" J%.3f K%.3f L%.3f H%.3f I%.3f T%.3f O%.3f",
                         is_inside ? "G6502.1.g" : "G6503.1.g",
                         center_x, center_y, z_top, dim_x, dim_y,
                         PROBE_DEFAULT_CLEARANCE, PROBE_DEFAULT_OVERTRAVEL);
                break;

            case LV_PROBING_WIZARD_MODE_CIRCLE:
                if (!setup_points[0].is_set) {
                    LOGE(TAG, "Cannot start circle probe: center point not set.");
                    return;
                }
                // G6500.1 for an inside circle (bore), G6501.1 for an outside one (boss).
                snprintf(gcode_buf, sizeof(gcode_buf), "M98 P\"/macros/%s\" J%.3f K%.3f L%.3f H%.3f T%.3f O%.3f",
                         is_inside ? "G6500.1.g" : "G6501.1.g",
                         setup_points[0].x, setup_points[0].y, z_top,
                         PROBE_DEFAULT_CIRCLE_DIAMETER, PROBE_DEFAULT_CLEARANCE, PROBE_DEFAULT_OVERTRAVEL);
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
                         PROBE_DEFAULT_CLEARANCE, PROBE_DEFAULT_OVERTRAVEL);
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
static void _mos_state_changed_cb(machine_interface_t* machine, void* user_data) {
    bool is_pending = (handler_state.probe_state == PROBE_STATE_PENDING_Z_COMPLETE ||
                       handler_state.probe_state == PROBE_STATE_PENDING_XY_COMPLETE);

    if (is_pending) {
        // RRF's "idle" state is mapped to our "RUNNING" status when not processing a job.
        // We check if the machine has returned to this state, which indicates the M98 macro has completed.
        if (machine->machine_status == MACHINE_STATUS_RUNNING) {
            LOGI(TAG, "Probe macro finished. Querying final position.");

            // Record whether this was a Z probe or an XY probe before changing state
            handler_state.was_z_probe = (handler_state.probe_state == PROBE_STATE_PENDING_Z_COMPLETE);
            
            // The macro is done. Now we need the machine's final position.
            // We change state and send a position query. The pos_changed_cb will handle the result.
            handler_state.probe_state = PROBE_STATE_AWAITING_POSITION;
            machine->send_gcode(machine, "M409 K\"move.axes[]\"", MACHINE_POSITION);
        }
    }
}

/**
 * @brief Callback for machine position updates. Lightweight check to retrieve probe result.
 */
static void _mos_pos_changed_cb(machine_interface_t* machine, void* user_data) {
    // This callback is lightweight. It only acts if we are in the specific state of
    // waiting for a position report after a probe. It ignores all other position updates.
    if (handler_state.probe_state == PROBE_STATE_AWAITING_POSITION) {
        LOGI(TAG, "Received position report after probe: WCS(X=%.3f, Y=%.3f), Machine(Z=%.3f)",
             machine->wcs_position[0], machine->wcs_position[1], machine->position[2]);
        
        // If the completed probe was for Z, update the wizard's Z-top value.
        if (handler_state.was_z_probe) {
             lv_probing_wizard_set_z_top(handler_state.wizard_obj, machine->position[2]);
             // After a Z probe, we simply advance the wizard to the next manual step.
             lv_probing_wizard_advance_step(handler_state.wizard_obj);
        } else {
            // If it was an XY probe, report the final calculated XY result.
            lv_probing_wizard_report_final_result(handler_state.wizard_obj,
                                                  machine->wcs_position[0],
                                                  machine->wcs_position[1]);
            // And then fast-forward the wizard to the final "complete" step.
            uint8_t num_steps = probe_routine_sizes[lv_probing_wizard_get_mode(handler_state.wizard_obj)];
            lv_probing_wizard_set_active_step(handler_state.wizard_obj, num_steps - 1);
        }
        
        // Reset the state machine to idle, ready for the next operation.
        handler_state.probe_state = PROBE_STATE_IDLE;
        handler_state.was_z_probe = false;
    }
}
