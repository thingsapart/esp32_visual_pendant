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
#include <string.h>
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
// The distance to probe downwards when finding the Z surface.
#define PROBE_DEFAULT_Z_DISTANCE 10.0f
// How far below z_top to probe when doing XY probing moves.
#define PROBE_XY_DEPTH_BELOW_Z_TOP 2.0f
#define Z_PROBING_BACKOFF_DISTANCE 2.0f
#define MAX_PROBE_RESULTS 16

// Backoff distance calculation for XY probing moves
#define PROBE_TIP_RADIUS 1.0f // Assumed 2mm diameter probe tip
#define PROBE_BACKOFF_MULTIPLIER 6.0f
#define PROBE_XY_BACKOFF_DISTANCE (PROBE_TIP_RADIUS * PROBE_BACKOFF_MULTIPLIER)

// Scratch WCS offset used during probing.
// MOS macros always set the WCS via G10 L2 immediately upon completion.
// To avoid polluting the user's desired WCS, we direct the macro to use this
// scratch offset. The wizard re-applies the parsed result to the user-chosen WCS
// at the end via the "Apply" button.
#define PROBE_SCRATCH_WCS_OFFSET 8

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
    PROBE_STATE_PENDING_XY_POINT,     /**< Waiting for a single XY probe point to finish. */
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
    lv_probing_wizard_point_float_t parsed_result;    // mosWPCtrPos (center) 
    lv_probing_wizard_point_float_t parsed_cnr_pos;   // mosWPCnrPos (corner position)
    lv_probing_wizard_point_float_t parsed_dims;      // mosWPDims
    float parsed_radius;                               // mosWPRad
    float parsed_rotation;                             // mosWPDeg
    float parsed_z_result;                             // Z from mosWPSfcPos when axis=Z
    char last_parsed_axis[MAX_PROBE_RESULTS];          // mosWPSfcAxis
    uint8_t probe_axes_reported_mask;                  // Bitmask: 1=X, 2=Y, 4=Z
    float parsed_sfc_pos;                              // mosWPSfcPos (non-Z)
    float parsed_sfc_pos_by_axis[3];                    // accumulated surface positions by axis: 0=X,1=Y,2=Z
    uint8_t parsed_sfc_pos_count[3];                    // count of samples per axis
    uint8_t current_probe_index;
    bool m7601_fallback_pending;                        // Set when M7601 query was sent; cleared on completion
} handler_state = {0};


// --- Forward Declarations ---
static lv_probing_wizard_point_float_t mos_get_current_jogged_position(void);
static void mos_execute_probe(lv_obj_t* wizard_obj, const lv_probing_action_t* action);
static void mos_set_wcs_origin(lv_obj_t* wizard_obj, uint8_t wcs_index, float x, float y, float z, bool apply_z);
static void mos_install_probe(lv_obj_t* wizard_obj);
static void mos_cancel_probe(lv_obj_t* wizard_obj);
static void mos_home_all_modal_event_handler(lv_event_t * e);
static void mos_show_home_all_modal(machine_interface_t * machine);
static void _mos_state_changed_cb(machine_interface_t* machine, void* user_data);
static void _mos_conn_changed_cb(machine_interface_t* machine, void* user_data);
static bool _mos_log_message_cb(machine_interface_t* machine, void* user_data, const char* message);
static void _mos_try_complete_from_fallback(void);

static void mos_cancel_probe(lv_obj_t* wizard_obj) {
    (void)wizard_obj;
    LOGI(TAG, "Cancel probe requested from UI. Clearing handler probe state.");
    handler_state.probe_state = PROBE_STATE_IDLE;
    handler_state.probe_was_running = false;
    handler_state.m7601_fallback_pending = false;
    handler_state.probe_axes_reported_mask = 0;
    memset(handler_state.last_parsed_axis, 0, sizeof(handler_state.last_parsed_axis));
    handler_state.parsed_result.x = NAN;
    handler_state.parsed_result.y = NAN;
    handler_state.parsed_cnr_pos.x = NAN;
    handler_state.parsed_cnr_pos.y = NAN;
    handler_state.parsed_dims.x = NAN;
    handler_state.parsed_dims.y = NAN;
    handler_state.parsed_radius = NAN;
    handler_state.parsed_rotation = NAN;
    handler_state.parsed_z_result = NAN;
}

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
    for (int i=0;i<3;i++) { handler_state.parsed_sfc_pos_by_axis[i] = NAN; handler_state.parsed_sfc_pos_count[i]=0; }

    // Register the three main callbacks with the wizard.
    lv_probing_wizard_register_callbacks(wizard_obj, mos_get_current_jogged_position, mos_execute_probe, mos_set_wcs_origin, mos_install_probe, mos_cancel_probe);

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

