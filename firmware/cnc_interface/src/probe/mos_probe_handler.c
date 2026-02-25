// mos_probe_handler.c — MillenniumOS back-end for probe_api
//
// See mos_probe_handler.h for the design overview.
//
// Machine command flow
// ────────────────────
// probe_api fires cmd_move_to  → we send "G0 Z<safe> F<feed>\nG1 X<x> Y<y> F<feed>"
//                                   then wait for machine to return to IDLE.
// probe_api fires cmd_probe_z  → we send "G6510.1 ..." and wait for "MillenniumOS:"
// probe_api fires cmd_probe_rect/circle → G650x.1 and wait for "MillenniumOS:"
//
// Result parsing mirrors mos_machine_handler.c exactly — MOS echoes its
// global variables as the macro runs.  We accumulate them and report back
// via probe_api_report_z_done / probe_api_report_xy_done.

#include "mos_probe_handler.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// ---------------------------------------------------------------------------
// Logging (reuse whatever macros the project uses)
// ---------------------------------------------------------------------------
#if defined(ARDUINO) || defined(ESP32_HW)
#  include "debug.h"   /* defines LOGI / LOGW / LOGE / LOGD for ESP32 firmware */
#else
#  include <stdio.h>
#  define LOGI(tag, fmt, ...) printf("[I][" tag "] " fmt "\n", ##__VA_ARGS__)
#  define LOGW(tag, fmt, ...) printf("[W][" tag "] " fmt "\n", ##__VA_ARGS__)
#  define LOGE(tag, fmt, ...) printf("[E][" tag "] " fmt "\n", ##__VA_ARGS__)
#  define LOGV(tag, fmt, ...) (void)0
#endif

static const char *TAG = "mos_probe";

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static bool  _log_message_cb(machine_interface_t *machine,
                              void *user_data, const char *message);
static void  _state_changed_cb(machine_interface_t *machine, void *user_data);
static void  _reset_parsed(mos_probe_handler_t *hdlr);
static void  _try_complete_from_fallback(mos_probe_handler_t *hdlr);
static void  _send_m7601_query(mos_probe_handler_t *hdlr);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void mos_probe_handler_init(mos_probe_handler_t *hdlr,
                             machine_interface_t *machine) {
    assert(hdlr);
    memset(hdlr, 0, sizeof(*hdlr));
    hdlr->machine = machine;
    hdlr->fsm     = MOS_PROBE_FSM_IDLE;
    _reset_parsed(hdlr);
}

void mos_probe_handler_deinit(mos_probe_handler_t *hdlr) {
    if (!hdlr) return;
    // Null out our callback slots.  The machine's slot arrays are not
    // compacted — simply zeroing the function pointer disables the slot.
    hdlr->log_cb_slot.cb_fn   = NULL;
    hdlr->state_cb_slot.cb_fn = NULL;
    hdlr->machine  = NULL;
    hdlr->api_ctx  = NULL;
    hdlr->fsm      = MOS_PROBE_FSM_IDLE;
}

// ---------------------------------------------------------------------------
// Internal: reset parsed MOS variables
// ---------------------------------------------------------------------------

static void _reset_parsed(mos_probe_handler_t *hdlr) {
    hdlr->parsed_ctr_x   = (float)NAN;
    hdlr->parsed_ctr_y   = (float)NAN;
    hdlr->parsed_cnr_x   = (float)NAN;
    hdlr->parsed_cnr_y   = (float)NAN;
    hdlr->parsed_dim_x   = (float)NAN;
    hdlr->parsed_dim_y   = (float)NAN;
    hdlr->parsed_radius  = (float)NAN;
    hdlr->parsed_rotation = (float)NAN;
    hdlr->parsed_z_result = (float)NAN;
    memset(hdlr->parsed_sfc_by_axis, 0, sizeof(hdlr->parsed_sfc_by_axis));
    memset(hdlr->parsed_sfc_count, 0, sizeof(hdlr->parsed_sfc_count));
    memset(hdlr->last_axis, 0, sizeof(hdlr->last_axis));
    for (int i = 0; i < 3; i++) {
        hdlr->parsed_sfc_by_axis[i] = (float)NAN;
        hdlr->parsed_sfc_count[i]   = 0;
    }
    hdlr->m7601_fallback_pending = false;
}

