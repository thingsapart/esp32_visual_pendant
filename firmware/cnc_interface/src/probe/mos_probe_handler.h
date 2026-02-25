// mos_probe_handler.h — MillenniumOS back-end for probe_api
//
// Wires a probe_api_ctx_t to a live machine_interface_t running MillenniumOS
// (Duet3D / RepRapFirmware with the MOS G-code extension macros).
//
// Relationship to mos_machine_handler.c
// ──────────────────────────────────────
// mos_machine_handler.c drives the *wizard* UI (lv_probing_wizard) using the
// same MOS G-code macros.  This module performs the same machine-level work
// but targets the new probe_api, which is completely decoupled from LVGL.
//
// Usage
// ─────
//   // Create handler state (one per machine / session).
//   mos_probe_handler_t hdlr;
//   mos_probe_handler_init(&hdlr, machine);
//
//   // Fill in the probe_api callback bundle.
//   probe_api_callbacks_t cbs = {0};
//   mos_probe_handler_fill_callbacks(&hdlr, &cbs);
//   cbs.on_rect_done   = my_on_rect_cb;
//   cbs.on_circle_done = my_on_circle_cb;
//   cbs.user_data      = my_ctx;
//
//   probe_api_init(&probe_ctx, &cbs);
//
//   // Start an operation.
//   probe_api_probe_pocket_params_t p = { ... };
//   probe_api_probe_pocket(&probe_ctx, &p);
//
//   // Machine logs arrive via the registered log_message callback; the
//   // handler parses MOS variable echoes and calls probe_api_report_*()
//   // when each phase completes.

#ifndef MOS_PROBE_HANDLER_H
#define MOS_PROBE_HANDLER_H

#include "probe_api.h"
#include "machine/machine_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Constants (mirrored from mos_machine_handler.c, kept in sync manually)
// ---------------------------------------------------------------------------

/// MOS scratch WCS used during probing to avoid polluting the user's WCS.
/// Results are in machine coordinates; the caller is responsible for applying
/// them to whichever WCS they choose (e.g. via G10 L2).
#define MOS_PROBE_SCRATCH_WCS_OFFSET     8

/// Clearance above the expected surface before starting a Z-probe move.
#define MOS_PROBE_Z_BACKOFF_MM           2.0f

/// Maximum downward travel for a Z-surface probe (mm).
#define MOS_PROBE_Z_MAX_TRAVEL_MM       50.0f

/// Default probe overtravel allowance.
#define MOS_PROBE_OVERTRAVEL_MM          2.0f

/// How far below z_surface to position the probe for XY side-wall probing.
#define MOS_PROBE_XY_DEPTH_BELOW_SURFACE 3.0f

/// Lateral backoff/clearance used by rectangular and boss probing macros.
#define MOS_PROBE_XY_BACKOFF_MM          5.0f

/// Feed rate for the slow positioning move (mm/min).
#define MOS_PROBE_SLOW_FEED_MMPM        300.0f

// ---------------------------------------------------------------------------
// Handler state
// ---------------------------------------------------------------------------

typedef enum {
    MOS_PROBE_FSM_IDLE              = 0,
    MOS_PROBE_FSM_PENDING_MOVE      = 1,   ///< Waiting for G0/G1 to finish
    MOS_PROBE_FSM_PENDING_Z         = 2,   ///< Waiting for G6510.1 to finish
    MOS_PROBE_FSM_PENDING_XY        = 3,   ///< Waiting for G650x.1 to finish
    MOS_PROBE_FSM_M7601_FALLBACK    = 4,   ///< Awaiting M7601 variable echoes
} mos_probe_fsm_t;

// Maximum number of MOS WP result slots we track.
#define MOS_PROBE_MAX_RESULTS 8

typedef struct {
    machine_interface_t *machine;   ///< The connected machine (not owned)
    probe_api_ctx_t     *api_ctx;   ///< The probe_api context we report back to

    mos_probe_fsm_t  fsm;
    bool             machine_was_running;  ///< tracks machine running state

    // Parsed MOS global variable values (reset before each operation).
    float parsed_ctr_x, parsed_ctr_y;  ///< mosWPCtrPos
    float parsed_cnr_x, parsed_cnr_y;  ///< mosWPCnrPos
    float parsed_dim_x, parsed_dim_y;  ///< mosWPDims
    float parsed_radius;               ///< mosWPRad
    float parsed_rotation;             ///< mosWPDeg
    float parsed_z_result;             ///< mosWPSfcPos (Z axis)
    float parsed_sfc_by_axis[3];       ///< per-axis surface positions [X,Y,Z]
    int   parsed_sfc_count[3];         ///< accumulation counts
    char  last_axis[MOS_PROBE_MAX_RESULTS]; ///< mosWPSfcAxis per index

    bool  m7601_fallback_pending;      ///< M7601 query sent, awaiting echoes

    // Log message callback bookkeeping — we register one slot on the machine.
    log_message_callback_t log_cb_slot;

    // State change callback slot.
    machine_change_callback_t state_cb_slot;
} mos_probe_handler_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/// Initialise the handler.  machine must remain valid for the handler's life.
/// api_ctx is set later via mos_probe_handler_fill_callbacks / attach.
void mos_probe_handler_init(mos_probe_handler_t *hdlr,
                             machine_interface_t *machine);

/// Detach from the machine (unregisters log/state callbacks).
void mos_probe_handler_deinit(mos_probe_handler_t *hdlr);

/// Fill in the command callbacks of a probe_api_callbacks_t with this
/// handler's implementations.  The caller may then add their own event
/// callbacks (on_rect_done, etc.) before calling probe_api_init().
/// Also stores api_ctx internally for use in the callbacks.
void mos_probe_handler_fill_callbacks(mos_probe_handler_t *hdlr,
                                       probe_api_ctx_t     *api_ctx,
                                       probe_api_callbacks_t *cbs);

/// Convenience: apply a WCS origin from a probe result.
/// wcs_p is the G10 P value (1=G54, 2=G55, …).
/// apply_z: if true also sets the Z origin.
void mos_probe_handler_apply_wcs(mos_probe_handler_t *hdlr,
                                  uint8_t wcs_p,
                                  float x, float y, float z,
                                  bool apply_z);

#ifdef __cplusplus
}
#endif

#endif // MOS_PROBE_HANDLER_H
