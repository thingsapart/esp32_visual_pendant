// probe_api.h — Standalone camera-assisted probe & move API
//
// Provides six high-level CNC positioning / probing operations driven by
// physical (machine) coordinates obtained from the camera view:
//
//   probe_api_move_to        — safe-Z retract → rapid to XY
//   probe_api_probe_z        — safe-Z retract → rapid to XY → probe Z surface
//   probe_api_probe_pocket   — Z-probe at reference point → probe inside rectangular pocket
//   probe_api_probe_rect     — Z-probe at reference point → probe outside rectangle (block)
//   probe_api_probe_bore     — Z-probe at reference point → probe inside circular bore
//   probe_api_probe_boss     — Z-probe at reference point → probe outside circular boss
//
// Design notes
// ─────────────
// • No LVGL dependency — pure C, no UI widgets.
// • All machine commands are issued via caller-supplied callbacks so the
//   caller controls transport (e.g. mos_probe_handler.c wraps MillenniumOS
//   macros; a test stub can count invocations).
// • The API is event-driven:  start an operation, return immediately, receive
//   progress/completion via the registered hook callbacks from any context.
// • Thread-safety: the API state is stored in a single probe_api_ctx_t that
//   the caller allocates.  Never share one context across threads without
//   external locking.
//
// Typical usage
// ─────────────
//   probe_api_ctx_t ctx;
//   probe_api_init(&ctx, &callbacks);
//
//   probe_api_move_to_params_t p = { .xy = {x, y}, .safe_z = 5.0f };
//   probe_api_move_to(&ctx, &p);
//   // ... later, when machine is idle:
//   probe_api_report_move_done(&ctx);   // (called by mos_probe_handler)

#ifndef PROBE_API_H
#define PROBE_API_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

typedef struct probe_api_ctx_t probe_api_ctx_t;

// ---------------------------------------------------------------------------
// Basic geometry types (no LVGL dependency)
// ---------------------------------------------------------------------------

typedef struct {
    float x;
    float y;
} probe_xy_t;

typedef struct {
    float x;
    float y;
    float z;
} probe_xyz_t;

// ---------------------------------------------------------------------------
// Operation types (mirrors the six public entry points)
// ---------------------------------------------------------------------------

typedef enum {
    PROBE_OP_NONE        = 0,
    PROBE_OP_MOVE_TO     = 1,
    PROBE_OP_PROBE_Z     = 2,
    PROBE_OP_PROBE_POCKET = 3,
    PROBE_OP_PROBE_RECT  = 4,
    PROBE_OP_PROBE_BORE  = 5,
    PROBE_OP_PROBE_BOSS  = 6,
} probe_op_t;

// ---------------------------------------------------------------------------
// Operation status
// ---------------------------------------------------------------------------

typedef enum {
    PROBE_STATUS_IDLE      = 0,  ///< No operation in progress
    PROBE_STATUS_RUNNING   = 1,  ///< Operation in progress
    PROBE_STATUS_DONE      = 2,  ///< Last operation completed successfully
    PROBE_STATUS_ERROR     = 3,  ///< Last operation failed
    PROBE_STATUS_CANCELLED = 4,  ///< Last operation was cancelled
} probe_status_t;

// ---------------------------------------------------------------------------
// Result structures
// ---------------------------------------------------------------------------

/// Result for move-to and probe-Z operations.
typedef struct {
    probe_xy_t  target_xy;     ///< The commanded XY destination
    float       z_surface;     ///< Measured Z surface (NAN if not probed)
} probe_result_move_t;

/// Result for rect/pocket probing.
typedef struct {
    probe_xy_t  center;        ///< Measured center of the rectangle
    float       width;         ///< Measured width  (X dimension) in mm, NAN if not available
    float       height;        ///< Measured height (Y dimension) in mm, NAN if not available
    float       rotation_deg;  ///< Measured rotation in degrees, NAN if not available
    float       z_surface;     ///< Measured Z of the reference surface
} probe_result_rect_t;

/// Result for bore/boss probing.
typedef struct {
    probe_xy_t  center;        ///< Measured centre of the circle
    float       diameter;      ///< Measured diameter in mm, NAN if not available
    float       z_surface;     ///< Measured Z of the reference surface
} probe_result_circle_t;

