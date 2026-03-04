/**
 * @file gcode_view.c
 * @brief View projection & grid logic for the G-Code visualizer.
 */

#include "gcode_view.h"
#include "gcode_sim.h"
#include <math.h>
#include <float.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Forward declarations.
static inline gc_vec3_t flip_world(const gc_view_t *view, gc_vec3_t w);

/* ─── isometric projection constants ───────────────────────────────────── */

/* Standard isometric angles: 30° from the horizontal.
 * cos(30°) ≈ 0.866,  sin(30°) = 0.5 */
#define ISO_COS  0.86602540378f
#define ISO_SIN  0.50000000000f

/* Isometric projection:
 *   screen_x =  (x - z) * cos(30°)
 *   screen_y = -(x + z) * sin(30°) - y          (Y is up in CNC)
 * Scaled by ppm. */

/* ═══════════════════════════════════════════════════════════════════════════
 * Axis labels per view
 * ═══════════════════════════════════════════════════════════════════════════ */

void gc_view_axis_labels(gc_view_mode_t mode,
                         const char **h_label, const char **v_label)
{
    static const char *labels[][2] = {
        [GCVIEW_TOP]       = { "X", "Y" },
        [GCVIEW_FRONT]     = { "X", "Z" },
        [GCVIEW_RIGHT]     = { "Y", "Z" },
        [GCVIEW_LEFT]      = { "Y", "Z" },
        [GCVIEW_BACK]      = { "X", "Z" },
        [GCVIEW_ISOMETRIC] = { "X", "Z" },
    };
    if (mode >= GCVIEW_COUNT) mode = GCVIEW_TOP;
    if (h_label) *h_label = labels[mode][0];
    if (v_label) *v_label = labels[mode][1];
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Projection
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Orthographic projection: extract the two in-plane axes. */
static inline void ortho_project(gc_view_mode_t mode,
                                 gc_vec3_t w,
                                 float *out_h, float *out_v)
{
    switch (mode) {
    case GCVIEW_TOP:
        *out_h =  w.x;
        *out_v = -w.y;      /* Y increases downward on screen for CNC top view */
        break;
    case GCVIEW_FRONT:
        *out_h =  w.x;
        *out_v = -w.z;      /* Z up → screen up (negate for screen coords) */
        break;
    case GCVIEW_RIGHT:
        *out_h =  w.y;
        *out_v = -w.z;
        break;
    case GCVIEW_LEFT:
        *out_h = -w.y;      /* flipped */
        *out_v = -w.z;
        break;
    case GCVIEW_BACK:
        *out_h = -w.x;      /* flipped */
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

void gc_view_project(const gc_view_t *view, gc_vec3_t world, gc_pt2_t *out) {
    float h, v;
    ortho_project(view->mode, flip_world(view, world), &h, &v);

    /* World mm → screen px: multiply by ppm, add pan, add viewport centre. */
    out->x = (int16_t)(h * view->ppm + view->pan_x + view->vp_x + view->vp_w / 2);
    out->y = (int16_t)(v * view->ppm + view->pan_y + view->vp_y + view->vp_h / 2);
}

void gc_view_unproject(const gc_view_t *view, gc_pt2_t screen,
                       float depth_mm, gc_vec3_t *out)
{
    /* Reverse the screen→world transform for orthographic views. */
    float sx = (float)(screen.x - view->vp_x - view->vp_w / 2) - view->pan_x;
    float sy = (float)(screen.y - view->vp_y - view->vp_h / 2) - view->pan_y;

    float h = sx / view->ppm;
    float v = sy / view->ppm;

    switch (view->mode) {
    case GCVIEW_TOP:
        out->x = h; out->y = -v; out->z = depth_mm;
        break;
    case GCVIEW_FRONT:
        out->x = h; out->y = depth_mm; out->z = -v;
        break;
    case GCVIEW_RIGHT:
        out->x = depth_mm; out->y = h; out->z = -v;
        break;
    case GCVIEW_LEFT:
        out->x = depth_mm; out->y = -h; out->z = -v;
        break;
    case GCVIEW_BACK:
        out->x = -h; out->y = depth_mm; out->z = -v;
        break;
    case GCVIEW_ISOMETRIC:
        /* Can't fully invert iso — approximate by using depth=0 */
        out->x = h; out->y = 0; out->z = -v;
        break;
    default:
        out->x = h; out->y = -v; out->z = depth_mm;
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Auto-fit
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Apply the per-axis flip flags from @p view to a world point before it is
 *  passed to ortho_project().  This helper centralises the flip logic so that
 *  gc_view_fit() and gc_view_iter_grid() produce results consistent with
 *  gc_view_project(). */
static inline gc_vec3_t flip_world(const gc_view_t *view, gc_vec3_t w)
{
    if (view->axis_flip_x) w.x *= (float)view->axis_flip_x;
    if (view->axis_flip_y) w.y *= (float)view->axis_flip_y;
    return w;
}

void gc_view_fit(gc_view_t *view,
                 const gc_segbuf_t *buf,
                 const float *machine_min,
                 const float *machine_max,
                 int margin_px)
{
    float min_h =  FLT_MAX, min_v =  FLT_MAX;
    float max_h = -FLT_MAX, max_v = -FLT_MAX;
    bool has_data = false;

    /* Include machine limits if provided */
    if (machine_min && machine_max) {
        gc_vec3_t corners[8];
        /* Build all 8 corners of the machine volume */
        for (int i = 0; i < 8; i++) {
            corners[i].x = (i & 1) ? machine_max[0] : machine_min[0];
            corners[i].y = (i & 2) ? machine_max[1] : machine_min[1];
            corners[i].z = (i & 4) ? machine_max[2] : machine_min[2];
        }
        for (int i = 0; i < 8; i++) {
            float h, v;
            ortho_project(view->mode, flip_world(view, corners[i]), &h, &v);
            if (h < min_h) min_h = h;
            if (h > max_h) max_h = h;
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
            has_data = true;
        }
    }

    /* Include all segment endpoints */
    if (buf) {
        for (uint16_t i = 0; i < buf->count; i++) {
            const gc_segment_t *seg = gc_segbuf_get(buf, i);
            if (!seg) continue;

            float h, v;
            ortho_project(view->mode, flip_world(view, seg->from), &h, &v);
            if (h < min_h) min_h = h;  if (h > max_h) max_h = h;
            if (v < min_v) min_v = v;  if (v > max_v) max_v = v;

            ortho_project(view->mode, flip_world(view, seg->to), &h, &v);
            if (h < min_h) min_h = h;  if (h > max_h) max_h = h;
            if (v < min_v) min_v = v;  if (v > max_v) max_v = v;

            has_data = true;
        }
    }

    if (!has_data) {
        /* Default: show ±100mm */
        min_h = -100; max_h = 100;
        min_v = -100; max_v = 100;
    }

    /* Compute zoom to fit */
    float range_h = max_h - min_h;
    float range_v = max_v - min_v;
    if (range_h < 1.0f) range_h = 1.0f;
    if (range_v < 1.0f) range_v = 1.0f;

    float avail_w = (float)(view->vp_w - 2 * margin_px);
    float avail_h = (float)(view->vp_h - 2 * margin_px);
    if (avail_w < 10.0f) avail_w = 10.0f;
    if (avail_h < 10.0f) avail_h = 10.0f;

    float ppm_h = avail_w / range_h;
    float ppm_v = avail_h / range_v;
    view->ppm = (ppm_h < ppm_v) ? ppm_h : ppm_v;

    /* Clamp zoom */
    if (view->ppm < GCVIEW_MIN_PPM) view->ppm = GCVIEW_MIN_PPM;
    if (view->ppm > GCVIEW_MAX_PPM) view->ppm = GCVIEW_MAX_PPM;

    /* Centre the content */
    float centre_h = (min_h + max_h) / 2.0f;
    float centre_v = (min_v + max_v) / 2.0f;
    view->pan_x = -centre_h * view->ppm;
    view->pan_y = -centre_v * view->ppm;

    view->dirty = true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Grid
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Pick a "nice" grid spacing for the current zoom. */
static const float nice_steps[] = {
    0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f,
    10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f
};
#define N_NICE (sizeof(nice_steps) / sizeof(nice_steps[0]))

float gc_view_grid_spacing(const gc_view_t *view) {
    /* Want roughly 40-80 px between grid lines */
    float ideal_mm = 50.0f / view->ppm;

    for (size_t i = 0; i < N_NICE; i++) {
        if (nice_steps[i] >= ideal_mm) return nice_steps[i];
    }
    return nice_steps[N_NICE - 1];
}

void gc_view_iter_grid(const gc_view_t *view,
                       const float *machine_min,
                       const float *machine_max,
                       gc_grid_line_cb_t cb, void *ctx)
{
    if (!cb) return;

    float spacing = gc_view_grid_spacing(view);

    /* Determine the world-space range visible in the viewport. */
    gc_pt2_t tl = { view->vp_x,                       view->vp_y };
    gc_pt2_t br = { (int16_t)(view->vp_x + view->vp_w - 1),
                    (int16_t)(view->vp_y + view->vp_h - 1) };

    gc_vec3_t w_tl, w_br;
    gc_view_unproject(view, tl, 0, &w_tl);
    gc_view_unproject(view, br, 0, &w_br);

    /* Get the two in-plane values */
    float h0, v0, h1, v1;
    ortho_project(view->mode, w_tl, &h0, &v0);
    ortho_project(view->mode, w_br, &h1, &v1);

    /* Ensure min < max */
    if (h0 > h1) { float t = h0; h0 = h1; h1 = t; }
    if (v0 > v1) { float t = v0; v0 = v1; v1 = t; }

    /* For isometric the unproject→ortho_project round-trip gives incorrect
     * world X/Y bounds because gc_view_unproject cannot fully invert the iso
     * projection (it approximates with y=0).  Instead, directly invert the
     * iso formula for all four viewport corners at z=0 to obtain true world
     * X (→h) and world Y (→v) extents.
     *
     *   screen_h = (wx - wy) * ISO_COS   ⟹  wx - wy = sh / ISO_COS
     *   screen_v = -(wx + wy) * ISO_SIN  ⟹  wx + wy = -sv / ISO_SIN
     *   ⟹  wx = (sh/ISO_COS + (-sv/ISO_SIN)) / 2
     *       wy = ((-sv/ISO_SIN) - sh/ISO_COS) / 2
     */
    if (view->mode == GCVIEW_ISOMETRIC) {
        gc_pt2_t corners[4] = {
            tl,
            { br.x,  tl.y },
            { tl.x,  br.y },
            br
        };
        float scx = (float)(view->vp_x + view->vp_w / 2);
        float scy = (float)(view->vp_y + view->vp_h / 2);
        /* Per-axis flip: for \u00b11 values, 1/f == f, so we just multiply. */
        float fx = (view->axis_flip_x != 0) ? (float)view->axis_flip_x : 1.0f;
        float fy = (view->axis_flip_y != 0) ? (float)view->axis_flip_y : 1.0f;
        float wx[4], wy[4];
        for (int c = 0; c < 4; c++) {
            float sh = ((float)corners[c].x - scx - view->pan_x) / view->ppm;
            float sv = ((float)corners[c].y - scy - view->pan_y) / view->ppm;
            /* With flip:
             *  sh = (wx*fx - wy*fy) * ISO_COS
             *  -sv/ISO_SIN = wx*fx + wy*fy
             *  => wx = (sh/ISO_COS + (-sv/ISO_SIN)) * 0.5 * fx
             *     wy = ((-sv/ISO_SIN) - sh/ISO_COS) * 0.5 * fy  */
            wx[c] = (sh / ISO_COS + (-sv / ISO_SIN)) * 0.5f * fx;
            wy[c] = ((-sv / ISO_SIN) - (sh / ISO_COS)) * 0.5f * fy;
        }
        h0 = h1 = wx[0];
        v0 = v1 = wy[0];
        for (int c = 1; c < 4; c++) {
            if (wx[c] < h0) h0 = wx[c];  if (wx[c] > h1) h1 = wx[c];
            if (wy[c] < v0) v0 = wy[c];  if (wy[c] > v1) v1 = wy[c];
        }
        /* Now h0..h1 = world X range, v0..v1 = world Y range — exactly what
         * the iso grid loops below expect:
         *   X-sweep: gc_vec3(x, v0..v1, 0)   (constant X, varying Y)
         *   Y-sweep: gc_vec3(h0..h1, y, 0)   (constant Y, varying X)   */
    }

    /* Extend to grid boundaries */
    float g_h0 = floorf(h0 / spacing) * spacing;
    float g_v0 = floorf(v0 / spacing) * spacing;

    /* Determine major line interval (every 5 lines) */
    float major_spacing = spacing * 5.0f;

    /* Safety cap: skip grid if spacing is too fine or range too large */
    int max_lines = GCVIEW_MAX_GRID_LINES;
    int drawn = 0;

    /* Vertical grid lines (sweep h-axis) */
    for (float h = g_h0; h <= h1 && drawn < max_lines; h += spacing, drawn++) {
        bool is_major = (fabsf(fmodf(h, major_spacing)) < spacing * 0.01f) ||
                        (fabsf(h) < spacing * 0.01f);  /* origin */

        gc_pt2_t p1, p2;
        /* Build world points along the v-axis at this h value */
        gc_vec3_t w1, w2;
        /* We use a trick: create world points via the unproject inverse.
         * For orthographic views we can directly construct them. */
        w1 = w2 = gc_vec3(0, 0, 0);

        /* For each view, map (h, v) → (x,y,z) */
        switch (view->mode) {
        case GCVIEW_TOP:
            w1 = gc_vec3(h, -v0, 0); w2 = gc_vec3(h, -v1, 0); break;
        case GCVIEW_FRONT:
            w1 = gc_vec3(h, 0, -v0); w2 = gc_vec3(h, 0, -v1); break;
        case GCVIEW_RIGHT:
            w1 = gc_vec3(0, h, -v0); w2 = gc_vec3(0, h, -v1); break;
        case GCVIEW_LEFT:
            w1 = gc_vec3(0, -h, -v0); w2 = gc_vec3(0, -h, -v1); break;
        case GCVIEW_BACK:
            w1 = gc_vec3(-h, 0, -v0); w2 = gc_vec3(-h, 0, -v1); break;
        case GCVIEW_ISOMETRIC:
            /* For iso grid, we draw lines in the XY plane at Z=0 */
            w1 = gc_vec3(h, v0, 0); w2 = gc_vec3(h, v1, 0);
            break;
        default:
            w1 = gc_vec3(h, -v0, 0); w2 = gc_vec3(h, -v1, 0); break;
        }

        gc_view_project(view, w1, &p1);
        gc_view_project(view, w2, &p2);
        cb(is_major, p1, p2, ctx);
    }

    /* Horizontal grid lines (sweep v-axis) */
    for (float v = g_v0; v <= v1 && drawn < max_lines; v += spacing, drawn++) {
        bool is_major = (fabsf(fmodf(v, major_spacing)) < spacing * 0.01f) ||
                        (fabsf(v) < spacing * 0.01f);

        gc_vec3_t w1, w2;
        w1 = w2 = gc_vec3(0, 0, 0);

        switch (view->mode) {
        case GCVIEW_TOP:
            w1 = gc_vec3(h0, -v, 0); w2 = gc_vec3(h1, -v, 0); break;
        case GCVIEW_FRONT:
            w1 = gc_vec3(h0, 0, -v); w2 = gc_vec3(h1, 0, -v); break;
        case GCVIEW_RIGHT:
            w1 = gc_vec3(0, h0, -v); w2 = gc_vec3(0, h1, -v); break;
        case GCVIEW_LEFT:
            w1 = gc_vec3(0, -h0, -v); w2 = gc_vec3(0, -h1, -v); break;
        case GCVIEW_BACK:
            w1 = gc_vec3(-h0, 0, -v); w2 = gc_vec3(-h1, 0, -v); break;
        case GCVIEW_ISOMETRIC:
            w1 = gc_vec3(h0, v, 0); w2 = gc_vec3(h1, v, 0);
            break;
        default:
            w1 = gc_vec3(h0, -v, 0); w2 = gc_vec3(h1, -v, 0); break;
        }

        gc_pt2_t p1, p2;
        gc_view_project(view, w1, &p1);
        gc_view_project(view, w2, &p2);
        cb(is_major, p1, p2, ctx);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * View lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

void gc_view_init(gc_view_t *view, gc_view_mode_t mode,
                  int16_t vp_x, int16_t vp_y,
                  int16_t vp_w, int16_t vp_h)
{
    memset(view, 0, sizeof(*view));
    view->mode        = mode;
    view->ppm         = GCVIEW_DEFAULT_PPM;
    view->vp_x        = vp_x;
    view->vp_y        = vp_y;
    view->vp_w        = vp_w;
    view->vp_h        = vp_h;
    view->dirty       = true;
    view->axis_flip_x = 1;  /* no flip by default */
    view->axis_flip_y = 1;
}

void gc_view_set_mode(gc_view_t *view, gc_view_mode_t mode) {
    view->mode  = mode;
    view->pan_x = 0;
    view->pan_y = 0;
    view->dirty = true;
}

void gc_view_set_viewport(gc_view_t *view,
                          int16_t vp_x, int16_t vp_y,
                          int16_t vp_w, int16_t vp_h)
{
    view->vp_x  = vp_x;
    view->vp_y  = vp_y;
    view->vp_w  = vp_w;
    view->vp_h  = vp_h;
    view->dirty = true;
}

void gc_view_pan(gc_view_t *view, float dx, float dy) {
    view->pan_x += dx;
    view->pan_y += dy;
    view->dirty  = true;
}

void gc_view_zoom(gc_view_t *view, float factor, int16_t sx, int16_t sy) {
    /* Zoom centred on (sx, sy):
     *  1. Convert screen point to world-relative offset.
     *  2. Scale ppm.
     *  3. Adjust pan so the same world point stays under the cursor.
     */
    float cx = (float)(view->vp_x + view->vp_w / 2);
    float cy = (float)(view->vp_y + view->vp_h / 2);

    float wx = ((float)sx - cx - view->pan_x) / view->ppm;
    float wy = ((float)sy - cy - view->pan_y) / view->ppm;

    float new_ppm = view->ppm * factor;
    if (new_ppm < GCVIEW_MIN_PPM) new_ppm = GCVIEW_MIN_PPM;
    if (new_ppm > GCVIEW_MAX_PPM) new_ppm = GCVIEW_MAX_PPM;

    view->pan_x = (float)sx - cx - wx * new_ppm;
    view->pan_y = (float)sy - cy - wy * new_ppm;
    view->ppm   = new_ppm;
    view->dirty  = true;
}
