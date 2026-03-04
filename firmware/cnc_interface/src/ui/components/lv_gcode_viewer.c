/**
 * @file lv_gcode_viewer.c
 * @brief LVGL G-Code viewer widget implementation.
 *
 * This is the main rendering component.  It follows the same pattern as
 * lv_cam_positioning.c:
 *  - private struct via calloc → lv_obj_set_user_data
 *  - draw callback on LV_EVENT_DRAW_MAIN_END
 *  - cleanup via LV_EVENT_DELETE
 *
 * The draw callback performs:
 *  1. Background fill
 *  2. Grid lines
 *  3. Machine limits rectangle
 *  4. Toolpath segments (coloured by type, dimmed when "done")
 *  5. WCS origin marker
 *  6. Current spindle position crosshair
 *  7. View label badge
 */

#include "lv_gcode_viewer.h"
#include "gcode_viewer/gcode_parser.h"
#include "gcode_viewer/gcode_sim.h"
#include "gcode_viewer/gcode_view.h"
#include "machine/machine_interface.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Private data
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    lv_obj_t            *root;        /**< the widget returned from _create */
    machine_interface_t *machine;     /**< live machine state (may be NULL) */

    /* Toolpath data */
    gc_segbuf_t         *segbuf;      /**< heap-allocated segment ring buffer */
    gc_sim_state_t       sim;         /**< current simulator modal state */

    /* View */
    gc_view_t            view;

    /* Feature flags */
    bool                 show_grid;
    bool                 show_limits;
    bool                 show_position;
    bool                 show_wcs_origin;
    bool                 auto_fit_pending;

    /* Cached machine state (updated on callback) */
    float                mpos[3];         /**< machine position */
    float                wpos[3];         /**< WCS position */
    float                wcs_offset[3];   /**< mpos - wpos = WCS offset */
    float                axis_min[3];
    float                axis_max[3];
    bool                 has_limits;
} lv_gcview_priv_t;

/* ─── get_priv helper ──────────────────────────────────────────────────── */

static inline lv_gcview_priv_t *get_priv(lv_obj_t *obj) {
    return (lv_gcview_priv_t *)lv_obj_get_user_data(obj);
}

/* Forward-declare view-mode helper to allow calling it from API functions
 * defined earlier in the file. */
static gc_view_mode_t lv_to_gc_view_mode(lv_gcode_viewer_view_t v);

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal: colour helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline lv_color_t seg_color(gc_move_type_t type) {
    switch (type) {
    case GCMOVE_RAPID:   return lv_color_hex(GCVIEW_COL_RAPID);
    case GCMOVE_LINEAR:  return lv_color_hex(GCVIEW_COL_FEED);
    case GCMOVE_ARC_CW:  return lv_color_hex(GCVIEW_COL_ARC_CW);
    case GCMOVE_ARC_CCW: return lv_color_hex(GCVIEW_COL_ARC_CCW);
    default:             return lv_color_hex(GCVIEW_COL_FEED);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal: draw primitives
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Draw a single line segment between two screen points. */
static void draw_line(lv_layer_t *layer,
                      gc_pt2_t p1, gc_pt2_t p2,
                      lv_color_t color, lv_opa_t opa, int width)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa   = opa;
    dsc.width = width;
    dsc.p1.x  = p1.x;  dsc.p1.y = p1.y;
    dsc.p2.x  = p2.x;  dsc.p2.y = p2.y;
    lv_draw_line(layer, &dsc);
}

/** Draw a dashed line (for rapids). */
static void draw_dashed_line(lv_layer_t *layer,
                             gc_pt2_t p1, gc_pt2_t p2,
                             lv_color_t color, lv_opa_t opa, int width)
{
    /* Simple dash: divide into N sub-segments, draw odd ones */
    float dx = (float)(p2.x - p1.x);
    float dy = (float)(p2.y - p1.y);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 2.0f) return;

    int dash_len = 6;  /* px */
    int gap_len  = 4;
    int cycle    = dash_len + gap_len;
    int steps    = (int)(len / (float)cycle);
    if (steps < 1) steps = 1;

    float ux = dx / len;
    float uy = dy / len;

    for (int i = 0; i <= steps; i++) {
        float s0 = (float)(i * cycle);
        float s1 = s0 + (float)dash_len;
        if (s1 > len) s1 = len;

        gc_pt2_t a = { (int16_t)(p1.x + ux * s0), (int16_t)(p1.y + uy * s0) };
        gc_pt2_t b = { (int16_t)(p1.x + ux * s1), (int16_t)(p1.y + uy * s1) };
        draw_line(layer, a, b, color, opa, width);
    }
}

