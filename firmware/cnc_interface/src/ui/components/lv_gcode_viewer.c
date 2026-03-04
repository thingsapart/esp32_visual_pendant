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
#include "config/app_settings.h"

#ifndef ISO_COS
#define ISO_COS  0.86602540378f
#define ISO_SIN  0.50000000000f
#endif

/* Grid subdivisions (compile-time default, can be overridden). */
#ifndef GCVIEW_GRID_SUBDIVISIONS
#define GCVIEW_GRID_SUBDIVISIONS 10
#endif

/* File-local orthographic projection helper (copy of gcode_view::ortho_project)
 * Exposed here because gcode_view's static helper is not available. */
static inline void local_ortho_project(gc_view_mode_t mode,
                                       gc_vec3_t w,
                                       float *out_h, float *out_v)
{
    switch (mode) {
    case GCVIEW_TOP:
        *out_h =  w.x;
        *out_v = -w.y;
        break;
    case GCVIEW_FRONT:
        *out_h =  w.x;
        *out_v = -w.z;
        break;
    case GCVIEW_RIGHT:
        *out_h =  w.y;
        *out_v = -w.z;
        break;
    case GCVIEW_LEFT:
        *out_h = -w.y;
        *out_v = -w.z;
        break;
    case GCVIEW_BACK:
        *out_h = -w.x;
        *out_v = -w.z;
        break;
    case GCVIEW_ISOMETRIC:
        *out_h =  (w.x - w.y) * ISO_COS;
        *out_v = -(w.x + w.y) * ISO_SIN - w.z;
        break;
    default:
        *out_h = w.x;
        *out_v = -w.y;
        break;
    }
}

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

/**
 * Liang-Barsky line clipping against an axis-aligned rectangle.
 * Modifies (cx0,cy0)-(cx1,cy1) to the clipped segment.
 * Returns false if the segment is entirely outside the clip rect.
 */
static bool lv_clip_line(const lv_area_t *clip,
                         int x0, int y0, int x1, int y1,
                         int *cx0, int *cy0, int *cx1, int *cy1)
{
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float t0 = 0.0f, t1 = 1.0f;
    float p[4] = { -dx,  dx,  -dy,  dy };
    float q[4] = {
        (float)(x0 - clip->x1),
        (float)(clip->x2 - x0),
        (float)(y0 - clip->y1),
        (float)(clip->y2 - y0)
    };
    for (int i = 0; i < 4; i++) {
        if (fabsf(p[i]) < 0.5f) {
            if (q[i] < 0.0f) return false;   /* parallel and outside */
        } else {
            float r = q[i] / p[i];
            if (p[i] < 0.0f) { if (r > t0) t0 = r; }
            else              { if (r < t1) t1 = r; }
            if (t0 > t1) return false;        /* clipped away */
        }
    }
    *cx0 = x0 + (int)(t0 * dx + 0.5f);
    *cy0 = y0 + (int)(t0 * dy + 0.5f);
    *cx1 = x0 + (int)(t1 * dx + 0.5f);
    *cy1 = y0 + (int)(t1 * dy + 0.5f);
    return true;
}

/**
 * Draw a single line segment between two screen points.
 *
 * Lines are software-clipped to the layer's clip area before being
 * submitted to LVGL.  This avoids the anti-alias rasteriser running
 * on out-of-viewport portions (critical for diagonal ISO grid lines
 * which otherwise extend to the full viewport diagonal).
 *
 * round_start/round_end are forced to 0 — round line-caps nearly
 * double the work for short grid tick marks.
 */
static void draw_line(lv_layer_t *layer,
                      gc_pt2_t p1, gc_pt2_t p2,
                      lv_color_t color, lv_opa_t opa, int width)
{
    int cx0, cy0, cx1, cy1;
    if (!lv_clip_line(&layer->_clip_area,
                      p1.x, p1.y, p2.x, p2.y,
                      &cx0, &cy0, &cx1, &cy1)) return;

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color       = color;
    dsc.opa         = opa;
    dsc.width       = width;
    dsc.round_start = 0;
    dsc.round_end   = 0;
    dsc.p1.x = cx0;  dsc.p1.y = cy0;
    dsc.p2.x = cx1;  dsc.p2.y = cy1;
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
    const gc_view_t *view;
    lv_gcview_priv_t *priv;
} grid_draw_ctx_t;

static void grid_line_cb(bool is_major, gc_pt2_t p1, gc_pt2_t p2, void *ctx) {
    grid_draw_ctx_t *gc = (grid_draw_ctx_t *)ctx;
    if (gc->view && gc->view->mode == GCVIEW_ISOMETRIC) {
        /* Iso: ALL lines width=1 to avoid LVGL's expensive AA thick-line
         * rasteriser on 30° diagonals.  Major lines are distinguished by
         * brightness only (no width difference). */
        lv_color_t col = is_major ? lv_color_hex(0x00BB00) : lv_color_hex(0x005500);
        lv_opa_t opa = is_major ? LV_OPA_COVER : LV_OPA_60;
        draw_line(gc->layer, p1, p2, col, opa, 1);
    } else {
        lv_color_t col = is_major ? lv_color_hex(GCVIEW_COL_GRID_MAJOR)
                                  : lv_color_hex(GCVIEW_COL_GRID);
        lv_opa_t opa = is_major ? (lv_opa_t)(GCVIEW_OPA_GRID + 40) : GCVIEW_OPA_GRID;
        draw_line(gc->layer, p1, p2, col, opa, GCVIEW_GRID_LINE_WIDTH);
    }
}