// ---------------------------------------------------------------------------
// Command callbacks (installed into probe_api_callbacks_t)
// ---------------------------------------------------------------------------

// --- cmd_move_to -----------------------------------------------------------

static void _cmd_move_to(probe_api_ctx_t *ctx,
                          float x, float y, float safe_z,
                          void *user_data) {
    mos_probe_handler_t *hdlr = (mos_probe_handler_t *)user_data;
    if (!hdlr->machine) {
        LOGW(TAG, "cmd_move_to: no machine");
        probe_api_report_move_done(ctx, false);
        return;
    }

    _reset_parsed(hdlr);
    hdlr->fsm = MOS_PROBE_FSM_PENDING_MOVE;

    // Retract to safe Z (G0 absolute), then slow-feed to XY.
    char gcode[128];
    snprintf(gcode, sizeof(gcode),
             "G90\nG0 Z%.4f F%.0f\nG1 X%.4f Y%.4f F%.0f",
             safe_z, MOS_PROBE_SLOW_FEED_MMPM,
             x, y,   MOS_PROBE_SLOW_FEED_MMPM);
    LOGI(TAG, "cmd_move_to → %s", gcode);
    hdlr->machine->send_gcode(hdlr->machine, gcode, 0);
    // Report is fired from _state_changed_cb when machine returns to IDLE.
}

// --- cmd_probe_z -----------------------------------------------------------

static void _cmd_probe_z(probe_api_ctx_t *ctx,
                          float max_depth,
                          void *user_data) {
    mos_probe_handler_t *hdlr = (mos_probe_handler_t *)user_data;
    if (!hdlr->machine) {
        LOGW(TAG, "cmd_probe_z: no machine");
        probe_api_report_z_done(ctx, false, (float)NAN);
        return;
    }

    _reset_parsed(hdlr);
    hdlr->fsm = MOS_PROBE_FSM_PENDING_Z;

    float cur_z = hdlr->machine->position[2];
    float start_z = cur_z + MOS_PROBE_Z_BACKOFF_MM;
    float travel = (max_depth > 0.0f) ? max_depth : MOS_PROBE_Z_MAX_TRAVEL_MM;

    // G6510.1: probe a single surface in the Z- direction.
    // J, K: current XY (machine coords); L: start Z; H4: downward surface;
    // I: max travel; O: overtravel; W: scratch WCS.
    char gcode[256];
    snprintf(gcode, sizeof(gcode),
             "G6510.1 W{%d} J{%.4f} K{%.4f} L{%.4f} H{4} I{%.4f} O{%.4f}",
             MOS_PROBE_SCRATCH_WCS_OFFSET,
             hdlr->machine->position[0], hdlr->machine->position[1],
             start_z, travel, MOS_PROBE_OVERTRAVEL_MM);
    LOGI(TAG, "cmd_probe_z → %s", gcode);
    hdlr->machine->probe(hdlr->machine, gcode);
}

// --- cmd_probe_rect --------------------------------------------------------