/** Draw a crosshair at screen position. */
static void draw_crosshair(lv_layer_t *layer, gc_pt2_t c,
                           lv_color_t color, lv_opa_t opa, int arm)
{
    gc_pt2_t l = { (int16_t)(c.x - arm), c.y };
    gc_pt2_t r = { (int16_t)(c.x + arm), c.y };
    gc_pt2_t t = { c.x, (int16_t)(c.y - arm) };
    gc_pt2_t b = { c.x, (int16_t)(c.y + arm) };
    draw_line(layer, l, r, color, opa, 2);
    draw_line(layer, t, b, color, opa, 2);

    /* Central dot */
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = color;
    rd.bg_opa   = opa;
    rd.radius   = LV_RADIUS_CIRCLE;
    lv_area_t dot = { c.x - 2, c.y - 2, c.x + 2, c.y + 2 };
    lv_draw_rect(layer, &rd, &dot);
}

/** Draw an axis origin indicator (short coloured arrows). */
static void draw_origin_axes(lv_layer_t *layer, const gc_view_t *view,
                             gc_vec3_t origin, int arm_px)
{
    gc_pt2_t o;
    gc_view_project(view, origin, &o);

    /* Project the axis endpoints */
    float arm_mm = (float)arm_px / view->ppm;

    gc_vec3_t ax = { origin.x + arm_mm, origin.y, origin.z };
    gc_vec3_t ay = { origin.x, origin.y + arm_mm, origin.z };
    gc_vec3_t az = { origin.x, origin.y, origin.z + arm_mm };

    gc_pt2_t px, py, pz;
    gc_view_project(view, ax, &px);
    gc_view_project(view, ay, &py);
    gc_view_project(view, az, &pz);

    /* Only draw axes that are visible in this projection */
    int dx, dy;

    dx = px.x - o.x;  dy = px.y - o.y;
    if (dx * dx + dy * dy > 4)
        draw_line(layer, o, px, lv_color_hex(GCVIEW_COL_AXIS_X), LV_OPA_COVER, 2);

    dx = py.x - o.x;  dy = py.y - o.y;
    if (dx * dx + dy * dy > 4)
        draw_line(layer, o, py, lv_color_hex(GCVIEW_COL_AXIS_Y), LV_OPA_COVER, 2);

    dx = pz.x - o.x;  dy = pz.y - o.y;
    if (dx * dx + dy * dy > 4)
        draw_line(layer, o, pz, lv_color_hex(GCVIEW_COL_AXIS_Z), LV_OPA_COVER, 2);
}

