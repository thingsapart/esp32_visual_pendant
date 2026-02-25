// lv_cam_positioning.h — Camera-based positioning view component
//
// Extends lv_cam_stream with interactive grid-based positioning features:
//  - POINT mode:     click to select a single point on the image
//  - RECTANGLE mode: click two corners to define an axis-aligned rectangle
//  - CIRCLE mode:    click center, then click edge to set radius
//
// All selected positions are reported as both pixel coordinates AND physical
// (machine) coordinates derived from the cam_receiver's grid calibration data.
//
// Visual style:
//  - Points: 10px diameter, outlined red, transparent red fill
//  - Rectangle edges: transparent blue (slightly darker), background blue/30%
//  - Circle outline: transparent blue, center point red
//
// Intended use-cases:
//  - Jog the CNC to a camera-selected point
//  - Define rectangle / circle probe features for the probing wizard

#ifndef LV_CAM_POSITIONING_H
#define LV_CAM_POSITIONING_H

#include "lvgl.h"
#include "driver/cam_receiver.h"
#include "machine/machine_interface.h"
#include "probe/probe_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------
typedef enum {
    LV_CAM_POS_MODE_NONE      = 0,  ///< View-only, no interaction
    LV_CAM_POS_MODE_POINT     = 1,  ///< Click to select a single point
    LV_CAM_POS_MODE_RECTANGLE = 2,  ///< Click two corners for a rectangle
    LV_CAM_POS_MODE_CIRCLE    = 3,  ///< Click center + edge for a circle
} lv_cam_pos_mode_t;

// ---------------------------------------------------------------------------
// Position data
// ---------------------------------------------------------------------------

/// A point in both pixel and physical coordinate spaces.
typedef struct {
    int16_t   px_x,   px_y;        ///< Pixel coordinates within the image
    float     phys_x, phys_y;      ///< Physical (machine) coordinates in mm
    bool      has_physical;         ///< true if grid calibration was available
} lv_cam_pos_point_t;

/// Rectangle defined by two corner points.
typedef struct {
    lv_cam_pos_point_t  corners[2]; ///< [0] = first click, [1] = second click
    float               width_mm;   ///< Physical width  (|c1.x - c0.x|)
    float               height_mm;  ///< Physical height (|c1.y - c0.y|)
} lv_cam_pos_rect_t;

/// Circle defined by center + radius.
typedef struct {
    lv_cam_pos_point_t  center;
    lv_cam_pos_point_t  edge;       ///< Point on the circumference
    float               radius_mm;  ///< Physical radius
    float               radius_px;  ///< Pixel radius
} lv_cam_pos_circle_t;

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------

/// Point selected (POINT mode).
typedef void (*lv_cam_pos_point_cb_t)(lv_obj_t *obj,
                                       const lv_cam_pos_point_t *pt,
                                       void *user_data);

/// Rectangle defined (RECTANGLE mode) — both corners set.
typedef void (*lv_cam_pos_rect_cb_t)(lv_obj_t *obj,
                                      const lv_cam_pos_rect_t *rect,
                                      void *user_data);

/// Circle defined (CIRCLE mode) — center + edge set.
typedef void (*lv_cam_pos_circle_cb_t)(lv_obj_t *obj,
                                        const lv_cam_pos_circle_t *circle,
                                        void *user_data);

// ---------------------------------------------------------------------------
// Public API — lifecycle
// ---------------------------------------------------------------------------

/// Create the camera-based positioning widget.
/// Internally creates an lv_cam_stream plus a transparent overlay.
lv_obj_t *lv_cam_positioning_create(lv_obj_t *parent);

/// Attach a receiver (forwarded to the internal cam_stream).
void lv_cam_positioning_set_receiver(lv_obj_t *obj, cam_receiver_t *receiver);

/// Return the attached receiver.
cam_receiver_t *lv_cam_positioning_get_receiver(lv_obj_t *obj);

/// Access the internal cam_stream widget (e.g. to set fit mode).
lv_obj_t *lv_cam_positioning_get_stream(lv_obj_t *obj);