// ---------------------------------------------------------------------------
// Command callbacks — called BY the API to request machine actions
//
// These are "fire and forget" from the API's perspective: the callback is
// expected to dispatch the command asynchronously and, once done, report back
// via one of the probe_api_report_* functions.
// ---------------------------------------------------------------------------

/// Request a safe-Z retract followed by a rapid move to (x, y).
/// When the move completes call probe_api_report_move_done().
typedef void (*probe_cmd_move_to_cb_t)(probe_api_ctx_t *ctx,
                                       float x, float y, float safe_z,
                                       void *user_data);

/// Request a Z-surface probe at the current XY.
/// max_depth is the maximum distance to travel downward from the current
/// machine Z before declaring an error.
/// When done call probe_api_report_z_done().
typedef void (*probe_cmd_probe_z_cb_t)(probe_api_ctx_t *ctx,
                                       float max_depth,
                                       void *user_data);

/// Request a full rectangular pocket (is_inside=true) or block (is_inside=false)
/// XY probe.
/// center_x/y   — approximate center in machine coords
/// width/height — approximate feature dimensions
/// safe_z       — safe travel height during probing
/// probe_z      — absolute Z depth at which to probe the side-walls
/// When done call probe_api_report_rect_done().
typedef void (*probe_cmd_probe_rect_cb_t)(probe_api_ctx_t *ctx,
                                          float center_x, float center_y,
                                          float width, float height,
                                          float safe_z, float probe_z,
                                          bool is_inside,
                                          void *user_data);

/// Request a full bore (is_inside=true) or boss (is_inside=false) XY probe.
/// center_x/y — approximate center in machine coords
/// diameter   — approximate diameter
/// safe_z     — safe travel height during probing
/// probe_z    — absolute Z depth at which to probe the side-wall
/// When done call probe_api_report_circle_done().
typedef void (*probe_cmd_probe_circle_cb_t)(probe_api_ctx_t *ctx,
                                             float center_x, float center_y,
                                             float diameter,
                                             float safe_z, float probe_z,
                                             bool is_inside,
                                             void *user_data);

// ---------------------------------------------------------------------------
// Event / progress callbacks — called BY the API to notify the caller
// ---------------------------------------------------------------------------

/// Called whenever the operation status changes.
/// Inspect ctx->status and ctx->op for details.
typedef void (*probe_event_status_cb_t)(probe_api_ctx_t *ctx,
                                         probe_status_t status,
                                         void *user_data);

/// Called when a move-to or probe-Z operation completes successfully.
typedef void (*probe_event_move_done_cb_t)(probe_api_ctx_t *ctx,
                                            const probe_result_move_t *result,
                                            void *user_data);

/// Called when a rectangular probe (pocket or block) completes successfully.
typedef void (*probe_event_rect_done_cb_t)(probe_api_ctx_t *ctx,
                                            const probe_result_rect_t *result,
                                            void *user_data);

/// Called when a circular probe (bore or boss) completes successfully.
typedef void (*probe_event_circle_done_cb_t)(probe_api_ctx_t *ctx,
                                              const probe_result_circle_t *result,
                                              void *user_data);

/// Called when any operation fails or is cancelled (status = ERROR / CANCELLED).
/// message is a short human-readable description (may be NULL).
typedef void (*probe_event_error_cb_t)(probe_api_ctx_t *ctx,
                                        probe_status_t status,
                                        const char *message,
                                        void *user_data);

// ---------------------------------------------------------------------------
// Callback bundle — passed at init time
// ---------------------------------------------------------------------------

typedef struct {
    // --- Command callbacks (required for real operation) ---
    probe_cmd_move_to_cb_t      cmd_move_to;
    probe_cmd_probe_z_cb_t      cmd_probe_z;
    probe_cmd_probe_rect_cb_t   cmd_probe_rect;
    probe_cmd_probe_circle_cb_t cmd_probe_circle;

    // --- Event callbacks (optional — pass NULL to ignore) ---
    probe_event_status_cb_t       on_status;
    probe_event_move_done_cb_t    on_move_done;
    probe_event_rect_done_cb_t    on_rect_done;
    probe_event_circle_done_cb_t  on_circle_done;
    probe_event_error_cb_t        on_error;

    /// Common user_data pointer forwarded to every callback.
    void *user_data;
} probe_api_callbacks_t;