static void _cmd_probe_rect(probe_api_ctx_t *ctx,
                             float center_x, float center_y,
                             float width, float height,
                             float safe_z, float probe_z,
                             bool is_inside,
                             void *user_data) {
    mos_probe_handler_t *hdlr = (mos_probe_handler_t *)user_data;
    if (!hdlr->machine) {
        LOGW(TAG, "cmd_probe_rect: no machine");
        probe_xy_t bad = {(float)NAN, (float)NAN};
        probe_api_report_xy_done(ctx, false, bad, NULL, (float)NAN, (float)NAN);
        return;
    }

    _reset_parsed(hdlr);
    hdlr->fsm = MOS_PROBE_FSM_PENDING_XY;

    // G6502.1 = inside (pocket), G6503.1 = outside (block).
    // J, K: approx center; L: safe Z (travel height); Z: probe depth;
    // H: width; I: height; T: clearance; O: overtravel; W: scratch WCS.
    char gcode[256];
    snprintf(gcode, sizeof(gcode),
             "%s W{%d} J{%.4f} K{%.4f} L{%.4f} Z{%.4f} H{%.4f} I{%.4f} T{%.4f} O{%.4f}",
             is_inside ? "G6502.1" : "G6503.1",
             MOS_PROBE_SCRATCH_WCS_OFFSET,
             center_x, center_y,
             safe_z + MOS_PROBE_Z_BACKOFF_MM,
             probe_z,
             width, height,
             MOS_PROBE_XY_BACKOFF_MM, MOS_PROBE_OVERTRAVEL_MM);
    LOGI(TAG, "cmd_probe_rect → %s", gcode);
    hdlr->machine->probe(hdlr->machine, gcode);
}

// --- cmd_probe_circle ------------------------------------------------------

static void _cmd_probe_circle(probe_api_ctx_t *ctx,
                               float center_x, float center_y,
                               float diameter,
                               float safe_z, float probe_z,
                               bool is_inside,
                               void *user_data) {
    mos_probe_handler_t *hdlr = (mos_probe_handler_t *)user_data;
    if (!hdlr->machine) {
        LOGW(TAG, "cmd_probe_circle: no machine");
        probe_xy_t bad = {(float)NAN, (float)NAN};
        probe_api_report_xy_done(ctx, false, bad, NULL, (float)NAN, (float)NAN);
        return;
    }

    _reset_parsed(hdlr);
    hdlr->fsm = MOS_PROBE_FSM_PENDING_XY;

    char gcode[256];
    if (is_inside) {
        // G6500.1: bore — probes outward from center, no T (clearance) needed.
        snprintf(gcode, sizeof(gcode),
                 "G6500.1 W{%d} J{%.4f} K{%.4f} L{%.4f} Z{%.4f} H{%.4f} O{%.4f}",
                 MOS_PROBE_SCRATCH_WCS_OFFSET,
                 center_x, center_y,
                 safe_z + MOS_PROBE_Z_BACKOFF_MM,
                 probe_z,
                 diameter, MOS_PROBE_OVERTRAVEL_MM);
    } else {
        // G6501.1: boss — T = clearance outside the boss edge.
        snprintf(gcode, sizeof(gcode),
                 "G6501.1 W{%d} J{%.4f} K{%.4f} L{%.4f} Z{%.4f} H{%.4f} T{%.4f} O{%.4f}",
                 MOS_PROBE_SCRATCH_WCS_OFFSET,
                 center_x, center_y,
                 safe_z + MOS_PROBE_Z_BACKOFF_MM,
                 probe_z,
                 diameter, MOS_PROBE_XY_BACKOFF_MM, MOS_PROBE_OVERTRAVEL_MM);
    }
    LOGI(TAG, "cmd_probe_circle → %s", gcode);
    hdlr->machine->probe(hdlr->machine, gcode);
}

// ---------------------------------------------------------------------------
// State change callback — detects move completion (IDLE after RUNNING)
// ---------------------------------------------------------------------------

static void _state_changed_cb(machine_interface_t *machine, void *user_data) {
    mos_probe_handler_t *hdlr = (mos_probe_handler_t *)user_data;
    if (!hdlr || !hdlr->api_ctx) return;

    bool running = (machine->machine_status == MACHINE_STATUS_RUNNING);

    if (hdlr->fsm == MOS_PROBE_FSM_PENDING_MOVE) {
        if (hdlr->machine_was_running && !running) {
            // Machine just went from RUNNING → IDLE after a move.
            LOGI(TAG, "_state_changed_cb: move completed (running→idle)");
            hdlr->fsm               = MOS_PROBE_FSM_IDLE;
            hdlr->machine_was_running = false;
            probe_api_report_move_done(hdlr->api_ctx, true);
            return;
        }
    }

    hdlr->machine_was_running = running;
}

