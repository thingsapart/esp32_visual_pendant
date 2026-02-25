// probe_api.c — Camera-assisted probe & move API implementation
//
// See probe_api.h for the full API contract and FSM description.

#include "probe_api.h"

#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline bool _is_running(const probe_api_ctx_t *ctx) {
    return ctx->status == PROBE_STATUS_RUNNING;
}

/// Fire the status change callback (if registered).
static void _notify_status(probe_api_ctx_t *ctx, probe_status_t status) {
    ctx->status = status;
    if (ctx->callbacks.on_status) {
        ctx->callbacks.on_status(ctx, status, ctx->callbacks.user_data);
    }
}

/// Transition to a failure/cancelled state and fire the error callback.
static void _fail(probe_api_ctx_t *ctx, probe_status_t status, const char *msg) {
    ctx->phase = PROBE_PHASE_ERROR;
    _notify_status(ctx, status);
    if (ctx->callbacks.on_error) {
        ctx->callbacks.on_error(ctx, status, msg, ctx->callbacks.user_data);
    }
}

/// Normalise two unordered corner points; compute center and extents.
static void _normalise_rect(probe_xy_t a, probe_xy_t b,
                             probe_xy_t *out_center,
                             float *out_width, float *out_height) {
    float x0 = (a.x < b.x) ? a.x : b.x;
    float y0 = (a.y < b.y) ? a.y : b.y;
    float x1 = (a.x > b.x) ? a.x : b.x;
    float y1 = (a.y > b.y) ? a.y : b.y;
    out_center->x = (x0 + x1) * 0.5f;
    out_center->y = (y0 + y1) * 0.5f;
    *out_width  = x1 - x0;
    *out_height = y1 - y0;
}

/// Euclidean distance from center to edge point.
static float _circle_radius(probe_xy_t center, probe_xy_t edge) {
    float dx = edge.x - center.x;
    float dy = edge.y - center.y;
    return sqrtf(dx * dx + dy * dy);
}

// ---------------------------------------------------------------------------
// FSM phase dispatchers
// ---------------------------------------------------------------------------

/// Dispatch a move-to command and enter MOVING_TO_XY.
static void _dispatch_move_to(probe_api_ctx_t *ctx, probe_xy_t xy, float safe_z) {
    ctx->target_xy = xy;
    ctx->safe_z    = safe_z;
    ctx->phase     = PROBE_PHASE_MOVING_TO_XY;
    if (ctx->callbacks.cmd_move_to) {
        ctx->callbacks.cmd_move_to(ctx, xy.x, xy.y, safe_z,
                                   ctx->callbacks.user_data);
    }
}

/// Dispatch a Z-probe command and enter PROBING_Z.
static void _dispatch_probe_z(probe_api_ctx_t *ctx) {
    ctx->phase = PROBE_PHASE_PROBING_Z;
    if (ctx->callbacks.cmd_probe_z) {
        ctx->callbacks.cmd_probe_z(ctx, ctx->probe_z_depth,
                                   ctx->callbacks.user_data);
    }
}