/// Attach a machine_interface_t so the widget can read axis travel limits
/// (axis_min / axis_max) to choose a sensible physical-coordinate grid step.
/// Pass NULL to detach.
void lv_cam_positioning_set_machine(lv_obj_t *obj, machine_interface_t *machine);

/// Return the currently attached machine interface (may be NULL).
machine_interface_t *lv_cam_positioning_get_machine(lv_obj_t *obj);

// ---------------------------------------------------------------------------
// Mode control
// ---------------------------------------------------------------------------

/// Set the interaction mode.  Changing mode clears any in-progress shape.
void lv_cam_positioning_set_mode(lv_obj_t *obj, lv_cam_pos_mode_t mode);

lv_cam_pos_mode_t lv_cam_positioning_get_mode(lv_obj_t *obj);

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void lv_cam_positioning_set_point_cb(lv_obj_t *obj,
                                      lv_cam_pos_point_cb_t cb, void *user_data);
void lv_cam_positioning_set_rect_cb(lv_obj_t *obj,
                                     lv_cam_pos_rect_cb_t cb, void *user_data);
void lv_cam_positioning_set_circle_cb(lv_obj_t *obj,
                                       lv_cam_pos_circle_cb_t cb, void *user_data);

// ---------------------------------------------------------------------------
// Shape manipulation
// ---------------------------------------------------------------------------

/// Clear all drawn shapes and reset the current selection.
void lv_cam_positioning_clear_shapes(lv_obj_t *obj);

/// Programmatically set a point (useful for showing machine position).
void lv_cam_positioning_set_point(lv_obj_t *obj, const lv_cam_pos_point_t *pt);

/// Get the last selected point (NULL if none).
const lv_cam_pos_point_t *lv_cam_positioning_get_point(lv_obj_t *obj);

/// Get the current rectangle (NULL if incomplete).
const lv_cam_pos_rect_t *lv_cam_positioning_get_rect(lv_obj_t *obj);

/// Get the current circle (NULL if incomplete).
const lv_cam_pos_circle_t *lv_cam_positioning_get_circle(lv_obj_t *obj);

// ---------------------------------------------------------------------------
// Coordinate mapping
// ---------------------------------------------------------------------------

/// Convert pixel coordinates to physical (machine) coordinates using the
/// receiver's grid.  Returns false if no grid is available.
bool lv_cam_positioning_pixel_to_physical(lv_obj_t *obj,
                                           int16_t px_x, int16_t px_y,
                                           float *out_phys_x, float *out_phys_y);

/// Returns true if the receiver has a valid grid for coordinate mapping.
bool lv_cam_positioning_has_grid(lv_obj_t *obj);

// ---------------------------------------------------------------------------
// Probe wizard API
// ---------------------------------------------------------------------------

/// Return the widget's internal probe_api_ctx_t so callers (e.g. interface.c)
/// can pass it to mos_probe_handler_fill_callbacks() before calling
/// lv_cam_positioning_set_probe_cbs().  The pointer is stable for the widget's
/// lifetime; it must not be freed or re-initialised by the caller.
probe_api_ctx_t *lv_cam_positioning_get_probe_ctx(lv_obj_t *obj);

/// Connect probe command callbacks so the widget can dispatch machine
/// operations when the user confirms a probe/move action.
///
/// The widget creates and owns the probe_api_ctx_t internally.
/// Only the four cmd_* callbacks and the caller's user_data are used from
/// @p cbs; the widget registers its own event (on_status / on_error) handlers.
///
/// Call this once after lv_cam_positioning_create(), before the first probe
/// action is attempted.  Safe to call again to change callbacks at runtime.
void lv_cam_positioning_set_probe_cbs(lv_obj_t *obj,
                                       const probe_api_callbacks_t *cbs);

/// Programmatically cancel any active wizard step (dismisses the popover,
/// instruction bar, or confirmation modal) and returns to idle state.
/// Calling this while a probe operation is in-flight also cancels the op.
void lv_cam_positioning_wizard_cancel(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif
#endif // LV_CAM_POSITIONING_H
