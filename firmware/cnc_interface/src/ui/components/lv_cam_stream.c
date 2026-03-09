// lv_cam_stream.c — LVGL camera stream view implementation

#define UI_DEBUG_LOCAL_LEVEL D_WARN
#include "debug.h"

#include "lv_cam_stream.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cam_stream";

// ---------------------------------------------------------------------------
// Module-level default receiver
// ---------------------------------------------------------------------------
static cam_receiver_t *s_default_receiver = NULL;

void lv_cam_stream_set_default_receiver(cam_receiver_t *receiver)
{
    s_default_receiver = receiver;
}

cam_receiver_t *lv_cam_stream_get_default_receiver(void)
{
    return s_default_receiver;
}

// ---------------------------------------------------------------------------
// Private widget data
// ---------------------------------------------------------------------------
typedef struct {
    lv_obj_t            *root;
    lv_obj_t            *img_obj;
    lv_obj_t            *info_label;

    cam_receiver_t      *receiver;

    // Image descriptor — data pointer is borrowed from receiver's frame_buf
    // (no copy; no separate allocation needed)
    lv_image_dsc_t       img_dsc;

    lv_cam_stream_fit_t  fit;
    bool                 show_info;
    bool                 frame_pending;

    // Streaming session state
    bool                 stream_active;   // cam_receiver_start_stream() sent
    bool                 was_visible;     // Previous tick visibility
    bool                 frame_in_flight; // request_frame sent, waiting for reply

    // Track last rendered dimensions to avoid redundant lv_image_set_src calls
    uint16_t             last_img_w;
    uint16_t             last_img_h;

    lv_timer_t          *refresh_timer;
} lv_cam_stream_priv_t;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lv_cam_stream_priv_t *get_priv(lv_obj_t *obj)
{
    return (lv_cam_stream_priv_t *)lv_obj_get_user_data(obj);
}

