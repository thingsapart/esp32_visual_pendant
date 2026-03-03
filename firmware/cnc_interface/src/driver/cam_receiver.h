// cam_receiver.h — Camera stream receiver / frame manager
//
// Receives cam-protocol messages from a transport layer, assembles tiles into
// complete RGB565 frames, stores the latest grid-mapping data, and notifies
// subscribers via callbacks.
//
// Thread-safety:  All public functions are safe to call from any task.
// The frame buffer is protected by a lightweight mutex; callers that need to
// read pixel data directly should bracket the access with
//   cam_receiver_lock_display() / cam_receiver_unlock_display().
// The calibration grid (px_points and points arrays) is protected by a
// separate grid mutex; callers that iterate grid data should bracket with
//   cam_receiver_lock_grid() / cam_receiver_unlock_grid().

#ifndef CAM_RECEIVER_H
#define CAM_RECEIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "cam_transport.h"
#include "cam_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Published data types
// ---------------------------------------------------------------------------

/// Snapshot description handed to frame-ready subscribers.
typedef struct {
    uint16_t        width;
    uint16_t        height;
    const uint8_t  *data;          ///< RGB565 pixel data (row-major, LE)
    size_t          data_size;     ///< width * height * 2
    uint16_t        frame_id;
    uint32_t        timestamp_ms;  ///< millis() at reception of FRAME_END
} cam_frame_info_t;

/// Parsed grid calibration data (pixel ↔ physical coordinate mapping).
/// Grid point (i, j) maps pixel position
///   px = i * img_width / (nx - 1),  py = j * img_height / (ny - 1)
/// to physical coordinates  points[(j * nx + i) * 2 + 0/1].
typedef struct {
    float       min_x, max_x;     ///< Physical X bounds (mm)
    float       min_y, max_y;     ///< Physical Y bounds (mm)
    float       dx, dy;           ///< Physical spacing between grid points
    uint16_t    nx, ny;           ///< Grid dimensions (columns × rows)
    uint16_t    point_count;      ///< Total number of points (nx * ny)
    float      *points;           ///< Flat array [x0,y0, x1,y1, …]  (heap)
    /// Actual calibration image pixel positions, normalised 0..1.
    /// Layout: [norm_x0, norm_y0, norm_x1, norm_y1, …]  (heap; NULL if
    /// the sender did not include image dimensions in the grid message).
    float      *px_points;
    uint16_t    src_img_w;        ///< Calibration image width used (0 = unknown)
    uint16_t    src_img_h;        ///< Calibration image height used (0 = unknown)
    /// Grid insets in output pixels — the grid mapping starts/ends inset
    /// from the image edges, defining a margin around the work area.
    uint16_t    inset_left;
    uint16_t    inset_top;
    uint16_t    inset_right;
    uint16_t    inset_bottom;
} cam_grid_info_t;

/// Camera device status (from CAM_MSG_STATUS heartbeats).
typedef struct {
    uint8_t     status;           ///< cam_device_status_t
    uint16_t    frame_id;
    uint8_t     fps_x10;          ///< FPS × 10
    uint8_t     wifi_channel;
    uint8_t     mac[6];
} cam_device_status_msg_t;

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------

/// Called when a complete frame (key or diff) is ready.
/// |frame| is valid only for the duration of the callback; if you need the
/// data later, copy it.  The receiver's display mutex is NOT held during
/// the callback — acquire it if you need to read frame->data directly.
typedef void (*cam_frame_ready_cb_t)(const cam_frame_info_t *frame,
                                      void *user_data);

/// Called when new grid calibration data is received.
typedef void (*cam_grid_update_cb_t)(const cam_grid_info_t *grid,
                                      void *user_data);

/// Called when a camera status heartbeat is received.
typedef void (*cam_status_cb_t)(const cam_device_status_msg_t *status,
                                 void *user_data);

// ---------------------------------------------------------------------------
// Receiver configuration
// ---------------------------------------------------------------------------

#define CAM_RECEIVER_MAX_CALLBACKS  4
#define CAM_RECEIVER_MAX_TILES      (8 * 8)   // Up to 8×8 tile grid
#define CAM_RECEIVER_TILE_BUF_SIZE  (4 * 1024) // Max JPEG bytes per tile (80×80 JPEG ≈ 1–2 KB)
#define CAM_RECEIVER_POOL_SIZE      2          // JPEG buffer pool slots (tiles decode
                                               // immediately, so ≤1–2 in flight at once)

/// Opaque receiver handle.
typedef struct cam_receiver cam_receiver_t;

/// Configuration for cam_receiver_create().
typedef struct {
    cam_transport_t *transport;        ///< Required — ownership NOT transferred
    uint16_t         max_width;        ///< Maximum supported image width  (0 = 640)
    uint16_t         max_height;       ///< Maximum supported image height (0 = 480)
    uint16_t         desired_width;    ///< Desired output width  (0 = don't-care)
    uint16_t         desired_height;   ///< Desired output height (0 = don't-care)
} cam_receiver_config_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/// Create a receiver.  Returns NULL on allocation failure.
cam_receiver_t *cam_receiver_create(const cam_receiver_config_t *cfg);