// ---------------------------------------------------------------------------
// Internal FSM phases (not part of the public API, but exposed here so that
// handler implementations can read the phase if needed)
// ---------------------------------------------------------------------------

typedef enum {
    PROBE_PHASE_IDLE            = 0,
    PROBE_PHASE_MOVING_TO_XY    = 1,   ///< Executing move-to command
    PROBE_PHASE_PROBING_Z       = 2,   ///< Executing Z-surface probe
    PROBE_PHASE_PROBING_XY      = 3,   ///< Executing XY (rect/circle) probe
    PROBE_PHASE_COMPLETE        = 4,
    PROBE_PHASE_ERROR           = 5,
} probe_phase_t;

// ---------------------------------------------------------------------------
// Context — allocate one per logical "probe session"
// ---------------------------------------------------------------------------

struct probe_api_ctx_t {
    probe_api_callbacks_t callbacks;   ///< Copied at init

    // Current state
    probe_op_t      op;
    probe_status_t  status;
    probe_phase_t   phase;

    // Per-operation parameters (filled by the start functions)
    probe_xy_t   target_xy;      ///< Commanded XY position
    float        safe_z;         ///< Safe retract Z for this operation
    float        probe_z_depth;  ///< For probe-Z: max distance to travel down
    float        probe_xy_depth; ///< Depth below z_surface for sidewall probing
    probe_xy_t   rect_corner[2]; ///< Two corner points of the feature rect (raw, may be unordered)
    probe_xy_t   circle_center;  ///< Approximate circle center
    float        circle_radius;  ///< Approximate circle radius
    probe_xy_t   z_probe_xy;     ///< XY position for the Z-surface reference probe

    // Intermediate results accumulated across phases
    float        z_surface;      ///< Measured Z surface (NAN until known)

    // Final result storage (valid after PROBE_PHASE_COMPLETE)
    probe_result_move_t   result_move;
    probe_result_rect_t   result_rect;
    probe_result_circle_t result_circle;