/// Dispatch the XY probe command and enter PROBING_XY.
/// Requires ctx->z_surface to already be valid.
static void _dispatch_probe_xy(probe_api_ctx_t *ctx) {
    ctx->phase = PROBE_PHASE_PROBING_XY;

    float probe_z_abs = ctx->z_surface - ctx->probe_xy_depth;

    switch (ctx->op) {
        case PROBE_OP_PROBE_POCKET:
        case PROBE_OP_PROBE_RECT: {
            probe_xy_t center;
            float width, height;
            _normalise_rect(ctx->rect_corner[0], ctx->rect_corner[1],
                            &center, &width, &height);
            if (ctx->callbacks.cmd_probe_rect) {
                ctx->callbacks.cmd_probe_rect(ctx,
                    center.x, center.y, width, height,
                    ctx->z_surface, probe_z_abs,
                    ctx->is_inside,
                    ctx->callbacks.user_data);
            }
            break;
        }
        case PROBE_OP_PROBE_BORE:
        case PROBE_OP_PROBE_BOSS: {
            float diameter = ctx->circle_radius * 2.0f;
            if (ctx->callbacks.cmd_probe_circle) {
                ctx->callbacks.cmd_probe_circle(ctx,
                    ctx->circle_center.x, ctx->circle_center.y,
                    diameter,
                    ctx->z_surface, probe_z_abs,
                    ctx->is_inside,
                    ctx->callbacks.user_data);
            }
            break;
        }
        default:
            _fail(ctx, PROBE_STATUS_ERROR, "unexpected op in _dispatch_probe_xy");
            break;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void probe_api_init(probe_api_ctx_t *ctx, const probe_api_callbacks_t *callbacks) {
    memset(ctx, 0, sizeof(*ctx));
    if (callbacks) {
        ctx->callbacks = *callbacks;
    }
    ctx->z_surface = (float)NAN;
    ctx->op        = PROBE_OP_NONE;
    ctx->status    = PROBE_STATUS_IDLE;
    ctx->phase     = PROBE_PHASE_IDLE;
}

void probe_api_cancel(probe_api_ctx_t *ctx) {
    if (ctx->status == PROBE_STATUS_RUNNING) {
        _fail(ctx, PROBE_STATUS_CANCELLED, "cancelled");
    }
    ctx->op     = PROBE_OP_NONE;
    ctx->phase  = PROBE_PHASE_IDLE;
    ctx->status = PROBE_STATUS_IDLE;
}

// ---------------------------------------------------------------------------
// Operation entry points
// ---------------------------------------------------------------------------

bool probe_api_move_to(probe_api_ctx_t *ctx,
                       const probe_api_move_to_params_t *params) {
    if (_is_running(ctx)) return false;
    ctx->op        = PROBE_OP_MOVE_TO;
    ctx->z_surface = (float)NAN;
    _notify_status(ctx, PROBE_STATUS_RUNNING);
    _dispatch_move_to(ctx, params->target_xy, params->safe_z);
    return true;
}

bool probe_api_probe_z(probe_api_ctx_t *ctx,
                       const probe_api_probe_z_params_t *params) {
    if (_is_running(ctx)) return false;
    ctx->op            = PROBE_OP_PROBE_Z;
    ctx->probe_z_depth = params->max_depth;
    ctx->z_surface     = (float)NAN;
    _notify_status(ctx, PROBE_STATUS_RUNNING);
    _dispatch_move_to(ctx, params->target_xy, params->safe_z);
    return true;
}

bool probe_api_probe_pocket(probe_api_ctx_t *ctx,
                             const probe_api_probe_pocket_params_t *params) {
    if (_is_running(ctx)) return false;
    ctx->op             = PROBE_OP_PROBE_POCKET;
    ctx->is_inside      = true;
    ctx->rect_corner[0] = params->corner_a;
    ctx->rect_corner[1] = params->corner_b;
    ctx->z_probe_xy     = params->z_probe_xy;
    ctx->safe_z         = params->safe_z;
    ctx->probe_z_depth  = params->max_z_depth;
    ctx->probe_xy_depth = params->xy_probe_depth;
    ctx->z_surface      = (float)NAN;
    _notify_status(ctx, PROBE_STATUS_RUNNING);
    _dispatch_move_to(ctx, params->z_probe_xy, params->safe_z);
    return true;
}

bool probe_api_probe_rect(probe_api_ctx_t *ctx,
                           const probe_api_probe_rect_params_t *params) {
    if (_is_running(ctx)) return false;
    ctx->op             = PROBE_OP_PROBE_RECT;
    ctx->is_inside      = false;
    ctx->rect_corner[0] = params->corner_a;
    ctx->rect_corner[1] = params->corner_b;
    ctx->z_probe_xy     = params->z_probe_xy;
    ctx->safe_z         = params->safe_z;
    ctx->probe_z_depth  = params->max_z_depth;
    ctx->probe_xy_depth = params->xy_probe_depth;
    ctx->z_surface      = (float)NAN;
    _notify_status(ctx, PROBE_STATUS_RUNNING);
    _dispatch_move_to(ctx, params->z_probe_xy, params->safe_z);
    return true;
}

bool probe_api_probe_bore(probe_api_ctx_t *ctx,
                           const probe_api_probe_bore_params_t *params) {
    if (_is_running(ctx)) return false;
    ctx->op             = PROBE_OP_PROBE_BORE;
    ctx->is_inside      = true;
    ctx->circle_center  = params->center;
    ctx->circle_radius  = _circle_radius(params->center, params->edge);
    ctx->z_probe_xy     = params->z_probe_xy;
    ctx->safe_z         = params->safe_z;
    ctx->probe_z_depth  = params->max_z_depth;
    ctx->probe_xy_depth = params->xy_probe_depth;
    ctx->z_surface      = (float)NAN;
    _notify_status(ctx, PROBE_STATUS_RUNNING);
    _dispatch_move_to(ctx, params->z_probe_xy, params->safe_z);
    return true;
}

bool probe_api_probe_boss(probe_api_ctx_t *ctx,
                           const probe_api_probe_boss_params_t *params) {
    if (_is_running(ctx)) return false;
    ctx->op             = PROBE_OP_PROBE_BOSS;
    ctx->is_inside      = false;
    ctx->circle_center  = params->center;
    ctx->circle_radius  = _circle_radius(params->center, params->edge);
    ctx->z_probe_xy     = params->z_probe_xy;
    ctx->safe_z         = params->safe_z;
    ctx->probe_z_depth  = params->max_z_depth;
    ctx->probe_xy_depth = params->xy_probe_depth;
    ctx->z_surface      = (float)NAN;
    _notify_status(ctx, PROBE_STATUS_RUNNING);
    _dispatch_move_to(ctx, params->z_probe_xy, params->safe_z);
    return true;
}

// ---------------------------------------------------------------------------
// Report-back functions
// ---------------------------------------------------------------------------

// FSM for feature probing (pocket/rect/bore/boss):
//
//  probe_api_probe_*()
//      │  _dispatch_move_to(z_probe_xy)   phase=MOVING_TO_XY, z_surface=NAN
//      ▼
//  report_move_done(success)
//      │  z_surface==NAN  →  _dispatch_probe_z()    phase=PROBING_Z
//      ▼
//  report_z_done(success, z)
//      │  z_surface = z
//      │  _dispatch_move_to(feature_center, z_surface)  phase=MOVING_TO_XY
//      ▼
//  report_move_done(success)   ← second call
//      │  z_surface!=NAN  →  _dispatch_probe_xy()   phase=PROBING_XY
//      ▼
//  report_xy_done(...)
//      │  phase=COMPLETE, fire on_rect_done / on_circle_done

void probe_api_report_move_done(probe_api_ctx_t *ctx, bool success) {
    if (ctx->phase != PROBE_PHASE_MOVING_TO_XY) return;

    if (!success) {
        _fail(ctx, PROBE_STATUS_ERROR, "move failed");
        return;
    }

    switch (ctx->op) {
        case PROBE_OP_MOVE_TO: {
            ctx->phase = PROBE_PHASE_COMPLETE;
            ctx->result_move.target_xy = ctx->target_xy;
            ctx->result_move.z_surface = (float)NAN;
            _notify_status(ctx, PROBE_STATUS_DONE);
            if (ctx->callbacks.on_move_done) {
                ctx->callbacks.on_move_done(ctx, &ctx->result_move,
                                             ctx->callbacks.user_data);
            }
            break;
        }
        case PROBE_OP_PROBE_Z: {
            // Moved to XY → now probe Z.
            _dispatch_probe_z(ctx);
            break;
        }
        case PROBE_OP_PROBE_POCKET:
        case PROBE_OP_PROBE_RECT:
        case PROBE_OP_PROBE_BORE:
        case PROBE_OP_PROBE_BOSS: {
            if (isnan(ctx->z_surface)) {
                // First move (to z_probe_xy) complete → probe Z.
                _dispatch_probe_z(ctx);
            } else {
                // Second move (to feature center) complete → probe XY.
                _dispatch_probe_xy(ctx);
            }
            break;
        }
        default:
            _fail(ctx, PROBE_STATUS_ERROR, "unknown op in report_move_done");
            break;
    }
}

void probe_api_report_z_done(probe_api_ctx_t *ctx, bool success, float z) {
    if (ctx->phase != PROBE_PHASE_PROBING_Z) return;

    if (!success || isnan(z)) {
        _fail(ctx, PROBE_STATUS_ERROR, "Z probe failed");
        return;
    }

    ctx->z_surface = z;

    switch (ctx->op) {
        case PROBE_OP_PROBE_Z: {
            ctx->phase = PROBE_PHASE_COMPLETE;
            ctx->result_move.target_xy = ctx->target_xy;
            ctx->result_move.z_surface = z;
            _notify_status(ctx, PROBE_STATUS_DONE);
            if (ctx->callbacks.on_move_done) {
                ctx->callbacks.on_move_done(ctx, &ctx->result_move,
                                             ctx->callbacks.user_data);
            }
            break;
        }
        case PROBE_OP_PROBE_POCKET:
        case PROBE_OP_PROBE_RECT: {
            // Z known → move to the approximate feature center.
            probe_xy_t center;
            float w, h;
            _normalise_rect(ctx->rect_corner[0], ctx->rect_corner[1],
                            &center, &w, &h);
            _dispatch_move_to(ctx, center, ctx->z_surface);
            break;
        }
        case PROBE_OP_PROBE_BORE:
        case PROBE_OP_PROBE_BOSS: {
            _dispatch_move_to(ctx, ctx->circle_center, ctx->z_surface);
            break;
        }
        default:
            _fail(ctx, PROBE_STATUS_ERROR, "unknown op in report_z_done");
            break;
    }
}

void probe_api_report_xy_done(probe_api_ctx_t *ctx,
                               bool success,
                               probe_xy_t center,
                               const probe_xy_t *dims,
                               float diameter,
                               float rotation_deg) {
    if (ctx->phase != PROBE_PHASE_PROBING_XY) return;

    if (!success) {
        _fail(ctx, PROBE_STATUS_ERROR, "XY probe failed");
        return;
    }

    ctx->phase = PROBE_PHASE_COMPLETE;

    switch (ctx->op) {
        case PROBE_OP_PROBE_POCKET:
        case PROBE_OP_PROBE_RECT: {
            ctx->result_rect.center       = center;
            ctx->result_rect.z_surface    = ctx->z_surface;
            ctx->result_rect.width        = dims ? dims->x : (float)NAN;
            ctx->result_rect.height       = dims ? dims->y : (float)NAN;
            ctx->result_rect.rotation_deg = rotation_deg;
            _notify_status(ctx, PROBE_STATUS_DONE);
            if (ctx->callbacks.on_rect_done) {
                ctx->callbacks.on_rect_done(ctx, &ctx->result_rect,
                                             ctx->callbacks.user_data);
            }
            break;
        }
        case PROBE_OP_PROBE_BORE:
        case PROBE_OP_PROBE_BOSS: {
            ctx->result_circle.center    = center;
            ctx->result_circle.diameter  = diameter;
            ctx->result_circle.z_surface = ctx->z_surface;
            _notify_status(ctx, PROBE_STATUS_DONE);
            if (ctx->callbacks.on_circle_done) {
                ctx->callbacks.on_circle_done(ctx, &ctx->result_circle,
                                               ctx->callbacks.user_data);
            }
            break;
        }
        default:
            _fail(ctx, PROBE_STATUS_ERROR, "unknown op in report_xy_done");
            break;
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

probe_op_t     probe_api_get_op(const probe_api_ctx_t *ctx)     { return ctx->op; }
probe_status_t probe_api_get_status(const probe_api_ctx_t *ctx) { return ctx->status; }
probe_phase_t  probe_api_get_phase(const probe_api_ctx_t *ctx)  { return ctx->phase; }

const probe_result_move_t *probe_api_get_result_move(const probe_api_ctx_t *ctx) {
    return &ctx->result_move;
}
const probe_result_rect_t *probe_api_get_result_rect(const probe_api_ctx_t *ctx) {
    return &ctx->result_rect;
}
const probe_result_circle_t *probe_api_get_result_circle(const probe_api_ctx_t *ctx) {
    return &ctx->result_circle;
}