/** Draw a text badge in the corner. */
static void draw_badge(lv_layer_t *layer, const lv_area_t *vp,
                       const char *text, bool top_right)
{
    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_hex(0x000000);
    bg.bg_opa   = LV_OPA_50;
    bg.radius   = 4;

    int w = 90, h = 20;
    lv_area_t badge;
    if (top_right) {
        badge.x1 = vp->x2 - w - 4;
        badge.y1 = vp->y1 + 4;
    } else {
        badge.x1 = vp->x1 + 4;
        badge.y1 = vp->y2 - h - 4;
    }
    badge.x2 = badge.x1 + w;
    badge.y2 = badge.y1 + h;
    lv_draw_rect(layer, &bg, &badge);

    lv_draw_label_dsc_t lbl;
    lv_draw_label_dsc_init(&lbl);
    lbl.color      = lv_color_white();
    lbl.opa        = LV_OPA_80;
    lbl.align      = LV_TEXT_ALIGN_CENTER;
    lbl.text       = text;
    lbl.text_local = 0;
    lv_area_t ta = { badge.x1 + 2, badge.y1 + 2, badge.x2 - 2, badge.y2 - 2 };
    lv_draw_label(layer, &lbl, &ta);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal: draw arc tessellation
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Tessellate and draw an arc segment as a polyline. */
static void draw_arc_segment(lv_layer_t *layer, const gc_view_t *view,
                             const gc_segment_t *seg,
                             lv_color_t color, lv_opa_t opa)
{
    /* Select the two in-plane axes for the arc */
    float cx, cy, sx, sy, ex, ey;
    float sz, ez;  /* out-of-plane axis for helix interpolation */

    switch (seg->plane) {
    case GCPLANE_XY:
        cx = seg->from.x + seg->ijk.x;
        cy = seg->from.y + seg->ijk.y;
        sx = seg->from.x;  sy = seg->from.y;
        ex = seg->to.x;    ey = seg->to.y;
        sz = seg->from.z;  ez = seg->to.z;
        break;
    case GCPLANE_XZ:
        cx = seg->from.x + seg->ijk.x;
        cy = seg->from.z + seg->ijk.z;
        sx = seg->from.x;  sy = seg->from.z;
        ex = seg->to.x;    ey = seg->to.z;
        sz = seg->from.y;  ez = seg->to.y;
        break;
    case GCPLANE_YZ:
        cx = seg->from.y + seg->ijk.y;
        cy = seg->from.z + seg->ijk.z;
        sx = seg->from.y;  sy = seg->from.z;
        ex = seg->to.y;    ey = seg->to.z;
        sz = seg->from.x;  ez = seg->to.x;
        break;
    }

    /* Compute start and end angles */
    float start_angle = atan2f(sy - cy, sx - cx);
    float end_angle   = atan2f(ey - cy, ex - cx);

    /* Determine sweep direction */
    float sweep;
    if (seg->type == GCMOVE_ARC_CW) {
        /* CW: sweep must be negative */
        sweep = end_angle - start_angle;
        if (sweep > 0) sweep -= 2.0f * (float)M_PI;
        if (sweep > -1e-6f) sweep = -2.0f * (float)M_PI;  /* full circle */
    } else {
        /* CCW: sweep must be positive */
        sweep = end_angle - start_angle;
        if (sweep < 0) sweep += 2.0f * (float)M_PI;
        if (sweep < 1e-6f) sweep = 2.0f * (float)M_PI;
    }

    float radius = sqrtf((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
    int n = GCVIEW_ARC_RESOLUTION;

    gc_pt2_t prev;
    gc_vec3_t wp;

    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        float angle = start_angle + sweep * t;
        float px = cx + radius * cosf(angle);
        float py = cy + radius * sinf(angle);
        float pz = sz + (ez - sz) * t;  /* helix interpolation */

        /* Map back to 3D */
        switch (seg->plane) {
        case GCPLANE_XY: wp = gc_vec3(px, py, pz); break;
        case GCPLANE_XZ: wp = gc_vec3(px, pz, py); break;
        case GCPLANE_YZ: wp = gc_vec3(pz, px, py); break;
        }

        gc_pt2_t cur;
        gc_view_project(view, wp, &cur);

        if (i > 0)
            draw_line(layer, prev, cur, color, opa, GCVIEW_LINE_WIDTH);

        prev = cur;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal: draw machine limits
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_limits(lv_layer_t *layer, const gc_view_t *view,
                        const float *amin, const float *amax)
{
    /* Draw a rectangle/box outline of the machine volume.
     * For orthographic views this is a rectangle; for iso it's a projected box. */

    /* Build 8 corners of the volume */
    gc_vec3_t corners[8];
    for (int i = 0; i < 8; i++) {
        corners[i].x = (i & 1) ? amax[0] : amin[0];
        corners[i].y = (i & 2) ? amax[1] : amin[1];
        corners[i].z = (i & 4) ? amax[2] : amin[2];
    }

    gc_pt2_t pts[8];
    for (int i = 0; i < 8; i++)
        gc_view_project(view, corners[i], &pts[i]);

    lv_color_t col = lv_color_hex(GCVIEW_COL_LIMITS);
    lv_opa_t opa = GCVIEW_OPA_LIMITS;

    /* Draw the 12 edges of the bounding box */
    int edges[][2] = {
        {0,1}, {1,3}, {3,2}, {2,0},   /* bottom face */
        {4,5}, {5,7}, {7,6}, {6,4},   /* top face */
        {0,4}, {1,5}, {2,6}, {3,7},   /* vertical edges */
    };

    for (int i = 0; i < 12; i++)
        draw_line(layer, pts[edges[i][0]], pts[edges[i][1]], col, opa, 1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal: grid draw callback adapter
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    lv_layer_t *layer;
} grid_draw_ctx_t;

static void grid_line_cb(bool is_major, gc_pt2_t p1, gc_pt2_t p2, void *ctx) {
    grid_draw_ctx_t *gc = (grid_draw_ctx_t *)ctx;
    lv_color_t col = is_major ? lv_color_hex(GCVIEW_COL_GRID_MAJOR)
                              : lv_color_hex(GCVIEW_COL_GRID);
    lv_opa_t opa = is_major ? (lv_opa_t)(GCVIEW_OPA_GRID + 40) : GCVIEW_OPA_GRID;
    draw_line(gc->layer, p1, p2, col, opa, GCVIEW_GRID_LINE_WIDTH);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main draw callback
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_cb(lv_event_t *e) {
    lv_gcview_priv_t *priv = (lv_gcview_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;

    lv_layer_t *layer = lv_event_get_layer(e);
    if (!layer) return;

    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_area_t vp;
    lv_obj_get_coords(obj, &vp);

    /* Update viewport in case of resize */
    int16_t w = (int16_t)(vp.x2 - vp.x1 + 1);
    int16_t h = (int16_t)(vp.y2 - vp.y1 + 1);
    priv->view.vp_x = (int16_t)vp.x1;
    priv->view.vp_y = (int16_t)vp.y1;
    priv->view.vp_w = w;
    priv->view.vp_h = h;

    /* ── Auto-fit on first draw if requested ── */
    if (priv->auto_fit_pending) {
        priv->auto_fit_pending = false;
        gc_view_fit(&priv->view, priv->segbuf,
                    priv->has_limits ? priv->axis_min : NULL,
                    priv->has_limits ? priv->axis_max : NULL,
                    20);
    }

    /* ── 1. Background ── */
    {
        lv_draw_rect_dsc_t bg;
        lv_draw_rect_dsc_init(&bg);
        bg.bg_color = lv_color_hex(GCVIEW_COL_BG);
        bg.bg_opa   = LV_OPA_COVER;
        bg.radius   = 0;
        lv_draw_rect(layer, &bg, &vp);
    }

    const gc_view_t *view = &priv->view;

    /* ── 2. Grid ── */
    if (priv->show_grid) {
        grid_draw_ctx_t gctx = { .layer = layer };
        gc_view_iter_grid(view,
                          priv->has_limits ? priv->axis_min : NULL,
                          priv->has_limits ? priv->axis_max : NULL,
                          grid_line_cb, &gctx);
    }

    /* ── 3. Machine limits ── */
    if (priv->show_limits && priv->has_limits) {
        draw_limits(layer, view, priv->axis_min, priv->axis_max);
    }

    /* ── 4. Toolpath segments ── */
    if (priv->segbuf && priv->segbuf->count > 0) {
        for (uint16_t i = 0; i < priv->segbuf->count; i++) {
            const gc_segment_t *seg = gc_segbuf_get(priv->segbuf, i);
            if (!seg) continue;

            bool is_done = (seg->seq <= priv->segbuf->done_seq);
            lv_color_t col = is_done ? lv_color_hex(GCVIEW_COL_DONE)
                                      : seg_color(seg->type);
            lv_opa_t opa = is_done ? GCVIEW_OPA_DONE : LV_OPA_COVER;

            if (seg->type == GCMOVE_ARC_CW || seg->type == GCMOVE_ARC_CCW) {
                draw_arc_segment(layer, view, seg, col, opa);
            } else {
                gc_pt2_t p1, p2;
                gc_view_project(view, seg->from, &p1);
                gc_view_project(view, seg->to, &p2);

                if (seg->type == GCMOVE_RAPID) {
                    draw_dashed_line(layer, p1, p2, col, opa, GCVIEW_LINE_WIDTH);
                } else {
                    draw_line(layer, p1, p2, col, opa, GCVIEW_LINE_WIDTH);
                }
            }
        }
    }

    /* ── 5. WCS origin marker ── */
    if (priv->show_wcs_origin && priv->machine) {
        gc_vec3_t wcs_origin = gc_vec3(priv->wcs_offset[0],
                                       priv->wcs_offset[1],
                                       priv->wcs_offset[2]);
        draw_origin_axes(layer, view, wcs_origin, 20);

        /* Small label */
        gc_pt2_t wo;
        gc_view_project(view, wcs_origin, &wo);
        lv_draw_label_dsc_t lbl;
        lv_draw_label_dsc_init(&lbl);
        lbl.color      = lv_color_hex(GCVIEW_COL_WCS_ORIGIN);
        lbl.opa        = LV_OPA_70;
        lbl.align      = LV_TEXT_ALIGN_LEFT;
        const char *wcs_names[] = {"G54","G55","G56","G57","G58","G59"};
        int wcs_idx = 0;
        if (priv->machine) wcs_idx = priv->machine->wcs;
        if (wcs_idx < 0 || wcs_idx > 5) wcs_idx = 0;
        lbl.text       = wcs_names[wcs_idx];
        lbl.text_local = 0;
        lv_area_t ta = { wo.x + 6, wo.y - 14, wo.x + 40, wo.y };
        lv_draw_label(layer, &lbl, &ta);
    }

    /* ── 6. Machine origin ── */
    {
        gc_vec3_t origin = gc_vec3(0, 0, 0);
        draw_origin_axes(layer, view, origin, 15);
    }

    /* ── 7. Current spindle position ── */
    if (priv->show_position && priv->machine) {
        gc_vec3_t pos = gc_vec3(priv->mpos[0], priv->mpos[1], priv->mpos[2]);
        gc_pt2_t sp;
        gc_view_project(view, pos, &sp);
        draw_crosshair(layer, sp, lv_color_hex(GCVIEW_COL_POSITION),
                       LV_OPA_COVER, GCVIEW_POSITION_SIZE);
    }

    /* ── 8. View label badge ── */
    {
        const char *name = lv_gcode_viewer_view_name(obj);
        draw_badge(layer, &vp, name, true);
    }

    /* ── 9. Segment count badge ── */
    if (priv->segbuf && priv->segbuf->count > 0) {
        static char seg_str[24];
        snprintf(seg_str, sizeof(seg_str), "%u segs", priv->segbuf->count);
        draw_badge(layer, &vp, seg_str, false);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Touch / gesture handling (pan & zoom)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void press_cb(lv_event_t *e) {
    lv_gcview_priv_t *priv = (lv_gcview_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);

    if (vect.x != 0 || vect.y != 0) {
        gc_view_pan(&priv->view, (float)vect.x, (float)vect.y);
        lv_obj_invalidate(priv->root);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Machine interface callbacks
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_pos_changed(machine_interface_t *mi, void *user_data) {
    lv_gcview_priv_t *priv = (lv_gcview_priv_t *)user_data;
    if (!priv || !mi) return;

    /* Cache position data */
    for (int i = 0; i < 3; i++) {
        priv->mpos[i] = mi->position[i];
        priv->wpos[i] = mi->wcs_position[i];
        priv->wcs_offset[i] = mi->position[i] - mi->wcs_position[i];
    }

    /* Invalidate for redraw — but rate-limit to avoid flooding.
     * LVGL will coalesce invalidations within one frame anyway. */
    if (priv->root) lv_obj_invalidate(priv->root);
}

static void on_home_changed(machine_interface_t *mi, void *user_data) {
    lv_gcview_priv_t *priv = (lv_gcview_priv_t *)user_data;
    if (!priv || !mi) return;

    /* Update limits when homing data arrives */
    bool valid = true;
    for (int i = 0; i < 3; i++) {
        priv->axis_min[i] = mi->axis_min[i];
        priv->axis_max[i] = mi->axis_max[i];
        if (mi->axis_min[i] >= mi->axis_max[i]) valid = false;
    }
    priv->has_limits = valid;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Delete callback (cleanup)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_delete(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;

    /* Remove draw / touch callbacks */
    lv_obj_remove_event_cb_with_user_data(obj, draw_cb, priv);
    lv_obj_remove_event_cb_with_user_data(obj, press_cb, priv);

    /* Deregister machine callbacks */
    /* Note: machine_interface doesn't have a remove callback API,
     * so we rely on the machine outliving the widget or being NULL'd.
     * This is safe because callbacks check priv != NULL. */

    /* Free segment buffer */
    if (priv->segbuf) {
        free(priv->segbuf);
        priv->segbuf = NULL;
    }

    free(priv);
    lv_obj_set_user_data(obj, NULL);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API: lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

lv_obj_t *lv_gcode_viewer_create(lv_obj_t *parent) {
    lv_gcview_priv_t *priv = calloc(1, sizeof(*priv));
    if (!priv) return NULL;

    /* Allocate segment buffer on heap (could be PSRAM on ESP32) */
    priv->segbuf = calloc(1, sizeof(gc_segbuf_t));
    if (!priv->segbuf) {
        free(priv);
        return NULL;
    }
    gc_segbuf_init(priv->segbuf);
    gc_sim_init(&priv->sim);

    /* Defaults */
    priv->show_grid       = true;
    priv->show_limits     = true;
    priv->show_position   = true;
    priv->show_wcs_origin = true;
    priv->auto_fit_pending = true;

    /* Create root object */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(root, priv);
    priv->root = root;

    /* Initialise view with a temporary viewport — will be corrected in draw_cb */
    gc_view_init(&priv->view, GCVIEW_TOP, 0, 0, 200, 200);

    /* Register callbacks */
    lv_obj_add_event_cb(root, draw_cb,  LV_EVENT_DRAW_MAIN_END, priv);
    lv_obj_add_event_cb(root, press_cb, LV_EVENT_PRESSING, priv);
    lv_obj_add_event_cb(root, on_delete, LV_EVENT_DELETE, NULL);

    return root;
}

void lv_gcode_viewer_set_machine(lv_obj_t *obj,
                                 struct machine_interface_t *machine)
{
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;

    priv->machine = machine;

    if (machine) {
        /* Seed initial state */
        for (int i = 0; i < 3; i++) {
            priv->mpos[i] = machine->position[i];
            priv->wpos[i] = machine->wcs_position[i];
            priv->wcs_offset[i] = machine->position[i] - machine->wcs_position[i];
            priv->axis_min[i] = machine->axis_min[i];
            priv->axis_max[i] = machine->axis_max[i];
        }

        bool valid = true;
        for (int i = 0; i < 3; i++) {
            if (machine->axis_min[i] >= machine->axis_max[i]) valid = false;
        }
        priv->has_limits = valid;

        /* Register callbacks */
        machine_interface_add_pos_changed_cb(machine, priv, on_pos_changed);
        machine_interface_add_home_changed_cb(machine, priv, on_home_changed);

        /* Seed the sim from current machine state */
        gc_sim_seed(&priv->sim, machine->position, machine->wcs_position,
                    machine->feed, machine->wcs);
    }

    priv->auto_fit_pending = true;
    lv_obj_invalidate(obj);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API: G-code input
 * ═══════════════════════════════════════════════════════════════════════════ */

void lv_gcode_viewer_load_gcode(lv_obj_t *obj, const char *gcode_text) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;

    /* Clear existing segments */
    gc_segbuf_clear(priv->segbuf);

    /* Reset sim from machine state */
    gc_sim_init(&priv->sim);
    if (priv->machine) {
        gc_sim_seed(&priv->sim, priv->machine->position,
                    priv->machine->wcs_position,
                    priv->machine->feed, priv->machine->wcs);
    }

    if (gcode_text) {
        gc_sim_run_text(&priv->sim, gcode_text, priv->segbuf);
    }

    priv->auto_fit_pending = true;
    lv_obj_invalidate(obj);
}

void lv_gcode_viewer_append_line(lv_obj_t *obj, const char *line) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv || !line) return;

    gc_parsed_line_t parsed;
    if (gcode_parse_line(line, &parsed)) {
        gc_sim_process(&priv->sim, &parsed, priv->segbuf);
    }

    lv_obj_invalidate(obj);
}

void lv_gcode_viewer_clear(lv_obj_t *obj) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;

    gc_segbuf_clear(priv->segbuf);
    gc_sim_init(&priv->sim);
    if (priv->machine) {
        gc_sim_seed(&priv->sim, priv->machine->position,
                    priv->machine->wcs_position,
                    priv->machine->feed, priv->machine->wcs);
    }

    lv_obj_invalidate(obj);
}

void lv_gcode_viewer_mark_done(lv_obj_t *obj) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;

    gc_segbuf_mark_done(priv->segbuf);
    lv_obj_invalidate(obj);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API: view control
 * ═══════════════════════════════════════════════════════════════════════════ */

void lv_gcode_viewer_set_view(lv_obj_t *obj, lv_gcode_viewer_view_t mode) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;
    gc_view_set_mode(&priv->view, lv_to_gc_view_mode(mode));
    priv->auto_fit_pending = true;
    lv_obj_invalidate(obj);
}

/* Forward declaration to avoid implicit-int conflicts when calling before
 * the static definition below. */
static gc_view_mode_t lv_to_gc_view_mode(lv_gcode_viewer_view_t v);

static gc_view_mode_t lv_to_gc_view_mode(lv_gcode_viewer_view_t v) {
    switch (v) {
    case LV_GCVIEW_TOP:       return GCVIEW_TOP;
    case LV_GCVIEW_FRONT:     return GCVIEW_FRONT;
    case LV_GCVIEW_RIGHT:     return GCVIEW_RIGHT;
    case LV_GCVIEW_LEFT:      return GCVIEW_LEFT;
    case LV_GCVIEW_BACK:      return GCVIEW_BACK;
    case LV_GCVIEW_ISOMETRIC: return GCVIEW_ISOMETRIC;
    default:
        assert(!"Unhandled lv_gcode_viewer_view_t");
        return GCVIEW_TOP;
    }
}

static lv_gcode_viewer_view_t gc_to_lv_view_mode(gc_view_mode_t v) {
    switch (v) {
    case GCVIEW_TOP:       return LV_GCVIEW_TOP;
    case GCVIEW_FRONT:     return LV_GCVIEW_FRONT;
    case GCVIEW_RIGHT:     return LV_GCVIEW_RIGHT;
    case GCVIEW_LEFT:      return LV_GCVIEW_LEFT;
    case GCVIEW_BACK:      return LV_GCVIEW_BACK;
    case GCVIEW_ISOMETRIC: return LV_GCVIEW_ISOMETRIC;
    default:
        assert(!"Unhandled gc_view_mode_t");
        return LV_GCVIEW_TOP;
    }
}

lv_gcode_viewer_view_t lv_gcode_viewer_get_view(lv_obj_t *obj) {
    lv_gcview_priv_t *priv = get_priv(obj);
    return priv ? gc_to_lv_view_mode(priv->view.mode) : LV_GCVIEW_TOP;
}

void lv_gcode_viewer_next_view(lv_obj_t *obj) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;
    int next = ((int)priv->view.mode + 1) % GCVIEW_COUNT;
    lv_gcode_viewer_set_view(obj, gc_to_lv_view_mode((gc_view_mode_t)next));
}

void lv_gcode_viewer_fit(lv_obj_t *obj) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;
    priv->auto_fit_pending = true;
    lv_obj_invalidate(obj);
}

void lv_gcode_viewer_zoom(lv_obj_t *obj, float factor) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;
    /* Zoom around viewport centre */
    int16_t cx = (int16_t)(priv->view.vp_x + priv->view.vp_w / 2);
    int16_t cy = (int16_t)(priv->view.vp_y + priv->view.vp_h / 2);
    gc_view_zoom(&priv->view, factor, cx, cy);
    lv_obj_invalidate(obj);
}

static const char *view_names[] = {
    "Top (XY)",
    "Front (XZ)",
    "Right (YZ)",
    "Left (YZ)",
    "Back (XZ)",
    "Isometric",
};

const char *lv_gcode_viewer_view_name(lv_obj_t *obj) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv || priv->view.mode >= GCVIEW_COUNT) return "Unknown";
    return view_names[priv->view.mode];
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API: appearance toggles
 * ═══════════════════════════════════════════════════════════════════════════ */

void lv_gcode_viewer_set_grid(lv_obj_t *obj, bool show) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (priv) { priv->show_grid = show; lv_obj_invalidate(obj); }
}

void lv_gcode_viewer_set_limits(lv_obj_t *obj, bool show) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (priv) { priv->show_limits = show; lv_obj_invalidate(obj); }
}

void lv_gcode_viewer_set_position(lv_obj_t *obj, bool show) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (priv) { priv->show_position = show; lv_obj_invalidate(obj); }
}

void lv_gcode_viewer_set_wcs_origin(lv_obj_t *obj, bool show) {
    lv_gcview_priv_t *priv = get_priv(obj);
    if (priv) { priv->show_wcs_origin = show; lv_obj_invalidate(obj); }
}

void lv_gcode_viewer_invalidate(lv_obj_t *obj) {
    lv_obj_invalidate(obj);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API: data access
 * ═══════════════════════════════════════════════════════════════════════════ */

const gc_segbuf_t *lv_gcode_viewer_get_segments(lv_obj_t *obj) {
    lv_gcview_priv_t *priv = get_priv(obj);
    return priv ? priv->segbuf : NULL;
}

const gc_sim_state_t *lv_gcode_viewer_get_sim_state(lv_obj_t *obj) {
    lv_gcview_priv_t *priv = get_priv(obj);
    return priv ? &priv->sim : NULL;
}