/// Destroy the receiver and free all resources.
void cam_receiver_destroy(cam_receiver_t *self);

/// Start receiving (hooks into the transport's rx callback).
void cam_receiver_start(cam_receiver_t *self);

/// Stop receiving.
void cam_receiver_stop(cam_receiver_t *self);

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

void cam_receiver_add_frame_cb(cam_receiver_t *self,
                                cam_frame_ready_cb_t cb, void *user_data);
void cam_receiver_remove_frame_cb(cam_receiver_t *self,
                                   cam_frame_ready_cb_t cb);

void cam_receiver_add_grid_cb(cam_receiver_t *self,
                               cam_grid_update_cb_t cb, void *user_data);
void cam_receiver_remove_grid_cb(cam_receiver_t *self,
                                  cam_grid_update_cb_t cb);

void cam_receiver_add_status_cb(cam_receiver_t *self,
                                 cam_status_cb_t cb, void *user_data);
void cam_receiver_remove_status_cb(cam_receiver_t *self,
                                    cam_status_cb_t cb);

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/// Get a pointer to the latest complete frame info.
/// The returned pointer is valid until the next frame completes.
/// To safely read |frame->data|, bracket the access with lock/unlock.
const cam_frame_info_t *cam_receiver_get_last_frame(cam_receiver_t *self);

/// Get a pointer to the latest grid info (NULL if none received yet).
const cam_grid_info_t *cam_receiver_get_grid(cam_receiver_t *self);

/// Latest device status (NULL if no heartbeat received yet).
const cam_device_status_msg_t *cam_receiver_get_status(cam_receiver_t *self);

/// Returns true if at least one complete frame has been received.
bool cam_receiver_has_frame(cam_receiver_t *self);

// ---------------------------------------------------------------------------
// Display buffer locking
// ---------------------------------------------------------------------------

/// Acquire a read-lock on the display frame buffer.
/// The lock is lightweight (mutex); do not hold it for long.
void cam_receiver_lock_display(cam_receiver_t *self);

/// Release the display frame buffer lock.
void cam_receiver_unlock_display(cam_receiver_t *self);

// ---------------------------------------------------------------------------
// Calibration grid locking
// ---------------------------------------------------------------------------

/// Acquire the grid data mutex before iterating px_points or points arrays.
/// Must be paired with cam_receiver_unlock_grid().  Do not call LVGL
/// functions that may allocate memory while holding this lock.
void cam_receiver_lock_grid(cam_receiver_t *self);

/// Release the grid data mutex.
void cam_receiver_unlock_grid(cam_receiver_t *self);

// ---------------------------------------------------------------------------
// Commands to the camera
// ---------------------------------------------------------------------------

/// Request the camera to send the next frame.
int cam_receiver_request_frame(cam_receiver_t *self);

/// Request a full keyframe (all tiles).
int cam_receiver_force_keyframe(cam_receiver_t *self);

/// Push a new configuration to the camera.
int cam_receiver_send_config(cam_receiver_t *self,
                              const cam_config_cmd_t *cfg);

// ---------------------------------------------------------------------------
// Streaming session management
// ---------------------------------------------------------------------------

/// Begin a streaming session: sends CAM_CMD_STREAM_START to the camera
/// (implies force-keyframe) and arms the reconnect beacon.
/// Call when the camera view becomes visible.
void cam_receiver_start_stream(cam_receiver_t *self);

/// End the streaming session: sends CAM_CMD_STREAM_STOP so the camera goes
/// idle.  Call when the camera view is no longer visible.
void cam_receiver_stop_stream(cam_receiver_t *self);

/// Returns true if a streaming session is currently active
/// (start_stream was called and stop_stream has not been called since).
bool cam_receiver_is_streaming(cam_receiver_t *self);

/// Periodic maintenance tick — call from your UI refresh timer (e.g. every
/// 33 ms).  Handles reconnect beacons when the camera goes silent so the
/// stream re-establishes automatically after a camera reboot.
/// Returns true if a reconnect beacon was transmitted this tick.
bool cam_receiver_tick(cam_receiver_t *self);

// ---------------------------------------------------------------------------
// Zoom / crop control
// ---------------------------------------------------------------------------

/// Ask the camera to crop its output to the window centred at (center_x,
/// center_y) with the given pixel dimensions (in the CURRENT output frame).
/// The camera will compose a crop+scale homography with the stored calibration
/// homography, rebuild its transform LUT, force a keyframe, and re-send the
/// calibration grid adjusted for the zoom view.
///
/// This function also sends a force-keyframe request so the caller does not
/// need to do so separately.
///
/// Returns 0 on success, negative on send failure.
int cam_receiver_set_zoom(cam_receiver_t *self,
                           uint16_t center_x, uint16_t center_y,
                           uint16_t zoom_w,   uint16_t zoom_h);

/// Restore the normal (un-zoomed) camera output.
/// The camera will revert to the original calibration homography, rebuild
/// the LUT, force a keyframe, and re-send the original calibration grid.
///
/// Returns 0 on success, negative on send failure.
int cam_receiver_clear_zoom(cam_receiver_t *self);

#ifdef __cplusplus
}
#endif
#endif // CAM_RECEIVER_H