    /// Whether is_inside (pocket/bore) or outside (rect/boss)
    bool is_inside;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/// Initialize (or re-initialize) a context.  Safe to call multiple times.
void probe_api_init(probe_api_ctx_t *ctx, const probe_api_callbacks_t *callbacks);

/// Cancel any in-progress operation and reset to IDLE.
void probe_api_cancel(probe_api_ctx_t *ctx);

// ---------------------------------------------------------------------------
// Operation entry points
// ---------------------------------------------------------------------------

typedef struct {
    probe_xy_t target_xy;    ///< Destination X,Y in machine coords
    float      safe_z;       ///< Safe Z to retract to before moving (WCS Z=0 typical)
} probe_api_move_to_params_t;

/// Move the toolhead to the target XY (retracts to safe_z first).
/// Returns false if another operation is already running.
bool probe_api_move_to(probe_api_ctx_t *ctx,
                       const probe_api_move_to_params_t *params);

typedef struct {
    probe_xy_t target_xy;    ///< Move to this XY before probing Z
    float      safe_z;       ///< Safe Z retract height
    float      max_depth;    ///< Max distance to probe downward from current Z
} probe_api_probe_z_params_t;

/// Move to XY then probe the Z surface.
/// Returns false if another operation is already running.
bool probe_api_probe_z(probe_api_ctx_t *ctx,
                       const probe_api_probe_z_params_t *params);

typedef struct {
    /// Two corner points defining the rectangle (any order — API normalises).
    probe_xy_t corner_a;
    probe_xy_t corner_b;
    /// XY position to probe the reference Z surface (outside/above the pocket).
    probe_xy_t z_probe_xy;
    float safe_z;        ///< Safe Z retract used while travelling
    float max_z_depth;   ///< Max travel for the Z-surface probe
    float xy_probe_depth; ///< Distance below z_surface to probe sidewalls
} probe_api_probe_pocket_params_t;

/// Probe inside a rectangular pocket.
/// Moves to z_probe_xy, probes Z, then runs the pocket (inside rectangle) probe.
bool probe_api_probe_pocket(probe_api_ctx_t *ctx,
                             const probe_api_probe_pocket_params_t *params);

typedef struct {
    probe_xy_t corner_a;
    probe_xy_t corner_b;
    probe_xy_t z_probe_xy;   ///< On top of / above the feature
    float safe_z;
    float max_z_depth;
    float xy_probe_depth;
} probe_api_probe_rect_params_t;

/// Probe outside a rectangle (block / stub).
/// Same as pocket but probes from outside.
bool probe_api_probe_rect(probe_api_ctx_t *ctx,
                           const probe_api_probe_rect_params_t *params);

typedef struct {
    probe_xy_t center;          ///< Approximate bore center
    probe_xy_t edge;            ///< Any point on the bore circumference
    probe_xy_t z_probe_xy;      ///< Reference Z-probe point (outside the bore)
    float safe_z;
    float max_z_depth;
    float xy_probe_depth;       ///< Depth below z_surface to probe inner wall
} probe_api_probe_bore_params_t;

/// Probe inside a bore (cylindrical hole).
bool probe_api_probe_bore(probe_api_ctx_t *ctx,
                           const probe_api_probe_bore_params_t *params);

typedef struct {
    probe_xy_t center;          ///< Approximate boss center
    probe_xy_t edge;            ///< Any point on the boss circumference
    probe_xy_t z_probe_xy;      ///< On top of the boss
    float safe_z;
    float max_z_depth;
    float xy_probe_depth;       ///< Depth below z_surface to probe outer wall
} probe_api_probe_boss_params_t;

/// Probe outside a boss (cylindrical protrusion).
bool probe_api_probe_boss(probe_api_ctx_t *ctx,
                           const probe_api_probe_boss_params_t *params);

// ---------------------------------------------------------------------------
// Report-back functions (called by the machine handler / transport layer)
//
// All of these are safe to call from any task/context; they update internal
// state and then fire the appropriate event callback.
// ---------------------------------------------------------------------------

/// The previously requested move completed (or failed with success=false).
void probe_api_report_move_done(probe_api_ctx_t *ctx, bool success);

/// The Z-surface probe completed.  z is the measured surface in machine coords.
/// Pass NAN and success=false to report a probe error.
void probe_api_report_z_done(probe_api_ctx_t *ctx, bool success, float z);

/// The XY rect/circle probe completed.
/// center    — measured center in machine coords (ignored if !success)
/// dims      — measured width/height for rect, NAN for circle  (may be NULL)
/// diameter  — measured diameter for circle, NAN for rect       (0.0 = ignore)
/// rotation  — measured rotation in degrees, NAN if not available
void probe_api_report_xy_done(probe_api_ctx_t *ctx,
                               bool success,
                               probe_xy_t center,
                               const probe_xy_t *dims,
                               float diameter,
                               float rotation_deg);

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

probe_op_t     probe_api_get_op(const probe_api_ctx_t *ctx);
probe_status_t probe_api_get_status(const probe_api_ctx_t *ctx);
probe_phase_t  probe_api_get_phase(const probe_api_ctx_t *ctx);

/// Returns the last move result.  Valid after PROBE_OP_MOVE_TO or
/// PROBE_OP_PROBE_Z completes with PROBE_STATUS_DONE.
const probe_result_move_t   *probe_api_get_result_move(const probe_api_ctx_t *ctx);

/// Returns the last rectangle result.  Valid after PROBE_OP_PROBE_POCKET or
/// PROBE_OP_PROBE_RECT completes with PROBE_STATUS_DONE.
const probe_result_rect_t   *probe_api_get_result_rect(const probe_api_ctx_t *ctx);

/// Returns the last circle result.  Valid after PROBE_OP_PROBE_BORE or
/// PROBE_OP_PROBE_BOSS completes with PROBE_STATUS_DONE.
const probe_result_circle_t *probe_api_get_result_circle(const probe_api_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // PROBE_API_H