void ensure_homed() {
    if (!handler_state.machine) return;

    // If any axes are not homed, show the "Home All" modal and block until homing is complete.
    if (!handler_state.machine->axes_homed[0] || !handler_state.machine->axes_homed[1] || !handler_state.machine->axes_homed[2]) {
        LOGW(TAG, "Axes not homed. Prompting user to home all axes before probing.");
        mos_show_home_all_modal(handler_state.machine);
        // The modal will block the UI until homing is complete, at which point the state change callback will allow probing to proceed.
    } else {
        LOGI(TAG, "Axes are homed. Proceeding with probe.");  
    }
}

static void mos_install_probe(lv_obj_t* wizard_obj) {
    ensure_homed();

    if (!handler_state.machine) {
        LOGW(TAG, "Cannot install probe: machine is NULL");
    } else {
        LOGI(TAG, "Installing probe tool: T T{global.mosPTID}");
        handler_state.machine->send_gcode(handler_state.machine, "T T{global.mosPTID}", 0);
    }

    // TODO: Wait and check for probe installation success!
    lv_probing_wizard_probe_intalled(wizard_obj);
}

/**
 * @brief The main execution callback, triggered by the wizard for each probing step.
 * This function is the core of the handler, translating wizard actions into G-Code.
 *
 * All MOS .1.g macros expect machine coordinates for J, K, L and Z parameters.
 * We pass W{PROBE_SCRATCH_WCS_OFFSET} to direct the macro to set a scratch WCS,
 * avoiding pollution of the user's desired WCS. The wizard re-applies results later.
 */