// ---------------------------------------------------------------------------
// Log message callback — parses MOS global variable echoes and "MillenniumOS:"
// completion messages.  Mirrors mos_machine_handler.c _mos_log_message_cb.
// ---------------------------------------------------------------------------

static bool _log_message_cb(machine_interface_t *machine,
                              void *user_data, const char *message) {
    mos_probe_handler_t *hdlr = (mos_probe_handler_t *)user_data;
    if (!hdlr || !hdlr->api_ctx) return false;

    // Only intercept messages when we are waiting for a probe result.
    if (hdlr->fsm != MOS_PROBE_FSM_PENDING_Z   &&
        hdlr->fsm != MOS_PROBE_FSM_PENDING_XY  &&
        hdlr->fsm != MOS_PROBE_FSM_M7601_FALLBACK) {
        return false;
    }

    bool handled = false;
    int  index;
    char axis;
    float f1, f2;
    const char *p;

    // --- Parse MOS global variable echoes ---

    if ((p = strstr(message, "global.mosWPCtrPos[")) != NULL) {
        if (sscanf(p, "global.mosWPCtrPos[%*d]={%f,%f}", &f1, &f2) == 2) {
            hdlr->parsed_ctr_x = f1;
            hdlr->parsed_ctr_y = f2;
            LOGI(TAG, "Parsed CtrPos: X=%.4f Y=%.4f", f1, f2);
            handled = true;
        }
    } else if ((p = strstr(message, "global.mosWPCnrPos[")) != NULL) {
        if (sscanf(p, "global.mosWPCnrPos[%*d]={%f,%f}", &f1, &f2) == 2) {
            hdlr->parsed_cnr_x = f1;
            hdlr->parsed_cnr_y = f2;
            LOGI(TAG, "Parsed CnrPos: X=%.4f Y=%.4f", f1, f2);
            handled = true;
        }
    } else if ((p = strstr(message, "global.mosWPDims[")) != NULL) {
        if (sscanf(p, "global.mosWPDims[%*d]={%f,%f}", &f1, &f2) == 2) {
            hdlr->parsed_dim_x = f1;
            hdlr->parsed_dim_y = f2;
            LOGI(TAG, "Parsed Dims: W=%.4f H=%.4f", f1, f2);
            handled = true;
        }
    } else if ((p = strstr(message, "global.mosWPRad[")) != NULL) {
        if (sscanf(p, "global.mosWPRad[%*d]=%f", &f1) == 1) {
            hdlr->parsed_radius = f1;
            LOGI(TAG, "Parsed Rad: R=%.4f", f1);
            handled = true;
        }
    } else if ((p = strstr(message, "global.mosWPDeg[")) != NULL) {
        if (sscanf(p, "global.mosWPDeg[%*d]=%f", &f1) == 1) {
            hdlr->parsed_rotation = f1;
            LOGI(TAG, "Parsed Deg: %.4f", f1);
            handled = true;
        }
    } else if ((p = strstr(message, "global.mosWPSfcAxis[")) != NULL) {
        if (sscanf(p, "global.mosWPSfcAxis[%d]=%c", &index, &axis) == 2) {
            if (index >= 0 && index < MOS_PROBE_MAX_RESULTS) {
                hdlr->last_axis[index] = axis;
                LOGI(TAG, "Parsed SfcAxis[%d]=%c", index, axis);
                handled = true;
            }
        }
    } else if ((p = strstr(message, "global.mosWPSfcPos[")) != NULL) {
        if (sscanf(p, "global.mosWPSfcPos[%d]=%f", &index, &f1) == 2) {
            if (index >= 0 && index < MOS_PROBE_MAX_RESULTS) {
                char reported_axis = hdlr->last_axis[index];
                LOGI(TAG, "Parsed SfcPos[%d] axis=%c val=%.4f", index, reported_axis, f1);
                if (reported_axis == 'Z') {
                    hdlr->parsed_z_result = f1;
                } else {
                    int ax = -1;
                    if      (reported_axis == 'X') ax = 0;
                    else if (reported_axis == 'Y') ax = 1;
                    else if (reported_axis == 'Z') ax = 2;
                    if (ax >= 0) {
                        int cnt = hdlr->parsed_sfc_count[ax];
                        if (isnan(hdlr->parsed_sfc_by_axis[ax])) {
                            hdlr->parsed_sfc_by_axis[ax] = f1;
                        } else {
                            hdlr->parsed_sfc_by_axis[ax] =
                                (hdlr->parsed_sfc_by_axis[ax] * cnt + f1) / (cnt + 1);
                        }
                        hdlr->parsed_sfc_count[ax]++;
                    }
                }
                handled = true;
            }
        }
    }

    // After each successful variable parse, check if M7601 fallback is done.
    if (hdlr->m7601_fallback_pending && handled) {
        _try_complete_from_fallback(hdlr);
    }

    // --- Completion messages ---
    if (strstr(message, "MillenniumOS:") != NULL) {
        mos_probe_fsm_t finished_fsm = hdlr->fsm;
        hdlr->fsm                   = MOS_PROBE_FSM_IDLE;
        hdlr->machine_was_running   = false;
        hdlr->m7601_fallback_pending = false;
        handled = true;

        if (finished_fsm == MOS_PROBE_FSM_PENDING_Z) {
            LOGI(TAG, "Z-probe completion: %s", message);
            float z = hdlr->parsed_z_result;
            if (isnan(z)) {
                LOGW(TAG, "Z probe done but no result parsed; using machine Z.");
                z = hdlr->machine->position[2];
            }
            probe_api_report_z_done(hdlr->api_ctx, true, z);

        } else if (finished_fsm == MOS_PROBE_FSM_PENDING_XY) {
            LOGI(TAG, "XY-probe completion: %s", message);

            // Prefer CtrPos, fall back to per-axis surface positions.
            float final_x = hdlr->parsed_ctr_x;
            float final_y = hdlr->parsed_ctr_y;

            // If CtrPos was never echoed, try per-axis surface positions.
            if (isnan(final_x) || isnan(final_y)) {
                bool have_x = hdlr->parsed_sfc_count[0] > 0;
                bool have_y = hdlr->parsed_sfc_count[1] > 0;
                if (have_x) final_x = hdlr->parsed_sfc_by_axis[0];
                if (have_y) final_y = hdlr->parsed_sfc_by_axis[1];
                LOGI(TAG, "Using sfc_by_axis: X=%.4f Y=%.4f (have_x=%d have_y=%d)",
                     final_x, final_y, have_x, have_y);
            }

            if (isnan(final_x) || isnan(final_y)) {
                // Send M7601 fallback query to re-read stored variables.
                LOGW(TAG, "XY probe done but result not parsed — sending M7601 fallback.");
                hdlr->m7601_fallback_pending = true;
                hdlr->fsm = MOS_PROBE_FSM_M7601_FALLBACK;
                _send_m7601_query(hdlr);
                return handled;
            }

            probe_xy_t center = {final_x, final_y};
            probe_xy_t dims   = {hdlr->parsed_dim_x, hdlr->parsed_dim_y};
            bool has_dims = !isnan(hdlr->parsed_dim_x) && !isnan(hdlr->parsed_dim_y);

            LOGI(TAG, "Reporting XY done: center=(%.4f,%.4f) dims=(%.4f,%.4f) "
                      "rad=%.4f rot=%.4f",
                 center.x, center.y,
                 has_dims ? dims.x : 0.f, has_dims ? dims.y : 0.f,
                 hdlr->parsed_radius, hdlr->parsed_rotation);

            probe_api_report_xy_done(hdlr->api_ctx, true,
                                      center,
                                      has_dims ? &dims : NULL,
                                      hdlr->parsed_radius * 2.0f,   // diameter
                                      hdlr->parsed_rotation);
        }
    }

    return handled;
}