/* Draw a grid using explicit spacing from app settings (fallback) and
 * machine limits when available. Colour is dark green for a subtle overlay. */
static void draw_settings_grid(lv_layer_t *layer, const gc_view_t *view,
                               lv_gcview_priv_t *priv)
{
    /* local_ortho_project is defined at file scope */
    /* Get fallback spacing from app settings */
    float gdx = app_settings_get_float(APP_SETTINGS_GROUP_CAM,
                                      APP_SETTINGS_CAM_FALLBACK_GRID_DX);
    float gdy = app_settings_get_float(APP_SETTINGS_GROUP_CAM,
                                      APP_SETTINGS_CAM_FALLBACK_GRID_DY);

    /* If invalid, fall back to auto spacing from view */
    if (!(gdx > 0.0f)) gdx = gc_view_grid_spacing(view);
    if (!(gdy > 0.0f)) gdy = gc_view_grid_spacing(view);

    /* Determine visible world-range. For isometric we must compute
     * world X/Y bounds by inverting the iso projection; for other views
     * we can reuse h/v approach. */
    float h0, v0, h1, v1;
    if (view->mode == GCVIEW_ISOMETRIC) {
        /* Unproject screen corners to world XY (z = 0) using iso inverse */
        int sx[4] = { view->vp_x, view->vp_x + view->vp_w - 1,
                      view->vp_x, view->vp_x + view->vp_w - 1 };
        int sy[4] = { view->vp_y, view->vp_y, view->vp_y + view->vp_h - 1,
                      view->vp_y + view->vp_h - 1 };
        float wx[4], wy[4];
        float cx = (float)(view->vp_x + view->vp_w / 2);
        float cy = (float)(view->vp_y + view->vp_h / 2);
        for (int i = 0; i < 4; i++) {
            float sxp = ((float)sx[i] - cx - view->pan_x) / view->ppm; /* = (x - y) * ISO_COS */
            float syp = ((float)sy[i] - cy - view->pan_y) / view->ppm; /* = -(x + y) * ISO_SIN */
            float xp = (sxp / ISO_COS + (-syp / ISO_SIN)) * 0.5f;
            float yp = ((-syp / ISO_SIN) - (sxp / ISO_COS)) * 0.5f;
            wx[i] = xp; wy[i] = yp;
        }
        float minx = wx[0], maxx = wx[0], miny = wy[0], maxy = wy[0];
        for (int i = 1; i < 4; i++) {
            if (wx[i] < minx) minx = wx[i]; if (wx[i] > maxx) maxx = wx[i];
            if (wy[i] < miny) miny = wy[i]; if (wy[i] > maxy) maxy = wy[i];
        }
        h0 = minx; h1 = maxx; v0 = miny; v1 = maxy;
    } else {
        gc_pt2_t tl = { view->vp_x, view->vp_y };
        gc_pt2_t br = { (int16_t)(view->vp_x + view->vp_w - 1),
                        (int16_t)(view->vp_y + view->vp_h - 1) };
        gc_vec3_t w_tl, w_br;
        gc_view_unproject(view, tl, 0, &w_tl);
        gc_view_unproject(view, br, 0, &w_br);
        local_ortho_project(view->mode, w_tl, &h0, &v0);
        local_ortho_project(view->mode, w_br, &h1, &v1);
        if (h0 > h1) { float t = h0; h0 = h1; h1 = t; }
        if (v0 > v1) { float t = v0; v0 = v1; v1 = t; }
    }

    /* Choose step sizes for h and v axes. Map gdx->h and gdy->v for TOP view,
     * for other views we keep the mapping generic: h->gdx, v->gdy. */
    float step_h = gdx;
    float step_v = gdy;

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(0x003300); /* dark green */
    dsc.width = 1;
    dsc.opa = LV_OPA_60;

    /* Draw subdivision lines + main grid lines. */
    int subdiv = GCVIEW_GRID_SUBDIVISIONS > 1 ? GCVIEW_GRID_SUBDIVISIONS : 1;
    float sub_step_h = step_h / (float)subdiv;
    float sub_step_v = step_v / (float)subdiv;

    if (view->mode == GCVIEW_ISOMETRIC) {
        /* For iso: draw lines of constant X and constant Y on the Z=0 plane. */
        int subdiv = GCVIEW_GRID_SUBDIVISIONS > 1 ? GCVIEW_GRID_SUBDIVISIONS : 1;
        float subx = step_h / (float)subdiv;
        float suby = step_v / (float)subdiv;

        float x_first = floorf(h0 / subx) * subx; /* Start from the nearest lower multiple */
        for (float x = x_first; x <= h1 + 1e-6f; x += subx) {
            bool is_major = (fabsf(fmodf(x, step_h)) < (subx * 0.5f)) || (fabsf(x) < 1e-6f);
            gc_vec3_t w1 = gc_vec3(x, v0, 0);
            gc_vec3_t w2 = gc_vec3(x, v1, 0);
            gc_pt2_t p1, p2;
            gc_view_project(view, w1, &p1);
            gc_view_project(view, w2, &p2);
            draw_line(layer, p1, p2, is_major ? lv_color_hex(0x00AA00) : lv_color_hex(0x006600),
                      is_major ? LV_OPA_COVER : LV_OPA_70, is_major ? 2 : 1);
        }

        float y_first = floorf(v0 / suby) * suby; /* Start from the nearest lower multiple */
        for (float y = y_first; y <= v1 + 1e-6f; y += suby) {
            bool is_major = (fabsf(fmodf(y, step_v)) < (suby * 0.5f)) || (fabsf(y) < 1e-6f);
            gc_vec3_t w1 = gc_vec3(h0, y, 0);
            gc_vec3_t w2 = gc_vec3(h1, y, 0);
            gc_pt2_t p1, p2;
            gc_view_project(view, w1, &p1);
            gc_view_project(view, w2, &p2);
            draw_line(layer, p1, p2, is_major ? lv_color_hex(0x00AA00) : lv_color_hex(0x006600),
                      is_major ? LV_OPA_COVER : LV_OPA_70, is_major ? 2 : 1);
        }
    } else {
        /* Existing non-iso logic (unchanged) */
        float h_sub_first = ceilf(h0 / sub_step_h) * sub_step_h;
        float tol_h = sub_step_h * 0.25f;
        for (float hh = h_sub_first; hh <= h1 + 1e-6f; hh += sub_step_h) {
            bool is_major = (fabsf(fmodf(fabsf(hh), step_h)) < tol_h) || (fabsf(hh) < tol_h);
            gc_vec3_t w1, w2;
            switch (view->mode) {
            case GCVIEW_TOP:    w1 = gc_vec3(hh, -v0, 0); w2 = gc_vec3(hh, -v1, 0); break;
            case GCVIEW_FRONT:  w1 = gc_vec3(hh, 0, -v0); w2 = gc_vec3(hh, 0, -v1); break;
            case GCVIEW_RIGHT:  w1 = gc_vec3(0, hh, -v0); w2 = gc_vec3(0, hh, -v1); break;
            case GCVIEW_LEFT:   w1 = gc_vec3(0, -hh, -v0); w2 = gc_vec3(0, -hh, -v1); break;
            case GCVIEW_BACK:   w1 = gc_vec3(-hh, 0, -v0); w2 = gc_vec3(-hh, 0, -v1); break;
            default:            w1 = gc_vec3(hh, -v0, 0); w2 = gc_vec3(hh, -v1, 0); break;
            }
            gc_pt2_t p1, p2;
            gc_view_project(view, w1, &p1);
            gc_view_project(view, w2, &p2);
            draw_line(layer, p1, p2,
                      is_major ? lv_color_hex(0x00AA00) : lv_color_hex(0x006600),
                      is_major ? LV_OPA_COVER : LV_OPA_70,
                      is_major ? 2 : 1);
        }

        float v_sub_first = ceilf(v0 / sub_step_v) * sub_step_v;
        float tol_v = sub_step_v * 0.25f;
        for (float vv = v_sub_first; vv <= v1 + 1e-6f; vv += sub_step_v) {
            bool is_major = (fabsf(fmodf(fabsf(vv), step_v)) < tol_v) || (fabsf(vv) < tol_v);
            gc_vec3_t w1, w2;
            switch (view->mode) {
            case GCVIEW_TOP:    w1 = gc_vec3(h0, -vv, 0); w2 = gc_vec3(h1, -vv, 0); break;
            case GCVIEW_FRONT:  w1 = gc_vec3(h0, 0, -vv); w2 = gc_vec3(h1, 0, -vv); break;
            case GCVIEW_RIGHT:  w1 = gc_vec3(0, h0, -vv); w2 = gc_vec3(0, h1, -vv); break;
            case GCVIEW_LEFT:   w1 = gc_vec3(0, -h0, -vv); w2 = gc_vec3(0, -h1, -vv); break;
            case GCVIEW_BACK:   w1 = gc_vec3(-h0, 0, -vv); w2 = gc_vec3(-h1, 0, -vv); break;
            default:            w1 = gc_vec3(h0, -vv, 0); w2 = gc_vec3(h1, -vv, 0); break;
            }
            gc_pt2_t p1, p2;
            gc_view_project(view, w1, &p1);
            gc_view_project(view, w2, &p2);
            draw_line(layer, p1, p2,
                      is_major ? lv_color_hex(0x00AA00) : lv_color_hex(0x006600),
                      is_major ? LV_OPA_COVER : LV_OPA_70,
                      is_major ? 2 : 1);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal: grid tick-mark ruler
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Pick a tick step from the set {1, 5, 10} × 10^n (n integer) such that
 * consecutive ticks are at least @p min_px screen pixels apart.
 */
static float nice_135_step(float ppm, float min_px)
{
    /* Ascending series: 0.01, 0.05, 0.1, 0.5, 1, 5, 10, 50, 100, 500, 1000 */
    static const float base[] = {
        0.01f, 0.05f, 0.1f, 0.5f, 1.0f,
        5.0f,  10.0f, 50.0f, 100.0f, 500.0f, 1000.0f
    };
    for (int i = 0; i < (int)(sizeof(base)/sizeof(base[0])); i++) {
        if (base[i] * ppm >= min_px) return base[i];
    }
    return 1000.0f;
}

/**
 * Draw ruler tick marks along the four viewport edges (orthographic views),
 * or along the projected X / Y axis lines (isometric view).
 *
 * Tick step is chosen from {1,5,10}×10^n so the smallest step gives at
 * least 8 px.  Major ticks fire every 5 (or 10) steps and optionally carry
 * a numeric label (controlled via GCVIEW_GRID_LABEL_ENABLE and the minimum
 * pixel spacing GCVIEW_GRID_LABEL_MIN_PX).
 *
 * Axis flip (machine origin convention) is handled by delegating all
 * screen-position queries to gc_view_project(), so this function is
 * flip-agnostic for both ISO and orthographic modes.
 *
 * Performance notes
 * -----------------
 * - All label rendering is compiled out when GCVIEW_GRID_LABEL_ENABLE == 0.
 * - Labels are also skipped at runtime when major_step × ppm < GCVIEW_GRID_LABEL_MIN_PX.
 * - Each label string is formatted into a local stack buffer and passed with
 *   text_local = 1 so LVGL owns a copy — this avoids dangling-pointer issues
 *   if LVGL defers rendering across the loop.
 * - Each loop is capped at GCVIEW_MAX_GRID_LINES / 2 iterations.
 */
static void draw_grid_ticks(lv_layer_t *layer, const gc_view_t *view)
{
    const int   TICK_MINOR  = 4;    /* tick arm length (px) */
    const int   TICK_MAJOR  = 8;
    const float MIN_PX      = 8.0f; /* min px between minor ticks */
    const int   MAX_TICKS   = GCVIEW_MAX_GRID_LINES / 2;

    float ppm   = view->ppm;
    int   vp_x  = view->vp_x, vp_y  = view->vp_y;
    int   vp_w  = view->vp_w, vp_h  = view->vp_h;

    /* ── Step selection ── */
    float minor_step = nice_135_step(ppm, MIN_PX);
    /* Major every 5 if that gives >= 40 px, else every 10 */
    float major_step = minor_step * ((minor_step * 5.0f * ppm >= 40.0f) ? 5.0f : 10.0f);

#if GCVIEW_GRID_LABEL_ENABLE
    bool do_labels = (major_step * ppm >= (float)GCVIEW_GRID_LABEL_MIN_PX);
#else
    bool do_labels = false;
#endif

    lv_color_t col_minor = lv_color_hex(0x336633);
    lv_color_t col_major = lv_color_hex(0x66BB66);
    lv_color_t col_label = lv_color_hex(0x99DD99);

    /* ── Helpers ── */
#define IS_MAJOR(v)  (fabsf(fmodf(fabsf(v), major_step)) < minor_step * 0.1f \
                      || fabsf(v) < minor_step * 0.01f)
#define TICK_LEN(m)  ((m) ? TICK_MAJOR : TICK_MINOR)
#define TICK_COL(m)  ((m) ? col_major  : col_minor)
#define TICK_OPA(m)  ((m) ? LV_OPA_COVER : LV_OPA_70)

    /* Draw a single tick label.  Uses a local stack buffer and text_local=1
     * so LVGL makes its own copy — safe even if rendering is deferred. */
#define DRAW_LABEL(val, x1, y1, x2, y2, halign) do {                   \
    if (do_labels) {                                                    \
        char _lb[16];                                                   \
        if (fabsf((float)(val) - roundf((float)(val))) < 5e-3f)        \
            snprintf(_lb, sizeof(_lb), "%d", (int)roundf((float)(val)));\
        else                                                            \
            snprintf(_lb, sizeof(_lb), "%.1f", (float)(val));          \
        lv_draw_label_dsc_t _ld;                                        \
        lv_draw_label_dsc_init(&_ld);                                   \
        _ld.color      = col_label; _ld.opa = LV_OPA_80;               \
        _ld.align      = (halign);                                      \
        _ld.text       = _lb;                                           \
        _ld.text_local = 1;   /* LVGL copies the string */              \
        lv_area_t _la = { (x1), (y1), (x2), (y2) };                   \
        lv_draw_label(layer, &_ld, &_la);                               \
    }                                                                   \
} while(0)

    if (view->mode == GCVIEW_ISOMETRIC) {
        /* ── Isometric ──────────────────────────────────────────────────
         * We delegate screen-position queries to gc_view_project() so that
         * axis_flip is handled automatically.
         *
         * The approach:
         *   1. Project origin (0,0,0) and unit vectors (1,0,0), (0,1,0).
         *   2. Derive per-mm screen deltas → axis direction + perpendicular.
         *   3. Determine visible world-value range on each axis by intersecting
         *      screen H and V viewport constraints (via the linear projection).
         *   4. Iterate and draw ticks; labels at major ticks.
         * ──────────────────────────────────────────────────────────────── */

        gc_pt2_t op, xp, yp;
        gc_view_project(view, gc_vec3(0, 0, 0), &op);
        gc_view_project(view, gc_vec3(1, 0, 0), &xp);
        gc_view_project(view, gc_vec3(0, 1, 0), &yp);

        /* X-axis: screen delta per world-X mm */
        float xdx = (float)(xp.x - op.x);   /* expected: ±ISO_COS*ppm */
        float xdy = (float)(xp.y - op.y);   /* expected: ∓ISO_SIN*ppm */
        float xsc = sqrtf(xdx*xdx + xdy*xdy);
        if (xsc < 0.1f) xsc = 0.1f;
        /* Perpendicular (90° CCW): (-xdy/xsc, xdx/xsc) */
        float xpx_n = -xdy / xsc;  /* normalised perp X */
        float xpy_n =  xdx / xsc;

        /* Y-axis: screen delta per world-Y mm */
        float ydx = (float)(yp.x - op.x);
        float ydy = (float)(yp.y - op.y);
        float ysc = sqrtf(ydx*ydx + ydy*ydy);
        if (ysc < 0.1f) ysc = 0.1f;
        float ypx_n = -ydy / ysc;
        float ypy_n =  ydx / ysc;

        /* ── X-axis visible range ──
         * Screen position of (xv,0,0): sx = op.x + xv*xdx, sy = op.y + xv*xdy
         * Clip to viewport H: vp_x <= op.x + xv*xdx <= vp_x+vp_w-1
         * Clip to viewport V: vp_y <= op.y + xv*xdy <= vp_y+vp_h-1   */
        float xa, xb;
        {
            float xh0 = 1e9f, xh1 = -1e9f, xv0 = 1e9f, xv1 = -1e9f;
            if (fabsf(xdx) > 0.1f) {
                float r0 = ((float)vp_x           - (float)op.x) / xdx;
                float r1 = ((float)(vp_x + vp_w - 1) - (float)op.x) / xdx;
                xh0 = r0 < r1 ? r0 : r1;
                xh1 = r0 > r1 ? r0 : r1;
            } else { xh0 = -1e9f; xh1 = 1e9f; }
            if (fabsf(xdy) > 0.1f) {
                float r0 = ((float)vp_y           - (float)op.y) / xdy;
                float r1 = ((float)(vp_y + vp_h - 1) - (float)op.y) / xdy;
                xv0 = r0 < r1 ? r0 : r1;
                xv1 = r0 > r1 ? r0 : r1;
            } else { xv0 = -1e9f; xv1 = 1e9f; }
            xa = xh0 > xv0 ? xh0 : xv0;
            xb = xh1 < xv1 ? xh1 : xv1;
        }

        if (xa < xb) {
            float xs = floorf(xa / minor_step) * minor_step;
            int cnt = 0;
            for (float xv = xs; xv <= xb + 1e-4f && cnt < MAX_TICKS; xv += minor_step, cnt++) {
                bool maj   = IS_MAJOR(xv);
                float tlen = (float)TICK_LEN(maj);
                float sx   = (float)op.x + xv * xdx;
                float sy   = (float)op.y + xv * xdy;
                /* Bounds check */
                if (sx < (float)vp_x - tlen || sx > (float)(vp_x + vp_w - 1) + tlen) continue;
                if (sy < (float)vp_y - tlen || sy > (float)(vp_y + vp_h - 1) + tlen) continue;
                /* Render tick arms axis-aligned for cheaper rasterisation:
                 * X-axis ticks are vertical, Y-axis ticks are horizontal.
                 * Keep the original perpendicular (xpx_n/xpy_n) for label offset. */
                gc_pt2_t p1 = { (int16_t)(sx), (int16_t)(sy - tlen) };
                gc_pt2_t p2 = { (int16_t)(sx), (int16_t)(sy + tlen) };
                draw_line(layer, p1, p2, TICK_COL(maj), TICK_OPA(maj), 1);
                if (maj) {
                    /* Label offset along +perpendicular (away from grid) */
                    float lo = tlen + 2.0f;
                    DRAW_LABEL(xv,
                        (int16_t)(sx + xpx_n * lo - 16),
                        (int16_t)(sy + xpy_n * lo - 1),
                        (int16_t)(sx + xpx_n * lo + 16),
                        (int16_t)(sy + xpy_n * lo + 11),
                        LV_TEXT_ALIGN_CENTER);
                }
            }
        }

        /* ── Y-axis visible range ── */
        float ya, yb;
        {
            float yh0 = 1e9f, yh1 = -1e9f, yv0 = 1e9f, yv1 = -1e9f;
            if (fabsf(ydx) > 0.1f) {
                float r0 = ((float)vp_x           - (float)op.x) / ydx;
                float r1 = ((float)(vp_x + vp_w - 1) - (float)op.x) / ydx;
                yh0 = r0 < r1 ? r0 : r1;
                yh1 = r0 > r1 ? r0 : r1;
            } else { yh0 = -1e9f; yh1 = 1e9f; }
            if (fabsf(ydy) > 0.1f) {
                float r0 = ((float)vp_y           - (float)op.y) / ydy;
                float r1 = ((float)(vp_y + vp_h - 1) - (float)op.y) / ydy;
                yv0 = r0 < r1 ? r0 : r1;
                yv1 = r0 > r1 ? r0 : r1;
            } else { yv0 = -1e9f; yv1 = 1e9f; }
            ya = yh0 > yv0 ? yh0 : yv0;
            yb = yh1 < yv1 ? yh1 : yv1;
        }

        if (ya < yb) {
            float ys = floorf(ya / minor_step) * minor_step;
            int cnt = 0;
            for (float yv = ys; yv <= yb + 1e-4f && cnt < MAX_TICKS; yv += minor_step, cnt++) {
                bool maj   = IS_MAJOR(yv);
                float tlen = (float)TICK_LEN(maj);
                float sx   = (float)op.x + yv * ydx;
                float sy   = (float)op.y + yv * ydy;
                if (sx < (float)vp_x - tlen || sx > (float)(vp_x + vp_w - 1) + tlen) continue;
                if (sy < (float)vp_y - tlen || sy > (float)(vp_y + vp_h - 1) + tlen) continue;
                /* Render tick arms axis-aligned for cheaper rasterisation:
                 * Y-axis ticks are horizontal (arm along X screen axis).
                 * Keep the original perpendicular (ypx_n/ypy_n) for label offset. */
                gc_pt2_t p1 = { (int16_t)(sx - tlen), (int16_t)(sy) };
                gc_pt2_t p2 = { (int16_t)(sx + tlen), (int16_t)(sy) };
                draw_line(layer, p1, p2, TICK_COL(maj), TICK_OPA(maj), 1);
                if (maj) {
                    float lo = tlen + 2.0f;
                    DRAW_LABEL(yv,
                        (int16_t)(sx + ypx_n * lo),
                        (int16_t)(sy + ypy_n * lo - 7),
                        (int16_t)(sx + ypx_n * lo + 28),
                        (int16_t)(sy + ypy_n * lo + 7),
                        LV_TEXT_ALIGN_LEFT);
                }
            }
        }

    } else {
        /* ── Orthographic ────────────────────────────────────────────────
         * H ticks along top and bottom edges; V ticks along left and right.
         * screen_x = h * ppm + pan_x + cx  (linear in h) — flip is handled
         * by gc_view_project, and we derive the same formula from it here.
         * ──────────────────────────────────────────────────────────────── */

        /* Derive screen mapping for H and V from gc_view_project  */
        gc_pt2_t o0, h1, v1;
        gc_view_project(view, gc_vec3(0, 0, 0), &o0);
        gc_view_project(view, gc_vec3(1, 0, 0), &h1);
        gc_view_project(view, gc_vec3(0, -1, 0), &v1);  /* -1 because v = -y in TOP */

        /* For views other than TOP the horizontal axis is not world-X.
         * Use the view mode to pick the right unit vectors. */
        gc_vec3_t h_unit, v_unit;           /* unit steps for h and v axes */
        switch (view->mode) {
        case GCVIEW_TOP:   h_unit = gc_vec3( 1,  0, 0); v_unit = gc_vec3(0, -1, 0); break;
        case GCVIEW_FRONT: h_unit = gc_vec3( 1,  0, 0); v_unit = gc_vec3(0,  0,-1); break;
        case GCVIEW_RIGHT: h_unit = gc_vec3( 0,  1, 0); v_unit = gc_vec3(0,  0,-1); break;
        case GCVIEW_LEFT:  h_unit = gc_vec3( 0, -1, 0); v_unit = gc_vec3(0,  0,-1); break;
        case GCVIEW_BACK:  h_unit = gc_vec3(-1,  0, 0); v_unit = gc_vec3(0,  0,-1); break;
        default:           h_unit = gc_vec3( 1,  0, 0); v_unit = gc_vec3(0, -1, 0); break;
        }
        (void)h1; (void)v1; /* computed above for reference; use unit vecs below */

        /* Project origin and unit steps to get screen scale */
        gc_pt2_t hp, vp_pt;
        gc_view_project(view, gc_vec3(0, 0, 0), &o0);
        {
            gc_vec3_t hw = { h_unit.x, h_unit.y, h_unit.z };
            gc_vec3_t vw = { v_unit.x, v_unit.y, v_unit.z };
            gc_view_project(view, hw, &hp);
            gc_view_project(view, vw, &vp_pt);
        }
        float h_sx = (float)(hp.x - o0.x);   /* screen-x delta per 1 mm of h */
        float h_sy = (float)(hp.y - o0.y);   /* should be ~0 for ortho; ~ppm for h */
        float v_sx = (float)(vp_pt.x - o0.x);
        float v_sy = (float)(vp_pt.y - o0.y);

        /* Visible world-H range */
        float h0, h1_f;
        if (fabsf(h_sx) > 0.1f) {
            h0   = ((float)vp_x           - (float)o0.x) / h_sx;
            h1_f = ((float)(vp_x + vp_w - 1) - (float)o0.x) / h_sx;
            if (h0 > h1_f) { float t = h0; h0 = h1_f; h1_f = t; }
        } else { h0 = -1000.0f; h1_f = 1000.0f; }

        /* Visible world-V range */
        float v0, v1_f;
        if (fabsf(v_sy) > 0.1f) {
            v0   = ((float)vp_y           - (float)o0.y) / v_sy;
            v1_f = ((float)(vp_y + vp_h - 1) - (float)o0.y) / v_sy;
            if (v0 > v1_f) { float t = v0; v0 = v1_f; v1_f = t; }
        } else { v0 = -1000.0f; v1_f = 1000.0f; }

        /* H ticks: draw at top and bottom viewport edges, sweep h-range */
        float hs = floorf(h0 / minor_step) * minor_step;
        int cnt = 0;
        for (float hh = hs; hh <= h1_f + 1e-4f && cnt < MAX_TICKS; hh += minor_step, cnt++) {
            bool  maj  = IS_MAJOR(hh);
            int   tlen = TICK_LEN(maj);
            float sx   = (float)o0.x + hh * h_sx;
            if (sx < (float)vp_x || sx > (float)(vp_x + vp_w - 1)) continue;
            int isx = (int)sx;

            gc_pt2_t ta = { (int16_t)isx, (int16_t)vp_y };
            gc_pt2_t tb = { (int16_t)isx, (int16_t)(vp_y + tlen) };
            draw_line(layer, ta, tb, TICK_COL(maj), TICK_OPA(maj), 1);

            gc_pt2_t ba = { (int16_t)isx, (int16_t)(vp_y + vp_h - 1 - tlen) };
            gc_pt2_t bb = { (int16_t)isx, (int16_t)(vp_y + vp_h - 1) };
            draw_line(layer, ba, bb, TICK_COL(maj), TICK_OPA(maj), 1);

            if (maj) {
                /* world coord = hh (h_unit already accounts for sign) */
                DRAW_LABEL(hh,
                    (int16_t)(isx - 18),
                    (int16_t)(vp_y + vp_h - 1 - tlen - 14),
                    (int16_t)(isx + 18),
                    (int16_t)(vp_y + vp_h - 1 - tlen - 1),
                    LV_TEXT_ALIGN_CENTER);
            }
        }

        /* V ticks: draw at left and right viewport edges, sweep v-range */
        float vs = floorf(v0 / minor_step) * minor_step;
        cnt = 0;
        for (float vv = vs; vv <= v1_f + 1e-4f && cnt < MAX_TICKS; vv += minor_step, cnt++) {
            bool  maj  = IS_MAJOR(vv);
            int   tlen = TICK_LEN(maj);
            float sy   = (float)o0.y + vv * v_sy;
            if (sy < (float)vp_y || sy > (float)(vp_y + vp_h - 1)) continue;
            int isy = (int)sy;

            gc_pt2_t la = { (int16_t)vp_x,        (int16_t)isy };
            gc_pt2_t lb = { (int16_t)(vp_x + tlen),(int16_t)isy };
            draw_line(layer, la, lb, TICK_COL(maj), TICK_OPA(maj), 1);

            gc_pt2_t ra = { (int16_t)(vp_x + vp_w - 1 - tlen), (int16_t)isy };
            gc_pt2_t rb = { (int16_t)(vp_x + vp_w - 1),        (int16_t)isy };
            draw_line(layer, ra, rb, TICK_COL(maj), TICK_OPA(maj), 1);

            if (maj) {
                /* world coord = vv (v_unit already accounts for sign) */
                DRAW_LABEL(vv,
                    (int16_t)(vp_x + tlen + 2),
                    (int16_t)(isy - 7),
                    (int16_t)(vp_x + tlen + 30),
                    (int16_t)(isy + 7),
                    LV_TEXT_ALIGN_LEFT);
            }
        }
    }

#undef IS_MAJOR
#undef TICK_LEN
#undef TICK_COL
#undef TICK_OPA
#undef DRAW_LABEL
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
        /* For isometric view use the existing iterator (handles iso better)
         * and draw colors via grid_line_cb. For other views use settings grid. */
        if (view->mode == GCVIEW_ISOMETRIC) {
            grid_draw_ctx_t gctx = { .layer = layer, .view = view, .priv = priv };
            gc_view_iter_grid(view,
                              priv->has_limits ? priv->axis_min : NULL,
                              priv->has_limits ? priv->axis_max : NULL,
                              grid_line_cb, &gctx);
        } else {
            draw_settings_grid(layer, view, priv);
        }
        draw_grid_ticks(layer, view);
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

    /* View label replaced by dropdown in widget UI */

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

/* Dropdown change handler - set view based on selection index */
static void view_dropdown_cb(lv_event_t *e) {
    lv_obj_t *dd = lv_event_get_target(e);
    lv_obj_t *root = (lv_obj_t *)lv_event_get_user_data(e);
    if (!root || !dd) return;
    uint32_t sel = lv_dropdown_get_selected(dd);
    if (sel >= (uint32_t)GCVIEW_COUNT) return;
    lv_gcode_viewer_set_view(root, (lv_gcode_viewer_view_t)sel);
}

/* Zoom buttons */
static void zoom_in_cb(lv_event_t *e) {
    lv_obj_t *root = (lv_obj_t *)lv_event_get_user_data(e);
    if (!root) return;
    lv_gcode_viewer_zoom(root, 1.2f);
}
static void zoom_out_cb(lv_event_t *e) {
    lv_obj_t *root = (lv_obj_t *)lv_event_get_user_data(e);
    if (!root) return;
    lv_gcode_viewer_zoom(root, 1.0f/1.2f);
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

    /* --- UI controls: view dropdown + zoom buttons --- */
    /* Build options string from view_names */
    char opts[256] = {0};
    const char *local_view_names[] = {
        "Top (XY)",
        "Front (XZ)",
        "Right (YZ)",
        "Left (YZ)",
        "Back (XZ)",
        "Isometric",
    };
    for (int i = 0; i < (int)GCVIEW_COUNT && i < (int)(sizeof(local_view_names)/sizeof(local_view_names[0])); i++) {
        if (i > 0) strlcat(opts, "\n", sizeof(opts));
        strlcat(opts, local_view_names[i], sizeof(opts));
    }

    lv_obj_t *dd = lv_dropdown_create(root);
    lv_dropdown_set_options(dd, opts);
    /* Make slightly wider and add internal padding */
    lv_obj_set_width(dd, 130);
    lv_obj_set_style_pad_left(dd, 10, 0);
    lv_obj_set_style_pad_right(dd, 10, 0);
    /* Start in isometric view */
    lv_dropdown_set_selected(dd, (uint16_t)GCVIEW_ISOMETRIC);
    lv_obj_align(dd, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_add_event_cb(dd, view_dropdown_cb, LV_EVENT_VALUE_CHANGED, root);
    /* Ensure viewer is in isometric mode at start */
    lv_gcode_viewer_set_view(root, LV_GCVIEW_ISOMETRIC);

    /* Zoom in */
    /* Zoom buttons: top-left, larger, dark gray with white font, small radius */
    lv_obj_t *btn_minus = lv_btn_create(root);
    lv_obj_set_size(btn_minus, 40, 40);
    lv_obj_align(btn_minus, LV_ALIGN_TOP_LEFT, 6, 6); /* place just below dd */
    lv_obj_set_style_radius(btn_minus, 2, 0);
    lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn_minus, LV_OPA_COVER, 0);
    lv_obj_t *lblm = lv_label_create(btn_minus);
    lv_label_set_text(lblm, "-");
    lv_obj_set_style_text_color(lblm, lv_color_white(), 0);
    lv_obj_center(lblm);
    lv_obj_add_event_cb(btn_minus, zoom_out_cb, LV_EVENT_CLICKED, root);

    lv_obj_t *btn_plus = lv_btn_create(root);
    lv_obj_set_size(btn_plus, 40, 40);
    lv_obj_align(btn_plus, LV_ALIGN_TOP_LEFT, 6 + 46, 6);
    lv_obj_set_style_radius(btn_plus, 2, 0);
    lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn_plus, LV_OPA_COVER, 0);
    lv_obj_t *lblp = lv_label_create(btn_plus);
    lv_label_set_text(lblp, "+");
    lv_obj_set_style_text_color(lblp, lv_color_white(), 0);
    lv_obj_center(lblp);
    lv_obj_add_event_cb(btn_plus, zoom_in_cb, LV_EVENT_CLICKED, root);

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

static gc_view_mode_t gc_to_lv_view_mode(gc_view_mode_t v) {
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

void lv_gcode_viewer_set_machine_origin(lv_obj_t *obj,
                                         lv_gcview_machine_origin_t origin)
{
    lv_gcview_priv_t *priv = get_priv(obj);
    if (!priv) return;

    /* Map origin enum to axis flip flags:
     *
     *  FRONT_LEFT  → X+→right (+1), Y+→back (+1)   (standard default)
     *  FRONT_RIGHT → X+→left  (-1), Y+→back (+1)
     *  BACK_LEFT   → X+→right (+1), Y+→front (-1)  (Y axis mirrored)
     *  BACK_RIGHT  → X+→left  (-1), Y+→front (-1)  (typical Grbl home)
     *
     *  In the TOP projection: h = x * flip_x, v = -(y * flip_y).
     *  So flip_x=-1 puts X+ leftward; flip_y=-1 puts Y+ upward on screen
     *  (since v is already negated, double-negation reverses the direction).
     */
    int8_t fx, fy;
    switch (origin) {
    default:
    case LV_GCVIEW_ORIGIN_FRONT_LEFT:  fx = +1; fy = +1; break;
    case LV_GCVIEW_ORIGIN_FRONT_RIGHT: fx = -1; fy = +1; break;
    case LV_GCVIEW_ORIGIN_BACK_LEFT:   fx = +1; fy = -1; break;
    case LV_GCVIEW_ORIGIN_BACK_RIGHT:  fx = -1; fy = -1; break;
    }

    priv->view.axis_flip_x = fx;
    priv->view.axis_flip_y = fy;
    priv->auto_fit_pending = true;
    lv_obj_invalidate(obj);
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