static void mos_execute_probe(lv_obj_t* wizard_obj, const lv_probing_action_t* action) {
    if (!handler_state.machine) {
        LOGW(TAG, "Probe command ignored: machine interface is NULL.");
        return;
    }

    if (handler_state.probe_state != PROBE_STATE_IDLE) {
        // If the machine is not actually running a probe (probe_was_running==false)
        // then we may have been left in a non-idle state due to a missed completion
        // message or earlier failure. In that case, clear the stale state and
        // allow the new probe to proceed. Otherwise, reject the new probe.
        if (!handler_state.probe_was_running) {
            LOGW(TAG, "Clearing stale probe_state=%d (no running probe). Allowing new probe.", handler_state.probe_state);
            handler_state.probe_state = PROBE_STATE_IDLE;
            handler_state.m7601_fallback_pending = false;
            handler_state.probe_axes_reported_mask = 0;
            memset(handler_state.last_parsed_axis, 0, sizeof(handler_state.last_parsed_axis));
        } else {
            LOGW(TAG, "Probe command ignored: machine not ready or probe already active.");
            return;
        }
    }

    ensure_homed();

    // Reset probe result state for the new operation
    handler_state.parsed_result = (lv_probing_wizard_point_float_t){.x = NAN, .y = NAN};
    handler_state.parsed_cnr_pos = (lv_probing_wizard_point_float_t){.x = NAN, .y = NAN};
    handler_state.parsed_dims = (lv_probing_wizard_point_float_t){.x = NAN, .y = NAN};
    handler_state.parsed_radius = NAN;
    handler_state.parsed_rotation = NAN;
    handler_state.parsed_z_result = NAN;
    handler_state.parsed_sfc_pos = NAN;
    handler_state.probe_was_running = false;
    handler_state.probe_axes_reported_mask = 0;
    memset(handler_state.last_parsed_axis, 0, sizeof(handler_state.last_parsed_axis));
    for (int i=0;i<3;i++) { handler_state.parsed_sfc_pos_by_axis[i] = NAN; handler_state.parsed_sfc_pos_count[i]=0; }

    char gcode_buf[256];
    const probe_point_t* setup_points = lv_probing_wizard_get_setup_points(wizard_obj);
    float z_top = lv_probing_wizard_get_z_top(wizard_obj);
    lv_probing_wizard_point_float_t current_pos = mos_get_current_jogged_position();

    // Handle single Z-surface probing.
    if (action->type == ACTION_PROBE_Z_TOP) {
        LOGI(TAG, "Executing Z-Top probe.");
        // G6510.1: Probe a single surface. Called via M98 for async execution.
        // J, K: Starting XY position (machine coordinates).
        // L: Starting Z position (machine coordinates).
        // H4: Probe downward (Z- direction, surface code 4 = "top").
        // I: Max travel distance.
        // O: Overtravel allowance.
        // W: Work offset to store result (scratch WCS).
        // Move from a safe backoff height above the surface to the probe start
        // location so travel moves do not collide with the surface. Keep the
        // actual probe-to depth unchanged (I / probing distance remains the same).
        snprintf(gcode_buf, sizeof(gcode_buf),
             "G6510.1 W{%d} J{%.3f} K{%.3f} L{%.3f} H{4} I{%.3f} O{%.3f}",
                 PROBE_SCRATCH_WCS_OFFSET,
                 current_pos.x, current_pos.y, handler_state.machine->position[2] + Z_PROBING_BACKOFF_DISTANCE,
                 PROBE_DEFAULT_Z_DISTANCE, PROBE_DEFAULT_OVERTRAVEL);

        LOGI(TAG, "Sending probe gcode: %s", gcode_buf);
        handler_state.probe_state = PROBE_STATE_PENDING_Z_COMPLETE;
        handler_state.machine->probe(handler_state.machine, gcode_buf);

    // Handle complex XY probing routines.
    } else if (action->type == ACTION_PROBE_POINT) {
        LOGI(TAG, "Executing XY probe routine for mode %d.", lv_probing_wizard_get_mode(wizard_obj));

        bool is_inside = lv_probing_wizard_get_is_inside(wizard_obj);
        float probe_z = z_top - PROBE_XY_DEPTH_BELOW_Z_TOP;

        switch (lv_probing_wizard_get_mode(wizard_obj)) {
            case LV_PROBING_WIZARD_MODE_RECTANGLE: {
                if (!setup_points[0].is_set || !setup_points[1].is_set) {
                    LOGE(TAG, "Cannot start rectangle probe: setup points not set.");
                    return;
                }
                // Calculate center and dimensions from the two corner setup points.
                float center_x = (setup_points[0].x + setup_points[1].x) / 2.0f;
                float center_y = (setup_points[0].y + setup_points[1].y) / 2.0f;
                float dim_x = fabsf(setup_points[0].x - setup_points[1].x);
                float dim_y = fabsf(setup_points[0].y - setup_points[1].y);

                // G6502.1 for inside rectangle (pocket), G6503.1 for outside (block).
                // J, K: Approximate center (machine coords).
                // L: Safe Z height (machine coords, used for travel between probes).
                // Z: Absolute probe depth (machine coords, where probe contacts sidewall).
                // H: Approximate width (X dimension).
                // I: Approximate length (Y dimension).
                // T: Surface clearance distance.
                // O: Overtravel distance.
                // W: Work offset (scratch WCS).
                // Use a backoff above the detected top surface for travel moves
                // (L) while leaving the probe target depth (Z) at the requested
                // depth below `z_top` so probing accuracy is preserved.
                snprintf(gcode_buf, sizeof(gcode_buf),
                         "%s W{%d} J{%.3f} K{%.3f} L{%.3f} Z{%.3f} H{%.3f} I{%.3f} T{%.3f} O{%.3f}",
                         is_inside ? "G6502.1" : "G6503.1",
                         PROBE_SCRATCH_WCS_OFFSET,
                         center_x, center_y, z_top + Z_PROBING_BACKOFF_DISTANCE, probe_z,
                         dim_x, dim_y,
                         PROBE_XY_BACKOFF_DISTANCE, PROBE_DEFAULT_OVERTRAVEL);
                break;
            }
            case LV_PROBING_WIZARD_MODE_CIRCLE: {
                if (!setup_points[0].is_set || !setup_points[1].is_set) {
                    LOGE(TAG, "Cannot start circle probe: center and edge points not set.");
                    return;
                }
                // Calculate diameter from center (setup_points[0]) and edge (setup_points[1]).
                float dx = setup_points[1].x - setup_points[0].x;
                float dy = setup_points[1].y - setup_points[0].y;
                float diameter = 2.0f * sqrtf(dx * dx + dy * dy);
                if (diameter < 1.0f) {
                    LOGW(TAG, "Calculated circle diameter %.1f too small, using minimum 5mm.", diameter);
                    diameter = 5.0f;
                }
                LOGI(TAG, "Circle probe: calculated diameter=%.1f from center-edge distance.", diameter);

                // G6500.1 for inside circle (bore), G6501.1 for outside (boss).
                // J, K: Approximate center (machine coords).
                // L: Safe Z height (machine coords).
                // Z: Absolute probe depth (machine coords).
                // H: Approximate diameter.
                // T: Clearance distance (boss only - how far outside the boss edge to start).
                // O: Overtravel distance.
                // W: Work offset (scratch WCS).
                if (is_inside) {
                    // Bore (G6500.1): No T parameter needed (probes outward from center).
                    snprintf(gcode_buf, sizeof(gcode_buf),
                             "G6500.1 W{%d} J{%.3f} K{%.3f} L{%.3f} Z{%.3f} H{%.3f} O{%.3f}",
                             PROBE_SCRATCH_WCS_OFFSET,
                             setup_points[0].x, setup_points[0].y, z_top + Z_PROBING_BACKOFF_DISTANCE, probe_z,
                             diameter, PROBE_DEFAULT_OVERTRAVEL);
                } else {
                    // Boss (G6501.1): T parameter = clearance to start outside the boss.
                    snprintf(gcode_buf, sizeof(gcode_buf),
                             "G6501.1 W{%d} J{%.3f} K{%.3f} L{%.3f} Z{%.3f} H{%.3f} T{%.3f} O{%.3f}",
                             PROBE_SCRATCH_WCS_OFFSET,
                             setup_points[0].x, setup_points[0].y, z_top + Z_PROBING_BACKOFF_DISTANCE, probe_z,
                             diameter, PROBE_XY_BACKOFF_DISTANCE, PROBE_DEFAULT_OVERTRAVEL);
                }
                break;
            }
            case LV_PROBING_WIZARD_MODE_CORNER: {
                if (is_inside) {
                    LOGE(TAG, "Inside corner probing is not supported by MillenniumOS (G6509).");
                    return;
                }
                lv_probing_wizard_corner_t corner = lv_probing_wizard_get_corner_type(wizard_obj);
                if (corner == LV_PROBING_CORNER_NONE) {
                    LOGE(TAG, "Cannot start corner probe: corner not selected.");
                    return;
                }
                if (!setup_points[0].is_set) {
                    LOGE(TAG, "Cannot start corner probe: start position not set.");
                    return;
                }
                // Map the wizard's corner enum to the G-code's numeric value (0-3).
                // LV_PROBING_CORNER_FRONT_LEFT=1 -> N0, FRONT_RIGHT=2 -> N1,
                // BACK_LEFT=3 -> N2 (but MOS uses: 0=FL, 1=FR, 2=BR, 3=BL)
                int corner_n;
                switch (corner) {
                    case LV_PROBING_CORNER_FRONT_LEFT:  corner_n = 0; break;
                    case LV_PROBING_CORNER_FRONT_RIGHT: corner_n = 1; break;
                    case LV_PROBING_CORNER_BACK_RIGHT:  corner_n = 2; break;
                    case LV_PROBING_CORNER_BACK_LEFT:   corner_n = 3; break;
                    default: corner_n = 0; break;
                }

                // G6508.1: Probe an outside corner in "quick mode" (Q1 = 1 point per surface).
                // J, K: Starting position above the corner (machine coords, from jog step).
                // L: Safe Z height (machine coords).
                // Z: Absolute probe depth (machine coords).
                // N: Corner index (0=FL, 1=FR, 2=BR, 3=BL).
                // Q1: Quick mode (single-point per surface).
                // T: Surface clearance distance.
                // O: Overtravel distance.
                // W: Work offset (scratch WCS).
                snprintf(gcode_buf, sizeof(gcode_buf),
                         "G6508.1 W{%d} J{%.3f} K{%.3f} L{%.3f} Z{%.3f} N{%d} Q{1} T{%.3f} O{%.3f}",
                         PROBE_SCRATCH_WCS_OFFSET,
                         setup_points[0].x, setup_points[0].y,
                         z_top + Z_PROBING_BACKOFF_DISTANCE, probe_z,
                         corner_n,
                         PROBE_XY_BACKOFF_DISTANCE, PROBE_DEFAULT_OVERTRAVEL);
                break;
            }
        }

        handler_state.probe_state = PROBE_STATE_PENDING_XY_COMPLETE;
        LOGI(TAG, "Sending probe gcode: %s", gcode_buf);
        if (handler_state.machine && handler_state.machine->probe) {
            // _serial_send_gcode_impl (and all other transport send paths)
            // always appends '\n'. Adding one here would be a no-op and only
            // adds confusion. The real fix for command separation is a
            // serial_flush() after each line in _serial_send_gcode_impl.
            handler_state.machine->probe(handler_state.machine, gcode_buf);
        } else {
            LOGW(TAG, "No machine probe function available — probe not sent.");
        }
    }
}

