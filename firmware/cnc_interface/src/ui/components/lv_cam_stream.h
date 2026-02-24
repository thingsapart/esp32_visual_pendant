// lv_cam_stream.h — LVGL camera stream view component
//
// Displays a live RGB565 camera feed driven by a cam_receiver_t.
// The widget is a container with an internal canvas that is automatically
// updated when a new frame arrives from the receiver.
//
// Usage:
//   lv_obj_t *cam = lv_cam_stream_create(parent);
//   lv_cam_stream_set_receiver(cam, receiver);
//
// Thread-safety:  set_receiver / get_receiver may be called from any task.
// All other property setters must be called from the LVGL thread.

#ifndef LV_CAM_STREAM_H
#define LV_CAM_STREAM_H

#include "lvgl.h"
#include "driver/cam_receiver.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Fit modes for scaling the camera image within the widget
// ---------------------------------------------------------------------------
typedef enum {
    LV_CAM_STREAM_FIT_CONTAIN,   ///< Scale to fit, preserving aspect ratio
    LV_CAM_STREAM_FIT_COVER,     ///< Scale to cover, preserving aspect ratio
    LV_CAM_STREAM_FIT_FILL,      ///< Stretch to fill (may distort)
    LV_CAM_STREAM_FIT_NONE,      ///< 1:1 pixels, centered
} lv_cam_stream_fit_t;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Create a camera stream view as a child of |parent|.
/// Returns the root container object (opaque lv_obj_t *).
lv_obj_t *lv_cam_stream_create(lv_obj_t *parent);

/// Attach the receiver that provides frame data.
/// Pass NULL to detach.  The widget subscribes to the receiver's frame-ready
/// callback and updates its display automatically.
void lv_cam_stream_set_receiver(lv_obj_t *obj, cam_receiver_t *receiver);

/// Return the currently attached receiver (may be NULL).
cam_receiver_t *lv_cam_stream_get_receiver(lv_obj_t *obj);

/// Set the image scaling / fit mode (default: CONTAIN).
void lv_cam_stream_set_fit(lv_obj_t *obj, lv_cam_stream_fit_t fit);
lv_cam_stream_fit_t lv_cam_stream_get_fit(lv_obj_t *obj);

/// Enable / disable an FPS + status overlay in the top-right corner.
void lv_cam_stream_set_show_info(lv_obj_t *obj, bool show);

/// Get the native image dimensions (0×0 if no frame received yet).
void lv_cam_stream_get_image_size(lv_obj_t *obj,
                                   uint16_t *out_width, uint16_t *out_height);

/// Force a refresh of the displayed image from the receiver's current buffer.
/// Normally not needed — the frame-ready callback triggers this automatically.
void lv_cam_stream_refresh(lv_obj_t *obj);

/// Return the internal image object (for advanced layout or styling).
lv_obj_t *lv_cam_stream_get_image_obj(lv_obj_t *obj);

// ---------------------------------------------------------------------------
// Module-level default receiver
// ---------------------------------------------------------------------------

/// Set a module-wide default receiver.  Any lv_cam_stream or lv_cam_positioning
/// widget created WITHOUT an explicit set_receiver call will automatically be
/// attached to this receiver instead of remaining unconnected.
/// Pass NULL to clear.
void lv_cam_stream_set_default_receiver(cam_receiver_t *receiver);

/// Return the module-wide default receiver (may be NULL).
cam_receiver_t *lv_cam_stream_get_default_receiver(void);

#ifdef __cplusplus
}
#endif
#endif // LV_CAM_STREAM_H
