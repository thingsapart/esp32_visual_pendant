/**
 * @file gcode_view.h
 * @brief View transform & projection for the G-Code visualizer.
 *
 * Converts world-space 3D coordinates (mm) to screen-space 2D pixels,
 * supporting top, front, right, left, back, and isometric views.
 *
 * Each view also provides auto-fit (zoom-to-extents) and grid drawing data.
 */
#ifndef GCODE_VIEW_H
#define GCODE_VIEW_H

#include "gcode_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── axis labels for each view ────────────────────────────────────────── */

/** Get the two axis labels for a given view mode (e.g. "X","Y" for TOP). */
void gc_view_axis_labels(gc_view_mode_t mode,
                         const char **h_label, const char **v_label);

/* ─── transforms ───────────────────────────────────────────────────────── */

/** Project a 3D world point to 2D screen coordinates according to the
 *  current view settings.
 *
 *  @param view  View transform state.
 *  @param world 3D point in mm (machine coordinates).
 *  @param out   Output 2D point in screen pixels.
 */
void gc_view_project(const gc_view_t *view, gc_vec3_t world, gc_pt2_t *out);

/** Inverse: screen pixel to world coordinates.  The "depth" axis is
 *  set to @p depth_mm.  For orthographic views the mapping is exact;
 *  for isometric only the projection plane is solved. */
void gc_view_unproject(const gc_view_t *view, gc_pt2_t screen,
                       float depth_mm, gc_vec3_t *out);

/* ─── auto-fit ─────────────────────────────────────────────────────────── */

/** Compute the bounding box of all segments in @p buf projected through
 *  the current view mode, then adjust zoom (ppm) and pan so the whole
 *  toolpath fits inside the viewport with a small margin.
 *
 *  @param view       View to update (ppm, pan_x, pan_y modified).
 *  @param buf        Segment buffer.
 *  @param machine_min  Machine axis min [3] in mm (may be NULL).
 *  @param machine_max  Machine axis max [3] in mm (may be NULL).
 *  @param margin_px  Margin in pixels around the content (default 20).
 */
void gc_view_fit(gc_view_t *view,
                 const gc_segbuf_t *buf,
                 const float *machine_min,
                 const float *machine_max,
                 int margin_px);

/* ─── grid helpers ─────────────────────────────────────────────────────── */

/** Compute a "nice" grid spacing based on the current zoom level.
 *  Returns the spacing in mm. */
float gc_view_grid_spacing(const gc_view_t *view);

/** Callback for grid line iteration.
 *  @param is_major  true for every Nth line (typically every 5th).
 *  @param p1, p2    Line endpoints in screen pixels.
 *  @param ctx       User context.
 */
typedef void (*gc_grid_line_cb_t)(bool is_major,
                                  gc_pt2_t p1, gc_pt2_t p2,
                                  void *ctx);

/** Iterate grid lines for the current view.  Calls @p cb for each line.
 *  @param view  Current view state.
 *  @param machine_min  Machine axis min [3] in mm (may be NULL).
 *  @param machine_max  Machine axis max [3] in mm (may be NULL).
 *  @param cb  Callback for each grid line.
 *  @param ctx  User data passed to callback.
 */
void gc_view_iter_grid(const gc_view_t *view,
                       const float *machine_min,
                       const float *machine_max,
                       gc_grid_line_cb_t cb, void *ctx);

/* ─── view initialisation ──────────────────────────────────────────────── */

/** Set up view defaults for a given mode and viewport size. */
void gc_view_init(gc_view_t *view, gc_view_mode_t mode,
                  int16_t vp_x, int16_t vp_y,
                  int16_t vp_w, int16_t vp_h);

/** Change the view mode, resetting pan but keeping zoom. */
void gc_view_set_mode(gc_view_t *view, gc_view_mode_t mode);

/** Update viewport size (e.g. on resize). */
void gc_view_set_viewport(gc_view_t *view,
                          int16_t vp_x, int16_t vp_y,
                          int16_t vp_w, int16_t vp_h);

/** Apply a pan delta (in pixels). */
void gc_view_pan(gc_view_t *view, float dx, float dy);

/** Apply a zoom delta centred on screen point (sx, sy).
 *  @param factor  >1 = zoom in, <1 = zoom out. */
void gc_view_zoom(gc_view_t *view, float factor, int16_t sx, int16_t sy);

#ifdef __cplusplus
}
#endif

#endif /* GCODE_VIEW_H */