// ---------------------------------------------------------------------------
// M7601 fallback helpers
// ---------------------------------------------------------------------------

static void _send_m7601_query(mos_probe_handler_t *hdlr) {
    if (!hdlr->machine) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "M7601 W%d", MOS_PROBE_SCRATCH_WCS_OFFSET);
    LOGI(TAG, "M7601 fallback query: %s", cmd);
    hdlr->machine->send_gcode(hdlr->machine, cmd, 0);
}

static void _try_complete_from_fallback(mos_probe_handler_t *hdlr) {
    if (!hdlr->api_ctx) return;

    float final_x = hdlr->parsed_ctr_x;
    float final_y = hdlr->parsed_ctr_y;

    if (isnan(final_x) || isnan(final_y)) return;  // Not enough yet.

    hdlr->m7601_fallback_pending = false;
    hdlr->fsm = MOS_PROBE_FSM_IDLE;

    probe_xy_t center = {final_x, final_y};
    probe_xy_t dims   = {hdlr->parsed_dim_x, hdlr->parsed_dim_y};
    bool has_dims = !isnan(hdlr->parsed_dim_x) && !isnan(hdlr->parsed_dim_y);

    LOGI(TAG, "M7601 fallback complete: center=(%.4f,%.4f)", final_x, final_y);

    probe_api_report_xy_done(hdlr->api_ctx, true,
                              center,
                              has_dims ? &dims : NULL,
                              hdlr->parsed_radius * 2.0f,
                              hdlr->parsed_rotation);
}

