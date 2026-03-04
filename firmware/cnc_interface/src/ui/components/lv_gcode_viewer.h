/**
 * @file lv_gcode_viewer.h
 * @brief LVGL component for visualising CNC toolpaths and machine state.
 *
 * Drop-in widget that:
 *  - Renders the current machine position, WCS origin, machine limits & grid.
 *  - Parses and simulates G-code snippets to draw a toolpath overlay.
 *  - Supports multiple orthographic + isometric view projections.
 *  - Uses direct LVGL draw API (LV_EVENT_DRAW_MAIN_END) for speed.
 *  - Designed for ESP32-S3 with tight memory constraints.
 *
 * Usage:
 *   lv_obj_t *viewer = lv_gcode_viewer_create(parent);
 *   lv_gcode_viewer_set_machine(viewer, machine);
 *   lv_gcode_viewer_set_view(viewer, LV_GCVIEW_TOP);
 *   lv_gcode_viewer_load_gcode(viewer, "G1 X10 Y20 F600\nG1 X30\n");
 */
#ifndef LV_GCODE_VIEWER_H
#define LV_GCODE_VIEWER_H

#include "lvgl.h"
#include "gcode_viewer/gcode_types.h"

/* Forward-declare — avoid pulling in the whole header everywhere */
struct machine_interface_t;

#ifdef __cplusplus
extern "C" {
#endif

/** LV-specific view enum mirroring `gc_view_mode_t`.
 *  This isolates LVGL-facing code from the internal parser/renderer enum. */
typedef enum {
    LV_GCVIEW_TOP       = 0,
    LV_GCVIEW_FRONT     = 1,
    LV_GCVIEW_RIGHT     = 2,
    LV_GCVIEW_LEFT      = 3,
    LV_GCVIEW_BACK      = 4,
    LV_GCVIEW_ISOMETRIC = 5,
    LV_GCVIEW_COUNT     = 6,
} lv_gcode_viewer_view_t;

/** Machine coordinate origin (home / hard-limit corner) convention.
 *
 *  Describes where the machine's physical home position is relative to
 *  the operator standing at the front of the machine.  The viewer uses
 *  this to orient the X and Y axes so the on-screen layout matches the
 *  physical machine:
 *
 *         BACK_LEFT   BACK_RIGHT
 *            ┌──────────┐
 *            │  machine │
 *            └──────────┘
 *        FRONT_LEFT  FRONT_RIGHT  ← operator
 *
 *  Common Grbl routers with home switches at back-right: use
 *  LV_GCVIEW_ORIGIN_BACK_RIGHT so the workspace (negative machine
 *  coordinates) fills the lower-left of the top view as expected.
 */
typedef enum {
    LV_GCVIEW_ORIGIN_FRONT_LEFT  = 0,  /**< X+→right, Y+→away  (home front-left)  */
    LV_GCVIEW_ORIGIN_FRONT_RIGHT = 1,  /**< X+→left,  Y+→away  (home front-right) */
    LV_GCVIEW_ORIGIN_BACK_LEFT   = 2,  /**< X+→right, Y+→toward (home back-left) */
    LV_GCVIEW_ORIGIN_BACK_RIGHT  = 3,  /**< X+→left,  Y+→toward (home back-right, typical Grbl) */
} lv_gcview_machine_origin_t;

/* ─── lifecycle ────────────────────────────────────────────────────────── */

/** Create the G-code viewer widget.
 *  The widget fills its parent (LV_PCT(100) × LV_PCT(100)).
 *  @return the root lv_obj_t, or NULL on alloc failure. */
lv_obj_t *lv_gcode_viewer_create(lv_obj_t *parent);

/** Attach a machine_interface for live position / limits data.
 *  The viewer registers a position-change callback. */
void lv_gcode_viewer_set_machine(lv_obj_t *obj,
                                 struct machine_interface_t *machine);

/* ─── G-code input ─────────────────────────────────────────────────────── */

/** Load a block of G-code text.  The viewer parses and simulates it,
 *  seeding the simulator from the current machine state.
 *  The text is NOT copied — caller must keep it alive or call with a
 *  heap-allocated copy that the viewer will *not* free.
 *  Pass NULL to clear. */
void lv_gcode_viewer_load_gcode(lv_obj_t *obj, const char *gcode_text);

/** Append a single G-code line (e.g. from a streaming queue).
 *  The line is parsed and simulated incrementally. */
void lv_gcode_viewer_append_line(lv_obj_t *obj, const char *line);

/** Clear all toolpath data.  Machine state / view are preserved. */
void lv_gcode_viewer_clear(lv_obj_t *obj);

/** Mark all current segments as "done" (dimmed rendering). */
void lv_gcode_viewer_mark_done(lv_obj_t *obj);

/* ─── view control ─────────────────────────────────────────────────────── */

/** Set the view projection mode. */
void lv_gcode_viewer_set_view(lv_obj_t *obj, lv_gcode_viewer_view_t mode);

/** Get the current view mode. */
lv_gcode_viewer_view_t lv_gcode_viewer_get_view(lv_obj_t *obj);

/** Cycle to the next view mode.  Wraps around. */
void lv_gcode_viewer_next_view(lv_obj_t *obj);

/** Zoom to fit all visible content (toolpath + machine limits). */
void lv_gcode_viewer_fit(lv_obj_t *obj);

/** Programmatic zoom.  factor > 1 = zoom in, < 1 = zoom out.
 *  Zooms around the viewport centre. */
void lv_gcode_viewer_zoom(lv_obj_t *obj, float factor);

/** Get the human-readable name of the current view (e.g. "Top (XY)"). */
const char *lv_gcode_viewer_view_name(lv_obj_t *obj);

/* ─── appearance toggles ───────────────────────────────────────────────── */

/** Show / hide the grid. */
void lv_gcode_viewer_set_grid(lv_obj_t *obj, bool show);

/** Show / hide machine limit rectangle. */
void lv_gcode_viewer_set_limits(lv_obj_t *obj, bool show);

/** Show / hide the tool / spindle position marker. */
void lv_gcode_viewer_set_position(lv_obj_t *obj, bool show);

/** Show / hide the WCS origin marker. */
void lv_gcode_viewer_set_wcs_origin(lv_obj_t *obj, bool show);

/** Set the machine coordinate origin convention.
 *
 *  This controls the orientation of the X and Y axes in the viewer so
 *  the displayed layout matches the physical machine.  Call this once
 *  after creating the viewer (before the first fit / draw).
 *
 *  Default: LV_GCVIEW_ORIGIN_FRONT_LEFT (X+→right, Y+→away from operator).
 */
void lv_gcode_viewer_set_machine_origin(lv_obj_t *obj,
                                         lv_gcview_machine_origin_t origin);

/** Force a redraw (invalidates the widget). */
void lv_gcode_viewer_invalidate(lv_obj_t *obj);

/* ─── data access ──────────────────────────────────────────────────────── */

/** Get the segment buffer (read-only) for external inspection. */
const gc_segbuf_t *lv_gcode_viewer_get_segments(lv_obj_t *obj);

/** Get the simulator state (read-only). */
const gc_sim_state_t *lv_gcode_viewer_get_sim_state(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* LV_GCODE_VIEWER_H */