static void mos_home_all_modal_event_handler(lv_event_t * e) {
    lv_obj_t *btn = lv_event_get_current_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_obj_t *mbox = lv_event_get_user_data(e);

    machine_interface_t * machine = lv_obj_get_user_data(mbox);
    const char *lbl = NULL;
    if (label) lbl = lv_label_get_text(label);
    if (machine && lbl && (lbl[0] == 'O' || lbl[0] == 'o')) {
        // machine->home_all(machine);
        LOGI(TAG, "Homing, then installing probe tool: T T{global.mosPTID}");
        handler_state.machine->send_gcode(handler_state.machine, "G28\nT T{global.mosPTID}", 0);
    }

    lv_msgbox_close(mbox);
}

static void mos_show_home_all_modal(machine_interface_t * machine) {
    lv_obj_t * mbox = lv_msgbox_create(lv_screen_active());
    lv_msgbox_add_title(mbox, "Home all?");
    lv_obj_set_user_data(mbox, machine);
    lv_msgbox_add_text(mbox, "One or more axes are not homed. Home all axes?");
    lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "OK");
    lv_obj_add_event_cb(ok, mos_home_all_modal_event_handler, LV_EVENT_CLICKED, mbox);
    lv_obj_t *close_btn = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(close_btn, mos_home_all_modal_event_handler, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);
}