// ---------------------------------------------------------------------------
// Public: fill callback bundle and register with machine
// ---------------------------------------------------------------------------

void mos_probe_handler_fill_callbacks(mos_probe_handler_t  *hdlr,
                                       probe_api_ctx_t      *api_ctx,
                                       probe_api_callbacks_t *cbs) {
    assert(hdlr && api_ctx && cbs);

    hdlr->api_ctx = api_ctx;

    // Install command callbacks; user_data = hdlr so we can reach machine etc.
    cbs->cmd_move_to      = _cmd_move_to;
    cbs->cmd_probe_z      = _cmd_probe_z;
    cbs->cmd_probe_rect   = _cmd_probe_rect;
    cbs->cmd_probe_circle = _cmd_probe_circle;
    cbs->user_data        = hdlr;

    // Register machine callbacks to receive state changes and log messages.
    // We store the filled slots so deinit() can null them out.
    if (hdlr->machine) {
        machine_interface_add_state_change_cb(hdlr->machine, hdlr, _state_changed_cb);
        machine_interface_add_log_message_cb(hdlr->machine, hdlr, _log_message_cb);
    }
}

// ---------------------------------------------------------------------------
// Utility: apply WCS origin
// ---------------------------------------------------------------------------

void mos_probe_handler_apply_wcs(mos_probe_handler_t *hdlr,
                                  uint8_t wcs_p,
                                  float x, float y, float z,
                                  bool apply_z) {
    if (!hdlr || !hdlr->machine) return;
    char gcode[96];
    if (apply_z) {
        snprintf(gcode, sizeof(gcode), "G10 L2 P%d X%.4f Y%.4f Z%.4f",
                 wcs_p, x, y, z);
    } else {
        snprintf(gcode, sizeof(gcode), "G10 L2 P%d X%.4f Y%.4f", wcs_p, x, y);
    }
    LOGI(TAG, "Apply WCS: %s", gcode);
    hdlr->machine->send_gcode(hdlr->machine, gcode, 0);
}