static void apply_fit(lv_cam_stream_priv_t *priv)
{
    if (!priv->img_obj) return;
    switch (priv->fit) {
        case LV_CAM_STREAM_FIT_FILL:
            lv_image_set_inner_align(priv->img_obj, LV_IMAGE_ALIGN_STRETCH);
            lv_obj_set_size(priv->img_obj, lv_pct(100), lv_pct(100));
            break;
        case LV_CAM_STREAM_FIT_COVER:
            lv_image_set_inner_align(priv->img_obj, LV_IMAGE_ALIGN_CENTER);
            lv_obj_set_size(priv->img_obj, lv_pct(100), lv_pct(100));
            break;
        case LV_CAM_STREAM_FIT_CONTAIN:
        case LV_CAM_STREAM_FIT_NONE:
        default:
            lv_image_set_inner_align(priv->img_obj, LV_IMAGE_ALIGN_CENTER);
            lv_obj_set_size(priv->img_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            break;
    }
}

// ---------------------------------------------------------------------------
// Receiver callback → deferred LVGL update
// ---------------------------------------------------------------------------

static void on_frame_ready(const cam_frame_info_t *frame, void *user_data)
{
    lv_cam_stream_priv_t *priv = (lv_cam_stream_priv_t *)user_data;
    priv->frame_pending   = true;
    priv->frame_in_flight = false;  // Unblock the next request_frame call
    (void)frame;
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    lv_cam_stream_priv_t *priv = (lv_cam_stream_priv_t *)lv_timer_get_user_data(timer);
    if (!priv || !priv->receiver) return;

    // -----------------------------------------------------------------------
    // 1. Reconnect beacon (no frame requests when invisible, but the tick
    //    keeps re-trying STREAM_START if the camera is silent).
    // -----------------------------------------------------------------------
    cam_receiver_tick(priv->receiver);

    // -----------------------------------------------------------------------
    // 2. Visibility state machine
    //    lv_obj_is_visible() returns false when the widget (or any ancestor)
    //    is hidden, which covers tileview inactive tiles.
    // -----------------------------------------------------------------------
    bool visible = lv_obj_is_visible(priv->root);

    if (visible && !priv->was_visible) {
        // Became visible — start (or restart) the streaming session.
        if (priv->stream_active) {
            // Was already started; just force a fresh keyframe to rebuild the
            // baseline after the view was hidden.
            cam_receiver_force_keyframe(priv->receiver);
        } else {
            cam_receiver_start_stream(priv->receiver);
            priv->stream_active = true;
        }
        priv->frame_in_flight = false;

    } else if (!visible && priv->was_visible) {
        // Became invisible — stop the streaming session to let the camera idle.
        if (priv->stream_active) {
            cam_receiver_stop_stream(priv->receiver);
            priv->stream_active = false;
        }
    }
    priv->was_visible = visible;

    // -----------------------------------------------------------------------
    // 3. Frame request — only when visible and the previous frame has landed.
    //    Backpressure: a new request is deferred until on_frame_ready() fires.
    // -----------------------------------------------------------------------
    if (visible && !priv->frame_in_flight) {
        cam_receiver_request_frame(priv->receiver);
        priv->frame_in_flight = true;
    }

    // -----------------------------------------------------------------------
    // 4. Render pending frame
    // -----------------------------------------------------------------------
    if (!priv->frame_pending) return;
    priv->frame_pending = false;

    const cam_frame_info_t *f = cam_receiver_get_last_frame(priv->receiver);
    if (!f || !f->data || !f->width || !f->height) return;

    cam_receiver_lock_display(priv->receiver);

    bool dims_changed = (f->width  != priv->last_img_w ||
                         f->height != priv->last_img_h);

    // Always update desc fields (data pointer is stable, but be explicit).
    priv->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    priv->img_dsc.data      = f->data;
    priv->img_dsc.data_size = f->data_size;

    if (priv->img_obj) {
        if (dims_changed || priv->img_dsc.header.w == 0) {
            // First frame or resolution change — re-attach the descriptor so
            // LVGL picks up the new dimensions and layout.
            priv->img_dsc.header.w = f->width;
            priv->img_dsc.header.h = f->height;
            priv->last_img_w = f->width;
            priv->last_img_h = f->height;
            lv_image_set_src(priv->img_obj, &priv->img_dsc);
            apply_fit(priv);
        } else {
            // Same resolution: frame_buf contents have been updated in-place;
            // just invalidate so LVGL redraws from the existing descriptor.
            lv_obj_invalidate(priv->img_obj);
        }
    }

    cam_receiver_unlock_display(priv->receiver);

    if (priv->show_info && priv->info_label) {
        const cam_device_status_msg_t *st = cam_receiver_get_status(priv->receiver);
        if (st) {
            lv_label_set_text_fmt(priv->info_label, "%u.%u fps  F#%u",
                                   st->fps_x10 / 10, st->fps_x10 % 10,
                                   f->frame_id);
        } else {
            lv_label_set_text_fmt(priv->info_label, "F#%u", f->frame_id);
        }
    }
}

// ---------------------------------------------------------------------------
// Cleanup on delete
// ---------------------------------------------------------------------------

static void on_delete(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_cam_stream_priv_t *priv = get_priv(obj);
    if (!priv) return;

    // Stop the streaming session so the camera goes idle.
    if (priv->receiver && priv->stream_active) {
        cam_receiver_stop_stream(priv->receiver);
    }

    // Unsubscribe from receiver
    if (priv->receiver) {
        cam_receiver_remove_frame_cb(priv->receiver, on_frame_ready);
    }

    if (priv->refresh_timer) {
        lv_timer_delete(priv->refresh_timer);
    }

    free(priv);
    lv_obj_set_user_data(obj, NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *lv_cam_stream_create(lv_obj_t *parent)
{
    // Allocate private state
    lv_cam_stream_priv_t *priv = calloc(1, sizeof(*priv));
    if (!priv) return NULL;

    // Root container
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(root, priv);
    lv_obj_add_event_cb(root, on_delete, LV_EVENT_DELETE, NULL);

    priv->root = root;
    priv->fit  = LV_CAM_STREAM_FIT_CONTAIN;

    // Image child
    lv_obj_t *img = lv_image_create(root);
    lv_obj_set_align(img, LV_ALIGN_CENTER);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    priv->img_obj = img;

    // Info label (hidden by default)
    lv_obj_t *lbl = lv_label_create(root);
    lv_obj_set_align(lbl, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(lbl, 3, 0);
    lv_label_set_text(lbl, "");
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    priv->info_label = lbl;

    // Periodic timer — 33 ms ≈ 30 Hz visibility / frame check.
    // Frame requests are gated on visibility and backpressure so the actual
    // camera frame rate is limited to what the network + decoder can sustain.
    priv->refresh_timer = lv_timer_create(refresh_timer_cb, 33, priv);

    LOGI(TAG, "cam_stream widget created");

    // Auto-attach to the module-level default receiver if one is registered.
    if (s_default_receiver) {
        lv_cam_stream_set_receiver(root, s_default_receiver);
        LOGD(TAG, "cam_stream auto-attached to default receiver %p", (void *)s_default_receiver);
    }

    return root;
}

void lv_cam_stream_set_receiver(lv_obj_t *obj, cam_receiver_t *receiver)
{
    lv_cam_stream_priv_t *priv = get_priv(obj);
    if (!priv) return;

    // Detach from old receiver — stop any active stream cleanly.
    if (priv->receiver) {
        if (priv->stream_active) {
            cam_receiver_stop_stream(priv->receiver);
            priv->stream_active = false;
        }
        cam_receiver_remove_frame_cb(priv->receiver, on_frame_ready);
    }

    priv->receiver        = receiver;
    priv->frame_pending   = false;
    priv->frame_in_flight = false;
    priv->was_visible     = false;   // Force re-evaluation on next tick

    // Attach to new receiver.
    if (receiver) {
        cam_receiver_add_frame_cb(receiver, on_frame_ready, priv);

        // Request one initial keyframe so the widget shows a static thumbnail
        // even before the view becomes fully visible for the first time.
        cam_receiver_force_keyframe(receiver);
        cam_receiver_request_frame(receiver);
        priv->frame_in_flight = true;

        // If the receiver already has a frame, display it immediately.
        const cam_frame_info_t *f = cam_receiver_get_last_frame(receiver);
        if (f && f->data) {
            priv->frame_pending = true;
        }
    }
}

cam_receiver_t *lv_cam_stream_get_receiver(lv_obj_t *obj)
{
    lv_cam_stream_priv_t *priv = get_priv(obj);
    return priv ? priv->receiver : NULL;
}

void lv_cam_stream_set_fit(lv_obj_t *obj, lv_cam_stream_fit_t fit)
{
    lv_cam_stream_priv_t *priv = get_priv(obj);
    if (!priv) return;
    priv->fit = fit;
    apply_fit(priv);
}

lv_cam_stream_fit_t lv_cam_stream_get_fit(lv_obj_t *obj)
{
    lv_cam_stream_priv_t *priv = get_priv(obj);
    return priv ? priv->fit : LV_CAM_STREAM_FIT_CONTAIN;
}

void lv_cam_stream_set_show_info(lv_obj_t *obj, bool show)
{
    lv_cam_stream_priv_t *priv = get_priv(obj);
    if (!priv) return;
    priv->show_info = show;
    if (priv->info_label) {
        if (show) lv_obj_clear_flag(priv->info_label, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(priv->info_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void lv_cam_stream_get_image_size(lv_obj_t *obj,
                                   uint16_t *out_width, uint16_t *out_height)
{
    lv_cam_stream_priv_t *priv = get_priv(obj);
    if (priv) {
        if (out_width)  *out_width  = (uint16_t)priv->img_dsc.header.w;
        if (out_height) *out_height = (uint16_t)priv->img_dsc.header.h;
    } else {
        if (out_width)  *out_width  = 0;
        if (out_height) *out_height = 0;
    }
}

void lv_cam_stream_refresh(lv_obj_t *obj)
{
    lv_cam_stream_priv_t *priv = get_priv(obj);
    if (priv) priv->frame_pending = true;
}

lv_obj_t *lv_cam_stream_get_image_obj(lv_obj_t *obj)
{
    lv_cam_stream_priv_t *priv = get_priv(obj);
    return priv ? priv->img_obj : NULL;
}