/**
 * @brief Callback to apply the final calculated coordinates as a WCS origin.
 *
 * The result coordinates from probing are in machine coordinates. We use G10 L2
 * (absolute offset mode) to set the WCS origin, matching what MillenniumOS macros
 * use internally.
 *
 * The wcs_index from the wizard's apply button is the G10 P value:
 *   - Button matrix: G55=0, G56=1, G57=2, G58=3, G59=4
 *   - G10 P values: G54=P1, G55=P2, G56=P3, etc.
 *   - So wcs_index (already adjusted by caller: btnm_idx + 2) maps directly to P.
 */
static void mos_set_wcs_origin(lv_obj_t* wizard_obj, uint8_t wcs_index, float x, float y, float z, bool apply_z) {
    if (!handler_state.machine) return;

    char gcode_buf[MAX_GCODE_STR_LEN];
    char z_buf[32] = "";
    if (apply_z) {
        snprintf(z_buf, sizeof(z_buf), " Z%.4f", z);
    }

    // G10 L2: Set work coordinate offset using absolute machine coordinates.
    // P = WCS number (1=G54, 2=G55, etc.)
    // X, Y, Z = machine coordinate values that become the new WCS origin
    snprintf(gcode_buf, sizeof(gcode_buf), "G10 L2 P%d X%.4f Y%.4f%s",
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
 * @brief Machine state change callback.
 *
 * Keep internal probe-running flag in sync with the machine state.
 */
static void _mos_state_changed_cb(machine_interface_t* machine, void* user_data) {
    (void)user_data;
    bool running = (machine->machine_status == MACHINE_STATUS_RUNNING);
    if (handler_state.probe_was_running != running) { 
        LOGI(TAG, "Machine running state changed: was %d, now %d", handler_state.probe_was_running, running);
    }
    handler_state.probe_was_running = running;
}

/**
 * @brief Sends an M7601 query to re-read stored probing variables from the machine.
 * This acts as a fallback if log message parsing missed results.
 * The response will be parsed by subsequent log message callbacks.
 */
static void _mos_send_result_query(void) {
    if (!handler_state.machine) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "M7601 W%d", PROBE_SCRATCH_WCS_OFFSET);
    LOGI(TAG, "Sending result query: %s", cmd);
    handler_state.machine->send_gcode(handler_state.machine, cmd, 0);
}

/**
 * @brief Checks if M7601 fallback data is now sufficient to finalize the probe.
 *
 * Called after each successful variable parse when m7601_fallback_pending is true.
 * For corner mode we need parsed_cnr_pos; for rect/circle we need parsed_result.
 * If sufficient data is available, reports the final result to the wizard and
 * clears the fallback flag.
 */
static void _mos_try_complete_from_fallback(void) {
    if (!handler_state.wizard_obj) return;

    lv_probing_wizard_mode_t mode = lv_probing_wizard_get_mode(handler_state.wizard_obj);
    float final_x = NAN, final_y = NAN;

    if (mode == LV_PROBING_WIZARD_MODE_CORNER) {
        if (!isnan(handler_state.parsed_cnr_pos.x) && !isnan(handler_state.parsed_cnr_pos.y)) {
            final_x = handler_state.parsed_cnr_pos.x;
            final_y = handler_state.parsed_cnr_pos.y;
        }
    } else {
        if (!isnan(handler_state.parsed_result.x) && !isnan(handler_state.parsed_result.y)) {
            final_x = handler_state.parsed_result.x;
            final_y = handler_state.parsed_result.y;
        }
    }

    if (isnan(final_x) || isnan(final_y)) {
        return;  // Not enough data yet, wait for more variable echoes.
    }

    // We have the coordinates — finalize.
    handler_state.m7601_fallback_pending = false;

    lv_probing_wizard_details_t details = {0};
    details.dimensions.x = handler_state.parsed_dims.x;
    details.dimensions.y = handler_state.parsed_dims.y;
    details.radius = handler_state.parsed_radius;
    details.rotation = handler_state.parsed_rotation;

    LOGI(TAG, "M7601 fallback complete: X=%.4f Y=%.4f (dims: %.1fx%.1f, rad: %.1f, rot: %.1f)",
         final_x, final_y, details.dimensions.x, details.dimensions.y,
         details.radius, details.rotation);

    lv_probing_wizard_report_full_result_deferred(handler_state.wizard_obj, final_x, final_y, &details);
}

/**
 * @brief Callback for log messages from the machine.
 * Parses MillenniumOS global variable echoes and completion messages.
 *
 * Results are parsed from lines like:
 *   global.mosWPCtrPos[N]={X,Y}   — center position (rect, circle)
 *   global.mosWPCnrPos[N]={X,Y}   — corner position (outside corner)
 *   global.mosWPDims[N]={W,H}     — measured dimensions (rect)
 *   global.mosWPRad[N]=R           — radius (circle)
 *   global.mosWPDeg[N]=D           — rotation angle (rect, corner full mode)
 *   global.mosWPSfcAxis[N]=A       — probed axis letter (single surface)
 *   global.mosWPSfcPos[N]=V        — probed surface position (single surface)
 *   MillenniumOS: ...              — completion message => finalize
 */
static bool _mos_log_message_cb(machine_interface_t* machine, void* user_data, const char* message) {
    bool handled = false;
    const char* resp_str = message;
    int index;
    char axis;
    float f1, f2;
    const char *p;

    // Only parse logs if we are in a pending probe state — but still allow
    // completion messages to be observed even if state returned to idle.
    if (handler_state.probe_state != PROBE_STATE_IDLE) {
        handled = true;
    }

    // --- Parse global variable echoes ---
    if ((p = strstr(resp_str, "global.mosWPCtrPos[")) != NULL) {
        if (sscanf(p, "global.mosWPCtrPos[%*d]={%f,%f}", &f1, &f2) == 2) {
            handler_state.parsed_result.x = f1;
            handler_state.parsed_result.y = f2;
            LOGI(TAG, "Parsed center pos: X=%.4f, Y=%.4f", f1, f2);
            handled = true;
        }
    } else if ((p = strstr(resp_str, "global.mosWPCnrPos[")) != NULL) {
        if (sscanf(p, "global.mosWPCnrPos[%*d]={%f,%f}", &f1, &f2) == 2) {
            handler_state.parsed_cnr_pos.x = f1;
            handler_state.parsed_cnr_pos.y = f2;
            LOGI(TAG, "Parsed corner pos: X=%.4f, Y=%.4f", f1, f2);
            handled = true;
        }
    } else if ((p = strstr(resp_str, "global.mosWPDims[")) != NULL) {
        if (sscanf(p, "global.mosWPDims[%*d]={%f,%f}", &f1, &f2) == 2) {
            handler_state.parsed_dims.x = f1;
            handler_state.parsed_dims.y = f2;
            LOGI(TAG, "Parsed dims: W=%.4f, H=%.4f", f1, f2);
            handled = true;
        }
    } else if ((p = strstr(resp_str, "global.mosWPRad[")) != NULL) {
        if (sscanf(p, "global.mosWPRad[%*d]=%f", &f1) == 1) {
            handler_state.parsed_radius = f1;
            LOGI(TAG, "Parsed radius: R=%.4f", f1);
            handled = true;
        }
    } else if ((p = strstr(resp_str, "global.mosWPDeg[")) != NULL) {
        if (sscanf(p, "global.mosWPDeg[%*d]=%f", &f1) == 1) {
            handler_state.parsed_rotation = f1;
            LOGI(TAG, "Parsed rotation: %.4f deg", f1);
            handled = true;
        }
    } else if ((p = strstr(resp_str, "global.mosWPSfcAxis[")) != NULL) {
        if (sscanf(p, "global.mosWPSfcAxis[%d]=%c", &index, &axis) == 2) {
            if (index < MAX_PROBE_RESULTS) {
                handler_state.last_parsed_axis[index] = axis;
                LOGI(TAG, "Parsed probed axis index %d: %c", index, axis);
                handled = true;
            }
        }
    } else if ((p = strstr(resp_str, "global.mosWPSfcPos[")) != NULL) {
        if (sscanf(p, "global.mosWPSfcPos[%d]=%f", &index, &f1) == 2) {
            if (index < MAX_PROBE_RESULTS) {
                char reported_axis = handler_state.last_parsed_axis[index];
                LOGI(TAG, "Parsed probed position index %d for axis %c: %f", index, reported_axis, f1);
                if (reported_axis == 'Z') {
                    handler_state.parsed_z_result = f1;
                } else {
                    // Map axis char to index: X=0, Y=1, Z=2
                    int ax = -1;
                    if (reported_axis == 'X') ax = 0;
                    else if (reported_axis == 'Y') ax = 1;
                    else if (reported_axis == 'Z') ax = 2;
                    if (ax >= 0) {
                        if (isnan(handler_state.parsed_sfc_pos_by_axis[ax])) {
                            handler_state.parsed_sfc_pos_by_axis[ax] = f1;
                        } else {
                            // accumulate (average) multiple values
                            handler_state.parsed_sfc_pos_by_axis[ax] = (handler_state.parsed_sfc_pos_by_axis[ax] * handler_state.parsed_sfc_pos_count[ax] + f1) / (handler_state.parsed_sfc_pos_count[ax] + 1);
                        }
                        handler_state.parsed_sfc_pos_count[ax]++;
                        handler_state.parsed_sfc_pos = f1; // keep last for compatibility
                    } else {
                        handler_state.parsed_sfc_pos = f1;
                    }
                }
                handled = true;
            }
        }
    }

    // If an M7601 fallback query was sent, check after each variable parse
    // whether we now have enough data to finalize the probe result.
    if (handler_state.m7601_fallback_pending && handled) {
        _mos_try_complete_from_fallback();
    }

    // --- Completion messages ---
    if (strstr(resp_str, "MillenniumOS:") != NULL) {
        probe_fsm_state_t finished_probe_type = handler_state.probe_state;
        handler_state.probe_state = PROBE_STATE_IDLE;
        handler_state.probe_was_running = false;
        handler_state.m7601_fallback_pending = false;  // Normal completion supersedes any pending fallback
        handled = true;

        if (finished_probe_type == PROBE_STATE_PENDING_Z_COMPLETE) {
            LOGI(TAG, "Z-probe completion detected: %s", resp_str);
            float z_res = handler_state.parsed_z_result;
            if (isnan(z_res)) {
                LOGW(TAG, "Z Probe completed but no result was parsed. Using last machine Z.");
                z_res = machine->position[2];
            }
            lv_probing_wizard_set_z_top_deferred(handler_state.wizard_obj, z_res);
        }
        else if (finished_probe_type == PROBE_STATE_PENDING_XY_COMPLETE) {
            LOGI(TAG, "XY-probe completion detected: %s", resp_str);

            // Determine the final X, Y result based on what was probed.
            lv_probing_wizard_mode_t mode = lv_probing_wizard_get_mode(handler_state.wizard_obj);
            float final_x = NAN, final_y = NAN;

            if (mode == LV_PROBING_WIZARD_MODE_CORNER) {
                // Corner probe: prefer mosWPCnrPos, fall back to mosWPCtrPos
                if (!isnan(handler_state.parsed_cnr_pos.x)) {
                    final_x = handler_state.parsed_cnr_pos.x;
                    final_y = handler_state.parsed_cnr_pos.y;
                    LOGI(TAG, "Using corner pos: X=%.4f Y=%.4f", final_x, final_y);
                } else if (!isnan(handler_state.parsed_result.x)) {
                    final_x = handler_state.parsed_result.x;
                    final_y = handler_state.parsed_result.y;
                    LOGI(TAG, "Corner: falling back to center pos: X=%.4f Y=%.4f", final_x, final_y);
                }
            } else {
                // Rect / Circle: use mosWPCtrPos
                if (!isnan(handler_state.parsed_result.x)) {
                    final_x = handler_state.parsed_result.x;
                    final_y = handler_state.parsed_result.y;
                }
            }

            // If we don't have parsed center but do have surface probe positions
            // recorded per-axis (from mosWPSfcAxis/mosWPSfcPos), derive a final
            // X/Y by averaging the values reported for X and Y axes.
            if ((isnan(final_x) || isnan(final_y))) {
                bool have_x = handler_state.parsed_sfc_pos_count[0] > 0;
                bool have_y = handler_state.parsed_sfc_pos_count[1] > 0;
                if (have_x || have_y) {
                    if (have_x) final_x = handler_state.parsed_sfc_pos_by_axis[0];
                    if (have_y) final_y = handler_state.parsed_sfc_pos_by_axis[1];
                    LOGI(TAG, "Derived final from surface positions: X=%.4f Y=%.4f (have_x=%d have_y=%d)", final_x, final_y, have_x, have_y);
                }
            }

            if (isnan(final_x) || isnan(final_y)) {
                LOGW(TAG, "XY probe completed but result coordinates not parsed! Sending M7601 fallback query.");
                handler_state.m7601_fallback_pending = true;
                _mos_send_result_query();
                // M7601 response will echo the variables. The variable parsers above will
                // call _mos_try_complete_from_fallback() after each successful parse to
                // check if sufficient data has arrived to finalize the result.
                return handled;
            }

            // Build the details struct to pass to the wizard.
            lv_probing_wizard_details_t details = {0};
            details.dimensions.x = handler_state.parsed_dims.x;  // NAN if not applicable
            details.dimensions.y = handler_state.parsed_dims.y;
            details.radius = handler_state.parsed_radius;
            details.rotation = handler_state.parsed_rotation;

            LOGI(TAG, "Reporting full result: X=%.4f Y=%.4f (dims: %.1fx%.1f, rad: %.1f, rot: %.1f)",
                 final_x, final_y, details.dimensions.x, details.dimensions.y,
                 details.radius, details.rotation);

            // Use the new atomic deferred function that sets details + result + advances to complete.
            lv_probing_wizard_report_full_result_deferred(handler_state.wizard_obj, final_x, final_y, &details);
        }
    }

    return handled;
}
