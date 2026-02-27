// lv_cam_positioning.c — Camera-based positioning view implementation

#define UI_DEBUG_LOCAL_LEVEL D_DEBUG
#include "debug.h"

#include "lv_cam_positioning.h"
#include "lv_cam_stream.h"
#include "probe/probe_api.h"
#include "config/probe_settings.h"
#include "ui/assets.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
static const char *TAG = "cam_pos";

// ---------------------------------------------------------------------------
// Drawing constants
// ---------------------------------------------------------------------------
#define POINT_RADIUS        5           // 10 px diameter
#define POINT_BORDER_W      2
#define RECT_BORDER_W       2
#define CIRCLE_STROKE_W     2

// Colours (LVGL hex)
#define COL_POINT_FILL      0xFF4444    // Red-ish
#define COL_POINT_BORDER    0xCC0000    // Darker red outline
#define OPA_POINT_FILL      LV_OPA_50  // Transparent red centre
#define OPA_POINT_BORDER    LV_OPA_80

#define COL_RECT_BG         0x4488FF    // Blue
#define OPA_RECT_BG         LV_OPA_30  // Transparent blue background
#define COL_RECT_EDGE       0x2266CC    // Slightly darker blue
#define OPA_RECT_EDGE       LV_OPA_60

#define COL_CIRCLE_STROKE   0x2266CC
#define OPA_CIRCLE_STROKE   LV_OPA_60

// Camera image border
#define CAM_INSET_MARGIN_PX     5   // Gap between root edges and stream widget
#define CAM_BORDER_WIDTH_PX     5   // Border around stream widget
#define CAM_BORDER_RADIUS_PX    8   // Rounded corner radius
#define CAM_BORDER_COLOR        0x707070  // Medium gray
#define CAM_INNER_PAD_PX        5   // Padding inside border around image

// ---------------------------------------------------------------------------
// Probe wizard styling constants
// ---------------------------------------------------------------------------
#define SIDEBAR_W           150    // Right-panel width reserved for wizard UI (px)
#define WIZ_OP_BTN_H         28    // Height of each op-selector button (px)
#define WIZ_STEP_BTN_H       26    // Height of each step button (px)
#define WIZ_PANEL_PAD         4    // Inner padding for sidebar panels (px)
#define WIZ_STEP_RADIUS       5    // Corner radius on step/op buttons (px)

// Colours — all as 24-bit RGB hex
#define WIZ_BG_COLOR         0x1E293B  // Slate-900 (dark panel background)
#define WIZ_TXT_COLOR        0xF1F5F9  // Slate-100 (near-white)
#define WIZ_XY_COLOR         0x38BDF8  // Sky-400   (coordinate accent)
#define WIZ_DIV_COLOR        0x334155  // Slate-700 (divider line)
#define WIZ_BTN_MOVE_COLOR   0x2563EB  // Blue-600  (Move to)
#define WIZ_BTN_RECT_COLOR   0x059669  // Emerald-600 (Block / Pocket)
#define WIZ_BTN_CIRC_COLOR   0xD97706  // Amber-600 (Bore / Boss)
#define WIZ_STEP_DONE_COLOR  0xB45309  // Amber-700 (completed step)
#define WIZ_STEP_CURR_COLOR  0x16A34A  // Green-600 (active step)
#define WIZ_STEP_NEXT_COLOR  0x374151  // Slate-700 (future/disabled step)
#define WIZ_RUN_COLOR        0xDC2626  // Red-600   (Run button)
#define WIZ_CANCEL_COLOR     0xDC2626  // Red-600   reset button
#define WIZ_ZREF_DOT_COLOR   0x00FF88  // Vivid teal for Z-ref dot
#define WIZ_ZREF_BDR_COLOR   0x00CC66  // Teal border for Z-ref dot

// ---------------------------------------------------------------------------
// Zoom badge visual constants
// ---------------------------------------------------------------------------
#define WIZ_ZOOM_BG_COLOR    0x7C3AED  // Violet-700
#define WIZ_ZOOM_TEXT_COLOR  0xEDE9FE  // Violet-100 (near-white)
#define WIZ_ZOOM_BTN_H       22        // Badge height in px
#define WIZ_ZOOM_PAD          6        // Margin from stream corner (px)

// ---------------------------------------------------------------------------
// Zoom trigger timing compile-time flag.
//
// CAM_POS_ZOOM_AFTER_PT2 = 1 (default):
//   Zoom activates as soon as pt2 has been collected, before the user taps
//   Z-surf.  The user can set Z-surf and the Run step in the zoomed view.
//
// CAM_POS_ZOOM_AFTER_PT2 = 0:
//   Zoom activates after Z-surf has been collected, just before Run.
//   Avoids any keyframe-round-trip latency during pt2/Z-surf collection.
// ---------------------------------------------------------------------------
#ifndef CAM_POS_ZOOM_AFTER_PT2
#define CAM_POS_ZOOM_AFTER_PT2  1
#endif

// Grid overlay
#define GRID_LINE_COLOR     0x808080   // Medium gray
#define GRID_LINE_OPA       LV_OPA_50

// Calibration grid overlay (actual calibration points from the camera)
#define CALIB_GRID_LINE_COLOR  0x44FF44   // Vivid green
#define CALIB_GRID_LINE_OPA    LV_OPA_60
#define CALIB_GRID_DOT_COLOR   0xFFFF44   // Yellow
#define CALIB_GRID_DOT_OPA     LV_OPA_80
#define CALIB_GRID_DOT_R       2          // dot half-size in screen pixels

// Machine origin axes overlay (yellow short axis lines + dot at physical 0,0)
#define COL_MACH_ORIGIN_AXES   0xFFFF00   // Yellow
#define OPA_MACH_ORIGIN_AXES   LV_OPA_90
#define COL_MACH_ORIGIN_DOT    0xFFFF00   // Yellow
#define OPA_MACH_ORIGIN_DOT    LV_OPA_COVER
#define MACH_ORIGIN_AXIS_LEN   50         // Length of axis arms in screen px
#define MACH_ORIGIN_DOT_R      3          // Dot radius in screen px

// WCS origin overlay (green axis lines + green dot with white outline)
#define COL_WCS_ORIGIN_AXES    0x00EE00   // Green
#define OPA_WCS_ORIGIN_AXES    LV_OPA_80
#define COL_WCS_ORIGIN_DOT     0x00CC00   // Slightly darker green
#define WCS_ORIGIN_DOT_R       5          // Dot radius in screen px

// Machine absolute-position crosshair (red)
#define COL_MACH_POS_CROSS     0xFF2222   // Bright red
#define OPA_MACH_POS_CROSS     LV_OPA_COVER
#define MACH_CROSS_ARM         16         // Half-length of each crosshair arm (px)
#define MACH_CROSS_GAP          5         // Half-gap around the centre (px)

// ---------------------------------------------------------------------------
// Wizard (probe flow) states
// ---------------------------------------------------------------------------
typedef enum {
    WIZ_IDLE       = 0,  ///< No tap yet; sidebar shows op selector
    WIZ_OP_SELECT  = 1,  ///< Pt1 placed; waiting for op choice in sidebar
    WIZ_COLLECTING = 2,  ///< Op chosen; collecting remaining points on camera
    WIZ_READY      = 3,  ///< All points set; Run active; tap/drag to adjust pts
    WIZ_CONFIRM    = 6,  ///< Confirmation modal open (keep value=6 for compat)
    WIZ_RUNNING    = 7,  ///< Operation dispatched, waiting for probe_api
} wiz_state_t;

/// Sidebar button context – used for both op-selector and step buttons.
typedef struct {
    void      *priv;         ///< cast to lv_cam_pos_priv_t *
    probe_op_t op;           ///< used by op-selector buttons
    int8_t     step_idx;     ///< used by step buttons (-1 = not a step btn)
} sidebar_btn_ctx_t;

// ---------------------------------------------------------------------------
// Selection state
// ---------------------------------------------------------------------------
typedef enum {
    SEL_NONE = 0,
    SEL_FIRST_POINT,   // One point placed (rect corner 1 / circle centre)
    SEL_COMPLETE,       // Shape fully defined
} sel_state_t;

// ---------------------------------------------------------------------------
// Private widget data
// ---------------------------------------------------------------------------
typedef struct {
    lv_obj_t               *root;
    lv_obj_t               *stream;       // lv_cam_stream child
    lv_obj_t               *overlay;      // Transparent touch-capture / draw obj

    cam_receiver_t         *receiver;
    machine_interface_t    *machine;       // For axis_min / axis_max → grid step

    lv_cam_pos_mode_t       mode;
    sel_state_t             sel_state;

    // Current shape data
    lv_cam_pos_point_t      point;
    lv_cam_pos_rect_t       rect;
    lv_cam_pos_circle_t     circle;

    bool                    point_valid;
    bool                    rect_valid;
    bool                    circle_valid;

    // Temporary drag point for live preview while placing second point
    bool                    preview_active;
    int16_t                 preview_px_x;
    int16_t                 preview_px_y;

    // Callbacks
    lv_cam_pos_point_cb_t   point_cb;
    void                   *point_cb_user;
    lv_cam_pos_rect_cb_t    rect_cb;
    void                   *rect_cb_user;
    lv_cam_pos_circle_cb_t  circle_cb;
    void                   *circle_cb_user;

    // --- Probe wizard ---
    wiz_state_t             wiz_state;         ///< Current wizard FSM state
    probe_op_t              wiz_op;            ///< Selected operation
    lv_cam_pos_point_t      wiz_pt1;           ///< First tap (target/corner/center)
    lv_cam_pos_point_t      wiz_pt2;           ///< Second point (corner2/edge)
    lv_cam_pos_point_t      wiz_zref;          ///< Z-reference surface point
    lv_point_t              wiz_scr_tap;       ///< Raw screen coords of first tap (offline fallback)
    bool                    wiz_pt1_set;       ///< pt1 collected
    bool                    wiz_pt2_set;       ///< pt2 collected
    bool                    wiz_zref_set;      ///< zref collected
    int                     wiz_step_idx;      ///< Currently active step index (0-based)
    int                     wiz_drag_pt;       ///< Point being dragged: -1=none 0=pt1 1=pt2 2=zref

    // Move-point mode: overrides the normal wizard step placement flow.
    // When wiz_move_pt_active, the next overlay tap repositions the point
    // identified by wiz_move_pt_which and then resumes the normal wizard flow.
    bool                    wiz_move_pt_active;       ///< Move-point mode active
    int8_t                  wiz_move_pt_which;        ///< -1=none 0=pt1 1=pt2 2=zref
    bool                    wiz_move_pt_blink_on;     ///< Blink phase (toggled by timer)
    lv_timer_t             *wiz_move_pt_blink_timer;  ///< Drives blink; NULL when mode inactive

    // Sidebar LVGL objects (permanent children of right_panel; contents rebuilt on state change)
    lv_obj_t               *right_panel;       ///< 80 px right panel container
    lv_obj_t               *coord_panel;       ///< Coordinate display (top of sidebar)
    lv_obj_t               *step_panel;        ///< Op selector or step list (below coords)
    sidebar_btn_ctx_t       sidebar_btn_ctxs[8];  ///< Contexts for sidebar buttons (no heap)
    lv_obj_t               *wiz_confirm_modal; ///< Confirmation msgbox (parented to screen)
    // Probe backend
    probe_api_ctx_t         probe_ctx;
    bool                    probe_cbs_set;
    // Command callbacks from external caller (forwarded into probe_ctx)
    probe_cmd_move_to_cb_t      probe_cmd_move_to;
    probe_cmd_probe_z_cb_t      probe_cmd_probe_z;
    probe_cmd_probe_rect_cb_t   probe_cmd_probe_rect;
    probe_cmd_probe_circle_cb_t probe_cmd_probe_circle;
    void                       *probe_user_data;

    // Grid-dirty flag + timer -- thread-safe overlay repaint from ESP-NOW task.
    // on_grid_update() (ESP-NOW FreeRTOS RX task) must NOT call lv_async_call()
    // or any other LVGL API directly: LVGL has no OS mutex in this build, so
    // concurrent modification of the timer linked list from a non-LVGL task
    // races with lv_task_handler() and corrupts the list (NULL-deref crash).
    // Instead it sets this volatile flag (a plain bool write is atomic on
    // Xtensa); the lv_timer below polls it every 50 ms from the LVGL task.
    volatile bool           grid_dirty;
    lv_timer_t             *grid_timer;   ///< owned by this widget, deleted in on_delete

    // Zoom state
    bool                    wiz_zoom_active; ///< Camera is currently showing a zoomed view
    lv_obj_t               *zoom_badge;     ///< Overlay badge while zoom is active (NULL otherwise)
    // Crop window sent with the last SET_ZOOM (in original image pixel coords).
    // Stored so that collected point px coords can be inverse-remapped on
    // CLEAR_ZOOM — restoring them to the unzoomed image coordinate space.
    uint16_t                wiz_zoom_cx, wiz_zoom_cy; ///< Crop centre (original px)
    uint16_t                wiz_zoom_w,  wiz_zoom_h;  ///< Crop size   (original px)
    // Set by wiz_activate_zoom, cleared by grid_dirty_timer_cb once the
    // first post-zoom grid update arrives (confirming the zoomed keyframe
    // is on its way).  Until cleared, points are still in pre-zoom coords.
    volatile bool           wiz_zoom_remap_pending;

    // ---- Local camera-connection status label ----
    // A small semi-transparent label overlaid on the cam area.  The widget
    // automatically shows "Connecting to camera..." when no frame has arrived
    // yet and hides once the camera starts streaming.  Callers can also show
    // an explicit message via lv_cam_positioning_show_status_text() — in that
    // case status_locked=true prevents the auto-clear until the caller calls
    // lv_cam_positioning_clear_status_text().
    lv_obj_t               *status_label;   ///< Semi-transparent overlay label
    bool                    status_locked;  ///< true: caller controls the text
} lv_cam_pos_priv_t;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static lv_cam_pos_priv_t *get_priv(lv_obj_t *obj);
static void overlay_draw_cb(lv_event_t *e);
static void overlay_click_cb(lv_event_t *e);
static void overlay_press_cb(lv_event_t *e);
static void on_delete(lv_event_t *e);

static bool pixel_to_image_coords(lv_cam_pos_priv_t *priv,
                                   lv_point_t screen_pt,
                                   int16_t *out_img_x, int16_t *out_img_y);
static bool grid_interpolate(const cam_grid_info_t *g,
                              uint16_t img_w, uint16_t img_h,
                              int16_t px_x, int16_t px_y,
                              float *out_x, float *out_y);
static lv_cam_pos_point_t make_point(lv_cam_pos_priv_t *priv,
                                      int16_t px_x, int16_t px_y);
static float cam_pos_choose_grid_step(float range_x, float range_y);
static void  draw_axis_grid(lv_layer_t *layer, lv_cam_pos_priv_t *priv);

// Wizard helpers
static void wiz_reset(lv_cam_pos_priv_t *priv);
static void wiz_show_confirm(lv_cam_pos_priv_t *priv);
static void wiz_execute(lv_cam_pos_priv_t *priv);
static void wiz_handle_click(lv_cam_pos_priv_t *priv, lv_point_t scr_pt);
static void sidebar_rebuild(lv_cam_pos_priv_t *priv);
static void sidebar_update_coords(lv_cam_pos_priv_t *priv);

// Zoom helpers
static void wiz_activate_zoom(lv_cam_pos_priv_t *priv);
static void wiz_deactivate_zoom(lv_cam_pos_priv_t *priv);

// Move-point mode helpers
static void wiz_start_move_pt(lv_cam_pos_priv_t *priv, int8_t which);
static void wiz_cancel_move_pt(lv_cam_pos_priv_t *priv);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lv_cam_pos_priv_t *get_priv(lv_obj_t *obj)
{
    return (lv_cam_pos_priv_t *)lv_obj_get_user_data(obj);
}

/// Map screen-relative touch point to image pixel coordinates.
static bool pixel_to_image_coords(lv_cam_pos_priv_t *priv,
                                   lv_point_t screen_pt,
                                   int16_t *out_img_x, int16_t *out_img_y)
{
    if (!priv->stream) return false;

    lv_obj_t *img_obj = lv_cam_stream_get_image_obj(priv->stream);
    if (!img_obj) return false;

    // Get image widget's screen-relative position
    lv_area_t img_area;
    lv_obj_get_coords(img_obj, &img_area);

    // Pixel position within the image object
    int16_t rel_x = screen_pt.x - img_area.x1;
    int16_t rel_y = screen_pt.y - img_area.y1;

    uint16_t img_w = 0, img_h = 0;
    lv_cam_stream_get_image_size(priv->stream, &img_w, &img_h);
    if (img_w == 0 || img_h == 0) return false;

    // Scale from widget pixel to image pixel (for fit modes with scaling)
    int32_t obj_w = lv_area_get_width(&img_area);
    int32_t obj_h = lv_area_get_height(&img_area);
    if (obj_w <= 0 || obj_h <= 0) return false;

    *out_img_x = (int16_t)((int32_t)rel_x * img_w / obj_w);
    *out_img_y = (int16_t)((int32_t)rel_y * img_h / obj_h);

    // Clamp
    if (*out_img_x < 0) *out_img_x = 0;
    if (*out_img_y < 0) *out_img_y = 0;
    if (*out_img_x >= (int16_t)img_w) *out_img_x = img_w - 1;
    if (*out_img_y >= (int16_t)img_h) *out_img_y = img_h - 1;
    return true;
}

/// Map image pixel coordinates to screen pixel coordinates (inverse of above).
static bool image_to_screen_coords(lv_cam_pos_priv_t *priv,
                                    int16_t img_x, int16_t img_y,
                                    int32_t *out_scr_x, int32_t *out_scr_y)
{
    lv_obj_t *img_obj = lv_cam_stream_get_image_obj(priv->stream);
    if (!img_obj) return false;

    lv_area_t img_area;
    lv_obj_get_coords(img_obj, &img_area);

    uint16_t img_w = 0, img_h = 0;
    lv_cam_stream_get_image_size(priv->stream, &img_w, &img_h);
    if (img_w == 0 || img_h == 0) return false;

    int32_t obj_w = lv_area_get_width(&img_area);
    int32_t obj_h = lv_area_get_height(&img_area);
    // Skip drawing when the image object hasn't been laid out yet (size = 0).
    // Without this guard, all points map to the same pixel which can produce
    // degenerate areas that trigger LVGL assertions inside lv_draw_rect.
    if (obj_w <= 0 || obj_h <= 0) return false;

    *out_scr_x = img_area.x1 + (int32_t)img_x * obj_w / img_w;
    *out_scr_y = img_area.y1 + (int32_t)img_y * obj_h / img_h;
    return true;
}

/// Bilinear interpolation on the grid to get physical coordinates.
/// Accounts for grid insets: the grid spans from
///   (inset_left, inset_top) to (img_w - inset_right - 1, img_h - inset_bottom - 1).
/// Insets are stored in calibration-image pixels; they are scaled to the
/// current display image size when the two differ.
/// Pixels outside the inset area are extrapolated (clamped to grid boundary).
static bool grid_interpolate(const cam_grid_info_t *g,
                              uint16_t img_w, uint16_t img_h,
                              int16_t px_x, int16_t px_y,
                              float *out_x, float *out_y)
{
    if (!g || !g->points || g->nx < 2 || g->ny < 2) return false;

    // Scale insets to current display-image pixels when the calibration was
    // performed at a different output resolution.
    float sx = (g->src_img_w > 0 && g->src_img_w != img_w)
               ? (float)img_w / (float)g->src_img_w : 1.0f;
    float sy = (g->src_img_h > 0 && g->src_img_h != img_h)
               ? (float)img_h / (float)g->src_img_h : 1.0f;
    float il = (float)g->inset_left  * sx;
    float it = (float)g->inset_top   * sy;
    // Active pixel area after insets
    float active_w = (float)img_w - il - (float)g->inset_right  * sx;
    float active_h = (float)img_h - it - (float)g->inset_bottom * sy;
    if (active_w < 1.0f) active_w = 1.0f;
    if (active_h < 1.0f) active_h = 1.0f;

    // Fractional grid cell position (remapped from inset area)
    float gx = ((float)px_x - il) * (float)(g->nx - 1) / active_w;
    float gy = ((float)px_y - it) * (float)(g->ny - 1) / active_h;

    int ix = (int)gx;
    int iy = (int)gy;
    if (ix < 0) ix = 0;
    if (iy < 0) iy = 0;
    if (ix >= g->nx - 1) ix = g->nx - 2;
    if (iy >= g->ny - 1) iy = g->ny - 2;

    float fx = gx - (float)ix;
    float fy = gy - (float)iy;
    if (fx < 0) fx = 0; if (fx > 1) fx = 1;
    if (fy < 0) fy = 0; if (fy > 1) fy = 1;

    // Four corner indices (row-major)
    int i00 = (iy       * g->nx + ix)     * 2;
    int i10 = (iy       * g->nx + ix + 1) * 2;
    int i01 = ((iy + 1) * g->nx + ix)     * 2;
    int i11 = ((iy + 1) * g->nx + ix + 1) * 2;

    // Bilinear interp
    float x00 = g->points[i00], y00 = g->points[i00 + 1];
    float x10 = g->points[i10], y10 = g->points[i10 + 1];
    float x01 = g->points[i01], y01 = g->points[i01 + 1];
    float x11 = g->points[i11], y11 = g->points[i11 + 1];

    *out_x = x00 * (1 - fx) * (1 - fy) + x10 * fx * (1 - fy)
           + x01 * (1 - fx) * fy        + x11 * fx * fy;
    *out_y = y00 * (1 - fx) * (1 - fy) + y10 * fx * (1 - fy)
           + y01 * (1 - fx) * fy        + y11 * fx * fy;
    return true;
}

/// Build a point with optional physical coordinates.
static lv_cam_pos_point_t make_point(lv_cam_pos_priv_t *priv,
                                      int16_t px_x, int16_t px_y)
{
    lv_cam_pos_point_t pt = { .px_x = px_x, .px_y = px_y,
                               .phys_x = 0, .phys_y = 0,
                               .has_physical = false };
    if (priv->receiver) {
        uint16_t iw = 0, ih = 0;
        lv_cam_stream_get_image_size(priv->stream, &iw, &ih);
        // Lock grid so g->points can't be freed while grid_interpolate reads it.
        cam_receiver_lock_grid(priv->receiver);
        const cam_grid_info_t *g = cam_receiver_get_grid(priv->receiver);
        if (g && iw && ih) {
            pt.has_physical = grid_interpolate(g, iw, ih, px_x, px_y,
                                                &pt.phys_x, &pt.phys_y);
        }
        cam_receiver_unlock_grid(priv->receiver);
    }
    return pt;
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------

/// Draw a selection point (filled circle with border).
static void draw_point(lv_layer_t *layer, int32_t scr_x, int32_t scr_y)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color     = lv_color_hex(COL_POINT_FILL);
    dsc.bg_opa       = OPA_POINT_FILL;
    dsc.border_color = lv_color_hex(COL_POINT_BORDER);
    dsc.border_width = POINT_BORDER_W;
    dsc.border_opa   = OPA_POINT_BORDER;
    dsc.radius       = LV_RADIUS_CIRCLE;

    lv_area_t area = {
        .x1 = scr_x - POINT_RADIUS,
        .y1 = scr_y - POINT_RADIUS,
        .x2 = scr_x + POINT_RADIUS,
        .y2 = scr_y + POINT_RADIUS,
    };
    lv_draw_rect(layer, &dsc, &area);
}

/// Draw a rectangle shape (translucent fill + border + corner points).
static void draw_rectangle(lv_layer_t *layer, lv_cam_pos_priv_t *priv,
                            int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    // Normalise so that x1 <= x2, y1 <= y2
    if (x1 > x2) { int32_t t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int32_t t = y1; y1 = y2; y2 = t; }

    // Filled background
    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_rect_dsc_init(&bg_dsc);
    bg_dsc.bg_color     = lv_color_hex(COL_RECT_BG);
    bg_dsc.bg_opa       = OPA_RECT_BG;
    bg_dsc.border_color = lv_color_hex(COL_RECT_EDGE);
    bg_dsc.border_width = RECT_BORDER_W;
    bg_dsc.border_opa   = OPA_RECT_EDGE;
    bg_dsc.radius       = 0;

    lv_area_t r = { .x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2 };
    lv_draw_rect(layer, &bg_dsc, &r);

    // Corner points
    draw_point(layer, x1, y1);
    draw_point(layer, x2, y1);
    draw_point(layer, x1, y2);
    draw_point(layer, x2, y2);

    (void)priv;
}

/// Draw a circle shape (arc outline + centre + edge point).
static void draw_circle(lv_layer_t *layer, lv_cam_pos_priv_t *priv,
                          int32_t cx, int32_t cy, int32_t ex, int32_t ey)
{
    int32_t dx = ex - cx;
    int32_t dy = ey - cy;
    int32_t r_px = (int32_t)sqrtf((float)(dx * dx + dy * dy));
    if (r_px < 1) r_px = 1;

    // Translucent filled circle (using a rect with full radius)
    lv_draw_rect_dsc_t fill_dsc;
    lv_draw_rect_dsc_init(&fill_dsc);
    fill_dsc.bg_color     = lv_color_hex(COL_RECT_BG);
    fill_dsc.bg_opa       = LV_OPA_20;
    fill_dsc.border_color = lv_color_hex(COL_CIRCLE_STROKE);
    fill_dsc.border_width = CIRCLE_STROKE_W;
    fill_dsc.border_opa   = OPA_CIRCLE_STROKE;
    fill_dsc.radius       = LV_RADIUS_CIRCLE;

    lv_area_t area = {
        .x1 = cx - r_px, .y1 = cy - r_px,
        .x2 = cx + r_px, .y2 = cy + r_px,
    };
    lv_draw_rect(layer, &fill_dsc, &area);

    // Centre point (red)
    draw_point(layer, cx, cy);
    // Edge point (red)
    draw_point(layer, ex, ey);

    (void)priv;
}

/// Draw a CNC probe point marker: white crosshair + optional symbol badge.
/// @param symbol  LVGL symbol string (e.g. LV_SYMBOL_GPS) or NULL for badge-less.
static void draw_wiz_marker(lv_layer_t *layer, int32_t cx, int32_t cy,
                             const char *symbol)
{
    // --- Crosshair ---
    const int32_t ARM = 10;   // arm length from center (px)
    const int32_t GAP =  4;   // half-gap around the center point
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_white();
    ld.width = 2;
    ld.opa   = LV_OPA_80;

    ld.p1.x = cx - ARM; ld.p1.y = cy;       ld.p2.x = cx - GAP; ld.p2.y = cy;       lv_draw_line(layer, &ld); // left arm
    ld.p1.x = cx + GAP; ld.p1.y = cy;       ld.p2.x = cx + ARM; ld.p2.y = cy;       lv_draw_line(layer, &ld); // right arm
    ld.p1.x = cx;       ld.p1.y = cy - ARM; ld.p2.x = cx;       ld.p2.y = cy - GAP; lv_draw_line(layer, &ld); // up arm
    ld.p1.x = cx;       ld.p1.y = cy + GAP; ld.p2.x = cx;       ld.p2.y = cy + ARM; lv_draw_line(layer, &ld); // down arm

    if (!symbol || symbol[0] == '\0') return;

    // --- Symbol badge (upper-right, just above the top arm end) ---
    const int32_t BW = 22;                      // badge width
    const int32_t BH = 20;                      // badge height
    const int32_t BX = cx + GAP + 2;            // right of center gap
    const int32_t BY = cy - ARM - BH - 2;       // above top arm

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color     = lv_color_hex(WIZ_BG_COLOR);
    rd.bg_opa       = LV_OPA_90;
    rd.border_color = lv_color_white();
    rd.border_width = 1;
    rd.border_opa   = LV_OPA_60;
    rd.radius       = 5;
    lv_area_t badge = { BX, BY, BX + BW - 1, BY + BH - 1 };
    lv_draw_rect(layer, &rd, &badge);

    lv_draw_label_dsc_t txt;
    lv_draw_label_dsc_init(&txt);
    txt.color      = lv_color_hex(WIZ_XY_COLOR);
    txt.opa        = LV_OPA_COVER;
    txt.align      = LV_TEXT_ALIGN_CENTER;
    txt.text       = symbol;
    txt.text_local = 0;  // ROM string literal — no copy needed
    lv_area_t tarea = { BX + 1, BY + 2, BX + BW - 2, BY + BH - 2 };
    lv_draw_label(layer, &txt, &tarea);
}

/// Draw an amber ring around a wizard marker to indicate it is selected for
/// repositioning.  Called for the blink-on phase while move-point mode is active.
static void draw_move_pt_halo(lv_layer_t *layer, int32_t cx, int32_t cy)
{
    const int32_t R = POINT_RADIUS + 5;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa       = LV_OPA_TRANSP;
    dsc.border_color = lv_color_hex(0xFFAA00);  // amber
    dsc.border_width = 2;
    dsc.border_opa   = LV_OPA_COVER;
    dsc.radius       = LV_RADIUS_CIRCLE;
    lv_area_t a = { cx - R, cy - R, cx + R, cy + R };
    lv_draw_rect(layer, &dsc, &a);
}

/// draw_calib_grid: not called from overlay_draw_cb.
///
/// This function draws per-point calibration offsets as distorted grid lines.
/// It is more expensive than draw_axis_grid and currently unused for rendering.
/// The px_points array is used exclusively for pixel↔physical coordinate
/// translation in image_to_screen_coords — never for drawing.
///
/// If a visual calib-grid indicator is needed in future, this can be called
/// directly from overlay_draw_cb now that LV_MEM_SIZE has been raised.
__attribute__((unused))
static void draw_calib_grid(lv_layer_t *layer, lv_cam_pos_priv_t *priv)
{
    if (!priv->receiver) return;

    // Snapshot only the scalar parameters needed for straight-line drawing.
    // No px_points copy required — idealized lines need only bounds + dimensions.
    cam_receiver_lock_grid(priv->receiver);
    const cam_grid_info_t *g = cam_receiver_get_grid(priv->receiver);
    if (!g || g->nx < 2 || g->ny < 2) {
        cam_receiver_unlock_grid(priv->receiver);
        return;
    }
    uint16_t nx    = g->nx;
    uint16_t ny    = g->ny;
    float    min_x = g->min_x,  max_x = g->max_x;
    float    min_y = g->min_y,  max_y = g->max_y;
    cam_receiver_unlock_grid(priv->receiver);

    float phys_w = max_x - min_x;
    float phys_h = max_y - min_y;
    if (phys_w <= 0.0f || phys_h <= 0.0f) return;

    lv_obj_t *img_obj = lv_cam_stream_get_image_obj(priv->stream);
    if (!img_obj) return;
    lv_area_t img_area;
    lv_obj_get_coords(img_obj, &img_area);
    int32_t scr_w = (int32_t)lv_area_get_width(&img_area);
    int32_t scr_h = (int32_t)lv_area_get_height(&img_area);
    if (scr_w <= 0 || scr_h <= 0) return;

    // Cap line count per axis so draw-task count is bounded even for dense grids.
    // At stride=1 a 15×15 grid creates 30 tasks — well within the pool budget
    // since each task now carries only p1/p2 (no points-array allocation).
#define CALIB_MAX_DRAW_LINES 16u
    uint16_t stride_x = (nx > CALIB_MAX_DRAW_LINES) ? (uint16_t)(nx / CALIB_MAX_DRAW_LINES) : 1u;
    uint16_t stride_y = (ny > CALIB_MAX_DRAW_LINES) ? (uint16_t)(ny / CALIB_MAX_DRAW_LINES) : 1u;

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color      = lv_color_hex(CALIB_GRID_LINE_COLOR);
    ldsc.width      = 1;
    ldsc.opa        = CALIB_GRID_LINE_OPA;
    ldsc.dash_width = 6;   // dashed to distinguish from the solid axis grid
    ldsc.dash_gap   = 6;
    /* points / point_cnt intentionally left NULL/0: use p1/p2 mode only */

    // Vertical lines — one per calibration column
    uint16_t nx1 = nx - 1u;
    for (uint16_t i = 0; i < nx; i += stride_x) {
        int32_t scr_x = img_area.x1 +
                        (int32_t)((float)i / (float)nx1 * (float)scr_w);
        if (scr_x < img_area.x1 || scr_x > img_area.x2) continue;
        ldsc.p1.x = (lv_value_precise_t)scr_x;
        ldsc.p1.y = (lv_value_precise_t)img_area.y1;
        ldsc.p2.x = (lv_value_precise_t)scr_x;
        ldsc.p2.y = (lv_value_precise_t)img_area.y2;
        lv_draw_line(layer, &ldsc);
    }
    // Always draw the last vertical line (right boundary)
    if ((nx - 1u) % stride_x != 0u) {
        ldsc.p1.x = (lv_value_precise_t)img_area.x2;
        ldsc.p1.y = (lv_value_precise_t)img_area.y1;
        ldsc.p2.x = (lv_value_precise_t)img_area.x2;
        ldsc.p2.y = (lv_value_precise_t)img_area.y2;
        lv_draw_line(layer, &ldsc);
    }

    // Horizontal lines — one per calibration row
    uint16_t ny1 = ny - 1u;
    for (uint16_t j = 0; j < ny; j += stride_y) {
        int32_t scr_y = img_area.y1 +
                        (int32_t)((float)j / (float)ny1 * (float)scr_h);
        if (scr_y < img_area.y1 || scr_y > img_area.y2) continue;
        ldsc.p1.x = (lv_value_precise_t)img_area.x1;
        ldsc.p1.y = (lv_value_precise_t)scr_y;
        ldsc.p2.x = (lv_value_precise_t)img_area.x2;
        ldsc.p2.y = (lv_value_precise_t)scr_y;
        lv_draw_line(layer, &ldsc);
    }
    // Always draw the last horizontal line (bottom boundary)
    if ((ny - 1u) % stride_y != 0u) {
        ldsc.p1.x = (lv_value_precise_t)img_area.x1;
        ldsc.p1.y = (lv_value_precise_t)img_area.y2;
        ldsc.p2.x = (lv_value_precise_t)img_area.x2;
        ldsc.p2.y = (lv_value_precise_t)img_area.y2;
        lv_draw_line(layer, &ldsc);
    }
#undef CALIB_MAX_DRAW_LINES
}

// ---------------------------------------------------------------------------
// Axis grid helpers
// ---------------------------------------------------------------------------

/// Choose the grid step (mm) from {5, 10, 20, 50, 100} such that the shorter
/// axis has roughly 5–10 divisions.
static float cam_pos_choose_grid_step(float range_x, float range_y)
{
    static const float k_candidates[] = {5.0f, 10.0f, 20.0f, 50.0f, 100.0f};
    static const int   k_n = 5;

    float shorter = 0.0f;
    if (range_x > 0.0f && range_y > 0.0f)
        shorter = (range_x < range_y) ? range_x : range_y;
    else if (range_x > 0.0f)
        shorter = range_x;
    else if (range_y > 0.0f)
        shorter = range_y;
    else
        return 10.0f;

    float best_step  = k_candidates[k_n - 1];
    float best_score = 1e9f;

    for (int i = 0; i < k_n; i++) {
        float divs  = shorter / k_candidates[i];
        float score = (divs < 5.0f) ? (5.0f - divs)
                    : (divs > 10.0f) ? (divs - 10.0f)
                    : 0.0f;  // perfect: within [5, 10]
        if (score < best_score) {
            best_score = score;
            best_step  = k_candidates[i];
        }
        if (score == 0.0f) break;  // can't do better
    }
    return best_step;
}

/// Screen-space context for physical ↔ screen coordinate conversion.
/// Y axis is FLIPPED: physical origin is at bottom-left, screen Y grows down.
typedef struct {
    float min_x, max_x;   ///< Physical X bounds (mm)
    float min_y, max_y;   ///< Physical Y bounds (mm)
    float phys_w, phys_h; ///< Physical extents
    float work_x0;        ///< Screen X of left edge of work area
    float work_y0;        ///< Screen Y of TOP edge of work area (physical Y-max)
    float work_w;         ///< Work area width  (screen px)
    float work_h;         ///< Work area height (screen px)
    lv_area_t img_area;   ///< Full image widget screen area
} grid_coord_ctx_t;

/// Convert physical (mm) coordinates to screen pixel coordinates.
/// X: left→right (same direction as screen X).
/// Y: FLIPPED – physical Y=min_y → screen bottom; Y=max_y → screen top.
static void phys_to_scr(const grid_coord_ctx_t *c, float px, float py,
                         int32_t *sx, int32_t *sy)
{
    *sx = (int32_t)(c->work_x0 + (px - c->min_x) / c->phys_w * c->work_w + 0.5f);
    *sy = (int32_t)(c->work_y0 + (c->max_y - py) / c->phys_h * c->work_h + 0.5f);
}

/// Draw two short yellow perpendicular lines (+X, +Y) and a filled dot at
/// the machine origin (physical 0, 0).
static void draw_machine_origin(lv_layer_t *layer, const grid_coord_ctx_t *c)
{
    int32_t ox, oy;
    phys_to_scr(c, 0.0f, 0.0f, &ox, &oy);

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(COL_MACH_ORIGIN_AXES);
    ld.width = 2;
    ld.opa   = OPA_MACH_ORIGIN_AXES;

    // +X arm (rightward)
    ld.p1.x = (lv_value_precise_t)ox;                        ld.p1.y = (lv_value_precise_t)oy;
    ld.p2.x = (lv_value_precise_t)(ox + MACH_ORIGIN_AXIS_LEN); ld.p2.y = (lv_value_precise_t)oy;
    lv_draw_line(layer, &ld);
    // +Y arm (upward on screen, i.e. decreasing screen Y because Y is flipped)
    ld.p1.x = (lv_value_precise_t)ox; ld.p1.y = (lv_value_precise_t)oy;
    ld.p2.x = (lv_value_precise_t)ox; ld.p2.y = (lv_value_precise_t)(oy - MACH_ORIGIN_AXIS_LEN);
    lv_draw_line(layer, &ld);

    // Dot at origin
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color     = lv_color_hex(COL_MACH_ORIGIN_DOT);
    rd.bg_opa       = OPA_MACH_ORIGIN_DOT;
    rd.border_color = lv_color_black();
    rd.border_width = 1;
    rd.border_opa   = LV_OPA_70;
    rd.radius       = LV_RADIUS_CIRCLE;
    lv_area_t da = { ox - MACH_ORIGIN_DOT_R, oy - MACH_ORIGIN_DOT_R,
                     ox + MACH_ORIGIN_DOT_R, oy + MACH_ORIGIN_DOT_R };
    lv_draw_rect(layer, &rd, &da);
}

/// Draw two green axis lines and a green dot with white outline at the
/// current WCS origin (machine coords: MPos − WPos).
static void draw_wcs_origin(lv_layer_t *layer, const grid_coord_ctx_t *c,
                              float wcs_ox, float wcs_oy)
{
    int32_t sx, sy;
    phys_to_scr(c, wcs_ox, wcs_oy, &sx, &sy);

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(COL_WCS_ORIGIN_AXES);
    ld.width = 2;
    ld.opa   = OPA_WCS_ORIGIN_AXES;

    // +X arm
    ld.p1.x = (lv_value_precise_t)sx;                        ld.p1.y = (lv_value_precise_t)sy;
    ld.p2.x = (lv_value_precise_t)(sx + MACH_ORIGIN_AXIS_LEN); ld.p2.y = (lv_value_precise_t)sy;
    lv_draw_line(layer, &ld);
    // +Y arm (upward)
    ld.p1.x = (lv_value_precise_t)sx; ld.p1.y = (lv_value_precise_t)sy;
    ld.p2.x = (lv_value_precise_t)sx; ld.p2.y = (lv_value_precise_t)(sy - MACH_ORIGIN_AXIS_LEN);
    lv_draw_line(layer, &ld);

    // Green dot with white border
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color     = lv_color_hex(COL_WCS_ORIGIN_DOT);
    rd.bg_opa       = LV_OPA_COVER;
    rd.border_color = lv_color_white();
    rd.border_width = 2;
    rd.border_opa   = LV_OPA_COVER;
    rd.radius       = LV_RADIUS_CIRCLE;
    lv_area_t wa = { sx - WCS_ORIGIN_DOT_R, sy - WCS_ORIGIN_DOT_R,
                     sx + WCS_ORIGIN_DOT_R, sy + WCS_ORIGIN_DOT_R };
    lv_draw_rect(layer, &rd, &wa);
}

/// Draw a red crosshair at the current absolute machine position.
static void draw_machine_pos_crosshair(lv_layer_t *layer,
                                        const grid_coord_ctx_t *c,
                                        float mpos_x, float mpos_y)
{
    int32_t sx, sy;
    phys_to_scr(c, mpos_x, mpos_y, &sx, &sy);

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(COL_MACH_POS_CROSS);
    ld.width = 2;
    ld.opa   = OPA_MACH_POS_CROSS;

    // Horizontal arms
    ld.p1.x = (lv_value_precise_t)(sx - MACH_CROSS_ARM); ld.p1.y = (lv_value_precise_t)sy;
    ld.p2.x = (lv_value_precise_t)(sx - MACH_CROSS_GAP); ld.p2.y = (lv_value_precise_t)sy;
    lv_draw_line(layer, &ld);
    ld.p1.x = (lv_value_precise_t)(sx + MACH_CROSS_GAP); ld.p1.y = (lv_value_precise_t)sy;
    ld.p2.x = (lv_value_precise_t)(sx + MACH_CROSS_ARM); ld.p2.y = (lv_value_precise_t)sy;
    lv_draw_line(layer, &ld);
    // Vertical arms
    ld.p1.x = (lv_value_precise_t)sx; ld.p1.y = (lv_value_precise_t)(sy - MACH_CROSS_ARM);
    ld.p2.x = (lv_value_precise_t)sx; ld.p2.y = (lv_value_precise_t)(sy - MACH_CROSS_GAP);
    lv_draw_line(layer, &ld);
    ld.p1.x = (lv_value_precise_t)sx; ld.p1.y = (lv_value_precise_t)(sy + MACH_CROSS_GAP);
    ld.p2.x = (lv_value_precise_t)sx; ld.p2.y = (lv_value_precise_t)(sy + MACH_CROSS_ARM);
    lv_draw_line(layer, &ld);
}

/// Draw a mm-spaced idealised grid over the camera image work area.
///
/// Grid lines are clipped to the margins-defined work area (they do NOT
/// extend into the margin regions).  Physical Y increases upward (CNC
/// convention): origin (0,0) is at the bottom-left of the work area.
///
/// In addition this function draws:
///  - Two short yellow lines + dot at the machine origin (0,0)
///  - Two green lines + white-outlined dot at the current WCS origin
///  - A red crosshair at the current absolute machine position
///
/// Step size is taken from grid_dx/grid_dy when the resulting line count is
/// manageable (≤ DRAW_AXIS_MAX_LINES), otherwise auto-chosen from
/// {5,10,20,50,100} mm for roughly 5–10 divisions.
#define DRAW_AXIS_MAX_LINES  20
static void draw_axis_grid(lv_layer_t *layer, lv_cam_pos_priv_t *priv)
{
    if (!priv->receiver) return;

    // Snapshot all grid fields needed under the lock.
    cam_receiver_lock_grid(priv->receiver);
    const cam_grid_info_t *g = cam_receiver_get_grid(priv->receiver);
    if (!g) {
        cam_receiver_unlock_grid(priv->receiver);
        return;
    }
    float cam_min_x  = g->min_x,  cam_max_x = g->max_x;
    float cam_min_y  = g->min_y,  cam_max_y = g->max_y;
    float grid_dx    = g->dx;
    float grid_dy    = g->dy;
    uint16_t inset_l = g->inset_left;
    uint16_t inset_t = g->inset_top;
    uint16_t inset_r = g->inset_right;
    uint16_t inset_b = g->inset_bottom;
    uint16_t src_w   = g->src_img_w;
    uint16_t src_h   = g->src_img_h;
    cam_receiver_unlock_grid(priv->receiver);

    uint16_t img_w = 0, img_h = 0;
    lv_cam_stream_get_image_size(priv->stream, &img_w, &img_h);
    if (img_w == 0 || img_h == 0) return;

    float phys_w = cam_max_x - cam_min_x;
    float phys_h = cam_max_y - cam_min_y;
    if (phys_w <= 0.0f || phys_h <= 0.0f) return;

    // Scale insets from calibration-image pixels to current display-image pixels.
    float sc_x = (src_w > 0 && src_w != img_w) ? (float)img_w / (float)src_w : 1.0f;
    float sc_y = (src_h > 0 && src_h != img_h) ? (float)img_h / (float)src_h : 1.0f;
    float il = (float)inset_l * sc_x;
    float it = (float)inset_t * sc_y;
    float ir = (float)inset_r * sc_x;
    float ib = (float)inset_b * sc_y;
    float active_img_w = (float)img_w - il - ir;
    float active_img_h = (float)img_h - it - ib;
    if (active_img_w < 1.0f) active_img_w = (float)img_w;
    if (active_img_h < 1.0f) active_img_h = (float)img_h;

    // Screen area of the image widget.
    lv_obj_t *img_obj = lv_cam_stream_get_image_obj(priv->stream);
    if (!img_obj) return;
    lv_area_t img_area;
    lv_obj_get_coords(img_obj, &img_area);
    int32_t scr_w = (int32_t)lv_area_get_width(&img_area);
    int32_t scr_h = (int32_t)lv_area_get_height(&img_area);
    if (scr_w <= 0 || scr_h <= 0) return;

    // Build the coordinate context.
    // work_y0 is the screen-top of the work area, which corresponds to
    // physical Y = cam_max_y (CNC Y increases upward, screen Y increases down).
    float scr_per_px_x = (float)scr_w / (float)img_w;
    float scr_per_px_y = (float)scr_h / (float)img_h;
    grid_coord_ctx_t cc;
    cc.min_x   = cam_min_x;  cc.max_x  = cam_max_x;
    cc.min_y   = cam_min_y;  cc.max_y  = cam_max_y;
    cc.phys_w  = phys_w;     cc.phys_h = phys_h;
    cc.work_x0 = (float)img_area.x1 + il * scr_per_px_x;
    cc.work_y0 = (float)img_area.y1 + it * scr_per_px_y;
    cc.work_w  = active_img_w * scr_per_px_x;
    cc.work_h  = active_img_h * scr_per_px_y;
    cc.img_area = img_area;
    if (cc.work_w <= 0.0f || cc.work_h <= 0.0f) return;

    // Integer work-area boundary for grid line clamping.
    int32_t wa_x1 = (int32_t)(cc.work_x0);
    int32_t wa_y1 = (int32_t)(cc.work_y0);
    int32_t wa_x2 = (int32_t)(cc.work_x0 + cc.work_w);
    int32_t wa_y2 = (int32_t)(cc.work_y0 + cc.work_h);

    // Step: prefer calibration dx/dy; fall back to auto-choose.
    float auto_step = cam_pos_choose_grid_step(phys_w, phys_h);
    float step_x = (grid_dx > 0.0f && phys_w / grid_dx <= DRAW_AXIS_MAX_LINES)
                   ? grid_dx : auto_step;
    float step_y = (grid_dy > 0.0f && phys_h / grid_dy <= DRAW_AXIS_MAX_LINES)
                   ? grid_dy : auto_step;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(GRID_LINE_COLOR);
    line_dsc.width = 1;
    line_dsc.opa   = GRID_LINE_OPA;

    // --- Vertical grid lines (constant physical X) — clipped to work area ---
    float x_first = ceilf(cam_min_x / step_x) * step_x;
    for (float x = x_first; x <= cam_max_x + 0.001f; x += step_x) {
        int32_t scr_x, dummy_y;
        phys_to_scr(&cc, x, 0.0f, &scr_x, &dummy_y);
        if (scr_x < wa_x1 || scr_x > wa_x2) continue;
        line_dsc.p1.x = (lv_value_precise_t)scr_x;
        line_dsc.p1.y = (lv_value_precise_t)wa_y1;
        line_dsc.p2.x = (lv_value_precise_t)scr_x;
        line_dsc.p2.y = (lv_value_precise_t)wa_y2;
        lv_draw_line(layer, &line_dsc);
    }

    // --- Horizontal grid lines (constant physical Y) — clipped to work area ---
    // Y is flipped: physical Y = cam_min_y → screen bottom; cam_max_y → screen top.
    float y_first = ceilf(cam_min_y / step_y) * step_y;
    for (float y = y_first; y <= cam_max_y + 0.001f; y += step_y) {
        int32_t dummy_x, scr_y;
        phys_to_scr(&cc, 0.0f, y, &dummy_x, &scr_y);
        if (scr_y < wa_y1 || scr_y > wa_y2) continue;
        line_dsc.p1.x = (lv_value_precise_t)wa_x1;
        line_dsc.p1.y = (lv_value_precise_t)scr_y;
        line_dsc.p2.x = (lv_value_precise_t)wa_x2;
        line_dsc.p2.y = (lv_value_precise_t)scr_y;
        lv_draw_line(layer, &line_dsc);
    }

    // --- Work-area outline: light-gray rounded rect around the calibrated
    //     region, below origin/WCS markings so they always read on top.
    {
        lv_draw_rect_dsc_t wa_dsc;
        lv_draw_rect_dsc_init(&wa_dsc);
        wa_dsc.bg_opa       = LV_OPA_TRANSP;
        wa_dsc.border_color = lv_color_hex(0xA0A8B0);
        wa_dsc.border_width = 2;
        wa_dsc.border_opa   = LV_OPA_50;
        wa_dsc.radius       = 3;
        lv_area_t wa_area = { wa_x1, wa_y1, wa_x2, wa_y2 };
        lv_draw_rect(layer, &wa_dsc, &wa_area);
    }

    // --- Machine origin: short yellow axes + dot at physical (0, 0) ---
    draw_machine_origin(layer, &cc);

    // --- WCS origin + machine position from machine_interface ---
    machine_interface_t *mi = priv->machine;
    if (mi) {
        // WCS origin in machine (physical) coords: MPos − WPos
        float wcs_ox = mi->position[0] - mi->wcs_position[0];
        float wcs_oy = mi->position[1] - mi->wcs_position[1];
        draw_wcs_origin(layer, &cc, wcs_ox, wcs_oy);

        // Current absolute machine position — red crosshair
        draw_machine_pos_crosshair(layer, &cc, mi->position[0], mi->position[1]);
    }
}

// ---------------------------------------------------------------------------
// Probe wizard — internal utilities
// ---------------------------------------------------------------------------

/// Destroy an LVGL object (if valid) and NULL the pointer.
static void _wiz_del(lv_obj_t **pobj)
{
    if (*pobj && lv_obj_is_valid(*pobj)) {
        lv_obj_delete(*pobj);
    }
    *pobj = NULL;
}

// ---------------------------------------------------------------------------
// Probe wizard — zoom helpers
// ---------------------------------------------------------------------------

/// Compute the zoom window (center and size in image pixels) from the
/// currently collected wizard points.
///
/// For rect/block/pocket:  bounding box of pt1 + pt2 defines the feature.
/// For bore/boss/circle:   pt1 is centre, pt2 defines the radius.
/// For single-point ops:   pt1 is the focus; uses a small fixed feature box.
///
/// The zoom window is scaled so the feature's longer dimension fills 60%
/// of the corresponding output dimension (20% margin on each side), with
/// the window constrained to the output aspect ratio.
///
/// Returns false if there is not enough data (e.g. pt1 not set).
static bool wiz_compute_zoom_window(lv_cam_pos_priv_t *priv,
                                     uint16_t *out_cx, uint16_t *out_cy,
                                     uint16_t *out_w,  uint16_t *out_h)
{
    if (!priv->wiz_pt1_set) return false;

    uint16_t img_w = 0, img_h = 0;
    lv_cam_stream_get_image_size(priv->stream, &img_w, &img_h);
    if (img_w == 0 || img_h == 0) return false;

    float feat_cx, feat_cy, feat_w, feat_h;
    const float pt1x = (float)priv->wiz_pt1.px_x;
    const float pt1y = (float)priv->wiz_pt1.px_y;

    if (priv->wiz_pt2_set) {
        const float pt2x = (float)priv->wiz_pt2.px_x;
        const float pt2y = (float)priv->wiz_pt2.px_y;

        if (priv->wiz_op == PROBE_OP_PROBE_BORE ||
            priv->wiz_op == PROBE_OP_PROBE_BOSS) {
            // Circle: pt1 = centre, pt2 = edge point
            float dx = pt2x - pt1x;
            float dy = pt2y - pt1y;
            float r  = sqrtf(dx * dx + dy * dy);
            feat_cx  = pt1x;
            feat_cy  = pt1y;
            feat_w   = feat_h = 2.0f * r;
        } else {
            // Rectangle: bounding box of the two corners
            float x0 = (pt1x < pt2x) ? pt1x : pt2x;
            float y0 = (pt1y < pt2y) ? pt1y : pt2y;
            float x1 = (pt1x > pt2x) ? pt1x : pt2x;
            float y1 = (pt1y > pt2y) ? pt1y : pt2y;
            feat_cx  = (x0 + x1) * 0.5f;
            feat_cy  = (y0 + y1) * 0.5f;
            feat_w   = x1 - x0;
            feat_h   = y1 - y0;
        }
    } else {
        // Single-point: zoom to a small fixed window centred on pt1.
        feat_cx = pt1x;
        feat_cy = pt1y;
        feat_w  = feat_h = 0.0f;  // Use minimum window (enforced below)
    }

    // Enforce a minimum feature footprint so tiny / zero-size features still
    // produce a useful zoom level.
    static const float MIN_FEAT_PX = 20.0f;
    if (feat_w < MIN_FEAT_PX) feat_w = MIN_FEAT_PX;
    if (feat_h < MIN_FEAT_PX) feat_h = MIN_FEAT_PX;

    // Scale the zoom window so the LONGER feature dimension fills 60% of the
    // corresponding output dimension.  The window must match the output aspect
    // ratio (same W×H as the output frame) so the resampled zoom frame fits.
    float out_ar = (float)img_w / (float)img_h;
    // Minimum crop that achieves 60% fill on each axis independently,
    // then pick the most conservative (larger) crop and apply aspect ratio.
    float crop_from_w = feat_w / 0.6f;
    float crop_from_h = feat_h / 0.6f;
    float raw_w = (crop_from_w > crop_from_h * out_ar)
                  ? crop_from_w : crop_from_h * out_ar;
    float raw_h = raw_w / out_ar;

    // Never produce a zoom window smaller than 40×30 px (avoids absurd zoom).
    static const float MIN_CROP_W = 40.0f;
    if (raw_w < MIN_CROP_W) { raw_w = MIN_CROP_W; raw_h = raw_w / out_ar; }

    // If the computed crop covers ≥ 90% of the image, zoom is effectively a
    // no-op — clamp to the full image so the camera just sends a normal frame.
    if (raw_w >= (float)img_w * 0.9f) {
        raw_w = (float)img_w;
        raw_h = (float)img_h;
    }

    // Clamp centre so the window stays fully within the image.
    float half_w = raw_w * 0.5f, half_h = raw_h * 0.5f;
    if (feat_cx < half_w) feat_cx = half_w;
    if (feat_cx > (float)img_w - half_w) feat_cx = (float)img_w - half_w;
    if (feat_cy < half_h) feat_cy = half_h;
    if (feat_cy > (float)img_h - half_h) feat_cy = (float)img_h - half_h;

    *out_cx = (uint16_t)(feat_cx + 0.5f);
    *out_cy = (uint16_t)(feat_cy + 0.5f);
    *out_w  = (uint16_t)(raw_w  + 0.5f);
    *out_h  = (uint16_t)(raw_h  + 0.5f);
    return true;
}

/// Callback for the "(X)" zoom badge close button.
static void _zoom_badge_close_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;
    wiz_deactivate_zoom(priv);
}

/// Remap a collected point's pixel position between the original (unzoomed)
/// image coordinate space and the zoomed crop-window space.
/// forward=true  : original → zoomed  (call on zoom activation)
/// forward=false : zoomed  → original (call on zoom deactivation)
static void _wiz_zoom_remap_pt(lv_cam_pos_point_t *pt, bool set,
                                 float left, float top,
                                 float out_w, float out_h,
                                 float zoom_w, float zoom_h,
                                 bool forward)
{
    if (!set || zoom_w <= 0 || zoom_h <= 0 || out_w <= 0 || out_h <= 0) return;
    float nx, ny;
    if (forward) {
        // (orig_px - left) * scale  →  zoomed_px
        nx = (pt->px_x - left) * out_w / zoom_w;
        ny = (pt->px_y - top)  * out_h / zoom_h;
    } else {
        // zoomed_px / scale + left  →  orig_px
        nx = pt->px_x * zoom_w / out_w + left;
        ny = pt->px_y * zoom_h / out_h + top;
    }
    pt->px_x = (int16_t)LV_CLAMP(-32767, (int)roundf(nx), 32767);
    pt->px_y = (int16_t)LV_CLAMP(-32767, (int)roundf(ny), 32767);
}

/// Activate zoom: compute the window, remap collected point pixels,
/// send SET_ZOOM to the camera, and show the badge overlay.
static void wiz_activate_zoom(lv_cam_pos_priv_t *priv)
{
    if (!priv->receiver || !priv->stream) return;

    uint16_t cx = 0, cy = 0, zw = 0, zh = 0;
    if (!wiz_compute_zoom_window(priv, &cx, &cy, &zw, &zh)) return;

    // Get current output size to compute the forward pixel remap.
    uint16_t out_w = 0, out_h = 0;
    lv_cam_stream_get_image_size(priv->stream, &out_w, &out_h);

    // Defer the pixel remap: the camera must rebuild its pipeline and deliver
    // a new keyframe (+ grid) before the zoomed image is visible.  If we
    // remapped now the points would jump to wrong positions on the still-
    // unzoomed image.  grid_dirty_timer_cb applies the remap once the first
    // post-zoom grid update arrives, confirming the new frame is on its way.
    priv->wiz_zoom_cx = cx;  priv->wiz_zoom_cy = cy;
    priv->wiz_zoom_w  = zw;  priv->wiz_zoom_h  = zh;
    priv->wiz_zoom_active        = true;
    priv->wiz_zoom_remap_pending = true;

    int rc = cam_receiver_set_zoom(priv->receiver, cx, cy, zw, zh);
    if (rc != 0) LOGW(TAG, "wiz_activate_zoom: send failed (rc=%d)", rc);

    // Create badge regardless of send success — the user needs the cancel
    // affordance and visual feedback of zoom state even during reconnection.
    if (!priv->overlay) return;
    _wiz_del(&priv->zoom_badge);

    lv_obj_t *badge = lv_obj_create(priv->overlay);
    lv_obj_remove_style_all(badge);
    lv_obj_set_style_bg_color(badge, lv_color_hex(WIZ_ZOOM_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(badge,   LV_OPA_COVER, 0);
    lv_obj_set_style_radius(badge,   6, 0);
    lv_obj_set_style_pad_hor(badge,  8, 0);
    lv_obj_set_style_pad_ver(badge,  4, 0);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_layout(badge, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(badge, 4, 0);

    lv_obj_t *lbl = lv_label_create(badge);
    lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN " ZOOM");
    lv_obj_set_style_text_color(lbl, lv_color_hex(WIZ_ZOOM_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    lv_obj_t *close_lbl = lv_label_create(badge);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(WIZ_ZOOM_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_12, 0);

    lv_obj_add_event_cb(badge, _zoom_badge_close_cb, LV_EVENT_CLICKED, priv);

    // Position AFTER children are added so LV_SIZE_CONTENT has something to
    // measure.  lv_obj_set_pos sets the top-left corner of the badge relative
    // to the overlay's content area, independent of badge size.
    lv_obj_set_pos(badge, WIZ_ZOOM_PAD, WIZ_ZOOM_PAD);

    priv->zoom_badge = badge;
    LOGI(TAG, "Zoom activated: cx=%u cy=%u w=%u h=%u out=%ux%u",
         cx, cy, zw, zh, out_w, out_h);
}

/// Deactivate zoom: inverse-remap collected point pixels back to unzoomed
/// image space, send CLEAR_ZOOM, and remove the badge.
static void wiz_deactivate_zoom(lv_cam_pos_priv_t *priv)
{
    if (!priv->wiz_zoom_active) return;
    priv->wiz_zoom_active = false;

    // Use lv_obj_delete_async: wiz_deactivate_zoom can be called from inside
    // the badge's own CLICKED event (_zoom_badge_close_cb).  Synchronous
    // lv_obj_delete of the current event target corrupts LVGL's event-dispatch
    // chain.  Async delete defers until the current lv_task_handler pass ends.
    if (priv->zoom_badge && lv_obj_is_valid(priv->zoom_badge))
        lv_obj_delete_async(priv->zoom_badge);
    priv->zoom_badge = NULL;

    if (priv->wiz_zoom_remap_pending) {
        // The zoomed keyframe never arrived (or the user cancelled fast).
        // Points are still in original (pre-zoom) coordinates — nothing to undo.
        priv->wiz_zoom_remap_pending = false;
    } else {
        // Inverse-remap collected point payload pixels: zoomed → original frame.
        if (priv->stream && priv->wiz_zoom_w && priv->wiz_zoom_h) {
            uint16_t out_w = 0, out_h = 0;
            lv_cam_stream_get_image_size(priv->stream, &out_w, &out_h);
            if (out_w && out_h) {
                float left = priv->wiz_zoom_cx - priv->wiz_zoom_w * 0.5f;
                float top  = priv->wiz_zoom_cy - priv->wiz_zoom_h * 0.5f;
                _wiz_zoom_remap_pt(&priv->wiz_pt1,  priv->wiz_pt1_set,  left, top,
                                    out_w, out_h, priv->wiz_zoom_w, priv->wiz_zoom_h, false);
                _wiz_zoom_remap_pt(&priv->wiz_pt2,  priv->wiz_pt2_set,  left, top,
                                    out_w, out_h, priv->wiz_zoom_w, priv->wiz_zoom_h, false);
                _wiz_zoom_remap_pt(&priv->wiz_zref, priv->wiz_zref_set, left, top,
                                    out_w, out_h, priv->wiz_zoom_w, priv->wiz_zoom_h, false);
            }
        }
    }
    priv->wiz_zoom_cx = priv->wiz_zoom_cy = priv->wiz_zoom_w = priv->wiz_zoom_h = 0;

    if (priv->receiver) cam_receiver_clear_zoom(priv->receiver);
    LOGD(TAG, "Zoom deactivated");
}

/// Reset wizard to idle state — cancels any running op, clears all points,
/// rebuilds the sidebar.  Safe to call at any time including from on_delete.
// ---------------------------------------------------------------------------
// Move-point mode helpers
// ---------------------------------------------------------------------------

/// Timer callback that toggles the blink phase and triggers an overlay repaint.
static void _move_pt_blink_cb(lv_timer_t *t)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_timer_get_user_data(t);
    if (!priv) return;
    priv->wiz_move_pt_blink_on = !priv->wiz_move_pt_blink_on;
    if (priv->overlay && lv_obj_is_valid(priv->overlay))
        lv_obj_invalidate(priv->overlay);
}

/// Enter move-point mode for the given point index (0=pt1, 1=pt2, 2=zref).
/// Starts the blink timer and immediately repaints the overlay.
static void wiz_start_move_pt(lv_cam_pos_priv_t *priv, int8_t which)
{
    priv->wiz_move_pt_active   = true;
    priv->wiz_move_pt_which    = which;
    priv->wiz_move_pt_blink_on = true;
    if (!priv->wiz_move_pt_blink_timer) {
        priv->wiz_move_pt_blink_timer =
            lv_timer_create(_move_pt_blink_cb, 400, priv);
    }
    if (priv->overlay && lv_obj_is_valid(priv->overlay))
        lv_obj_invalidate(priv->overlay);
}

/// Exit move-point mode, stop the blink timer and repaint.
static void wiz_cancel_move_pt(lv_cam_pos_priv_t *priv)
{
    priv->wiz_move_pt_active   = false;
    priv->wiz_move_pt_which    = -1;
    priv->wiz_move_pt_blink_on = false;
    if (priv->wiz_move_pt_blink_timer) {
        lv_timer_delete(priv->wiz_move_pt_blink_timer);
        priv->wiz_move_pt_blink_timer = NULL;
    }
}

static void wiz_reset(lv_cam_pos_priv_t *priv)
{
    // Exit move-point mode first (stops blink timer, clears flags).
    wiz_cancel_move_pt(priv);

    // Deactivate any active zoom so the camera returns to its normal view.
    wiz_deactivate_zoom(priv);

    // Null event callbacks before cancelling so probe_api_cancel doesn't
    // fire into tearing-down UI (avoids re-entrant LVGL + dangling-priv).
    if (priv->probe_cbs_set) {
        priv->probe_ctx.callbacks.on_status       = NULL;
        priv->probe_ctx.callbacks.on_error        = NULL;
        priv->probe_ctx.callbacks.on_move_done    = NULL;
        priv->probe_ctx.callbacks.on_rect_done    = NULL;
        priv->probe_ctx.callbacks.on_circle_done  = NULL;
        probe_api_cancel(&priv->probe_ctx);
    }

    _wiz_del(&priv->wiz_confirm_modal);

    priv->wiz_state    = WIZ_IDLE;
    priv->wiz_op       = PROBE_OP_NONE;
    priv->wiz_pt1_set  = false;
    priv->wiz_pt2_set  = false;
    priv->wiz_zref_set = false;
    priv->wiz_step_idx = 0;
    priv->wiz_drag_pt  = -1;

    // Sidebar panels exist as permanent children — just rebuild their contents.
    if (priv->step_panel  && lv_obj_is_valid(priv->step_panel))  sidebar_rebuild(priv);
    if (priv->coord_panel && lv_obj_is_valid(priv->coord_panel)) sidebar_update_coords(priv);

    if (priv->overlay && lv_obj_is_valid(priv->overlay))
        lv_obj_invalidate(priv->overlay);
}

// ---------------------------------------------------------------------------
// Probe wizard — probe_api async result delivery
// ---------------------------------------------------------------------------

/// Heap-allocated context passed via lv_async_call so that the result
/// callback is always executed on the LVGL task, regardless of which
/// FreeRTOS task fires probe_api event callbacks.
typedef struct {
    lv_obj_t      *root;      ///< cam_positioning root object (validity guard)
    probe_status_t status;
    probe_op_t     op;
    union {
        probe_result_move_t   move;
        probe_result_rect_t   rect;
        probe_result_circle_t circle;
    } result;
    char           error_msg[64];
} wiz_async_result_t;

static void _result_ok_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (mbox && lv_obj_is_valid(mbox)) lv_msgbox_close_async(mbox);  /* async: button is child of mbox */
}

static void _probe_done_async(void *user_data)
{
    wiz_async_result_t *ac = (wiz_async_result_t *)user_data;
    if (!ac) return;

    if (!ac->root || !lv_obj_is_valid(ac->root)) { free(ac); return; }
    lv_cam_pos_priv_t *priv = get_priv(ac->root);
    if (!priv) { free(ac); return; }

    // Operation done — clear all wizard state and return to idle
    priv->wiz_state    = WIZ_IDLE;
    priv->wiz_op       = PROBE_OP_NONE;
    priv->wiz_pt1_set  = false;  memset(&priv->wiz_pt1,  0, sizeof(priv->wiz_pt1));
    priv->wiz_pt2_set  = false;  memset(&priv->wiz_pt2,  0, sizeof(priv->wiz_pt2));
    priv->wiz_zref_set = false;  memset(&priv->wiz_zref, 0, sizeof(priv->wiz_zref));
    priv->wiz_step_idx = 0;
    wiz_cancel_move_pt(priv);    // clear any pending move-point mode
    wiz_deactivate_zoom(priv);   // ensure camera returns to normal view
    sidebar_rebuild(priv);
    sidebar_update_coords(priv);
    lv_obj_invalidate(priv->overlay);

    // Build result / error text
    char title_buf[48];
    char text_buf[192];

    if (ac->status == PROBE_STATUS_DONE) {
        switch (ac->op) {
        case PROBE_OP_MOVE_TO:
            snprintf(title_buf, sizeof(title_buf), "Moved");
            snprintf(text_buf, sizeof(text_buf),
                     "X: %.4f\nY: %.4f",
                     (double)ac->result.move.target_xy.x,
                     (double)ac->result.move.target_xy.y);
            break;
        case PROBE_OP_PROBE_Z:
            snprintf(title_buf, sizeof(title_buf), "Z Probed");
            snprintf(text_buf, sizeof(text_buf),
                     "Z surface: %.4f mm",
                     (double)ac->result.move.z_surface);
            break;
        case PROBE_OP_PROBE_POCKET:
        case PROBE_OP_PROBE_RECT:
            snprintf(title_buf, sizeof(title_buf), "Rectangle Probed");
            snprintf(text_buf, sizeof(text_buf),
                     "Centre: X %.4f  Y %.4f\nW: %.4f  H: %.4f\nZ: %.4f",
                     (double)ac->result.rect.center.x,
                     (double)ac->result.rect.center.y,
                     (double)ac->result.rect.width,
                     (double)ac->result.rect.height,
                     (double)ac->result.rect.z_surface);
            break;
        case PROBE_OP_PROBE_BORE:
        case PROBE_OP_PROBE_BOSS:
            snprintf(title_buf, sizeof(title_buf), "Circle Probed");
            snprintf(text_buf, sizeof(text_buf),
                     "Centre: X %.4f  Y %.4f\nDiameter: %.4f mm\nZ: %.4f",
                     (double)ac->result.circle.center.x,
                     (double)ac->result.circle.center.y,
                     (double)ac->result.circle.diameter,
                     (double)ac->result.circle.z_surface);
            break;
        default:
            snprintf(title_buf, sizeof(title_buf), "Done");
            text_buf[0] = '\0';
            break;
        }
    } else {
        snprintf(title_buf, sizeof(title_buf),
                 ac->status == PROBE_STATUS_CANCELLED ? "Cancelled" : "Error");
        snprintf(text_buf, sizeof(text_buf), "%s",
                 ac->error_msg[0] ? ac->error_msg : "Operation failed.");
    }

    lv_obj_t *mbox = lv_msgbox_create(lv_screen_active());
    lv_msgbox_add_title(mbox, title_buf);
    lv_msgbox_add_text(mbox, text_buf);
    lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "OK");
    lv_obj_add_event_cb(ok, _result_ok_cb, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);

    free(ac);
}

/// Called by probe_api from (potentially) a non-LVGL task; posts to LVGL thread.
static void _probe_on_status(probe_api_ctx_t *ctx, probe_status_t status,
                               void *user_data)
{
    if (status != PROBE_STATUS_DONE &&
        status != PROBE_STATUS_ERROR &&
        status != PROBE_STATUS_CANCELLED) return;

    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)user_data;
    if (!priv) return;

    wiz_async_result_t *ac = (wiz_async_result_t *)calloc(1, sizeof(*ac));
    if (!ac) return;
    ac->root   = priv->root;
    ac->status = status;
    ac->op     = ctx->op;
    if (status == PROBE_STATUS_DONE) {
        switch (ctx->op) {
        case PROBE_OP_MOVE_TO:
        case PROBE_OP_PROBE_Z:    ac->result.move   = ctx->result_move;   break;
        case PROBE_OP_PROBE_POCKET:
        case PROBE_OP_PROBE_RECT: ac->result.rect   = ctx->result_rect;   break;
        case PROBE_OP_PROBE_BORE:
        case PROBE_OP_PROBE_BOSS: ac->result.circle = ctx->result_circle; break;
        default: break;
        }
    }
    lv_async_call(_probe_done_async, ac);
}

static void _probe_on_error(probe_api_ctx_t *ctx, probe_status_t status,
                             const char *message, void *user_data)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)user_data;
    if (!priv) return;

    wiz_async_result_t *ac = (wiz_async_result_t *)calloc(1, sizeof(*ac));
    if (!ac) return;
    ac->root   = priv->root;
    ac->status = status;
    ac->op     = ctx->op;
    if (message) {
        strncpy(ac->error_msg, message, sizeof(ac->error_msg) - 1);
    }
    lv_async_call(_probe_done_async, ac);
}

// ---------------------------------------------------------------------------
// Probe wizard — command callback forwarders
// (wrap external cmd callbacks so user_data can stay as priv)
// ---------------------------------------------------------------------------
static void _fwd_move_to(probe_api_ctx_t *ctx, float x, float y,
                          float safe_z, void *ud)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)ud;
    if (priv->probe_cmd_move_to)
        priv->probe_cmd_move_to(ctx, x, y, safe_z, priv->probe_user_data);
}
static void _fwd_probe_z(probe_api_ctx_t *ctx, float max_depth, void *ud)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)ud;
    if (priv->probe_cmd_probe_z)
        priv->probe_cmd_probe_z(ctx, max_depth, priv->probe_user_data);
}
static void _fwd_probe_rect(probe_api_ctx_t *ctx,
                             float cx, float cy, float w, float h,
                             float sz, float pz, bool inside, void *ud)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)ud;
    if (priv->probe_cmd_probe_rect)
        priv->probe_cmd_probe_rect(ctx, cx, cy, w, h, sz, pz, inside,
                                    priv->probe_user_data);
}
static void _fwd_probe_circle(probe_api_ctx_t *ctx,
                               float cx, float cy, float dia,
                               float sz, float pz, bool inside, void *ud)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)ud;
    if (priv->probe_cmd_probe_circle)
        priv->probe_cmd_probe_circle(ctx, cx, cy, dia, sz, pz, inside,
                                      priv->probe_user_data);
}

/// (Re-)initialise the internal probe_api_ctx_t after probe callbacks change.
static void _wiz_reinit_probe_ctx(lv_cam_pos_priv_t *priv)
{
    probe_api_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.cmd_move_to      = _fwd_move_to;
    cbs.cmd_probe_z      = _fwd_probe_z;
    cbs.cmd_probe_rect   = _fwd_probe_rect;
    cbs.cmd_probe_circle = _fwd_probe_circle;
    cbs.on_status        = _probe_on_status;
    cbs.on_error         = _probe_on_error;
    cbs.user_data        = priv;   // priv is the single user_data; forwarders use probe_user_data
    probe_api_init(&priv->probe_ctx, &cbs);
    priv->probe_cbs_set = true;
}

// ---------------------------------------------------------------------------
// Probe wizard — step / op definitions
// ---------------------------------------------------------------------------

typedef enum { WIZ_SK_PT1=0, WIZ_SK_PT2=1, WIZ_SK_ZREF=2, WIZ_SK_RUN=3 } wiz_step_kind_t;
typedef struct { wiz_step_kind_t kind; const char *label; } wiz_step_def_t;
typedef struct { probe_op_t op; const char *symbol; const char *label; uint32_t color; } wiz_op_def_t;

static const wiz_step_def_t k_steps_move[2] = {
    {WIZ_SK_PT1,"Point"  }, {WIZ_SK_RUN,"Run"} };
static const wiz_step_def_t k_steps_block[4] = {
    {WIZ_SK_PT1,"Corner1"}, {WIZ_SK_PT2,"Corner2"},
    {WIZ_SK_ZREF,"Z-Surf"}, {WIZ_SK_RUN,"Run"} };
static const wiz_step_def_t k_steps_bore[4] = {
    {WIZ_SK_PT1,"Center" }, {WIZ_SK_PT2,"Edge"   },
    {WIZ_SK_ZREF,"Z-Surf"}, {WIZ_SK_RUN,"Run"} };

static const wiz_step_def_t * const k_op_steps[] = {
    [PROBE_OP_NONE]         = NULL,
    [PROBE_OP_MOVE_TO]      = k_steps_move,
    [PROBE_OP_PROBE_Z]      = k_steps_move,
    [PROBE_OP_PROBE_POCKET] = k_steps_block,
    [PROBE_OP_PROBE_RECT]   = k_steps_block,
    [PROBE_OP_PROBE_BORE]   = k_steps_bore,
    [PROBE_OP_PROBE_BOSS]   = k_steps_bore,
};
static const int k_op_step_counts[] = {
    [PROBE_OP_NONE]         = 0,
    [PROBE_OP_MOVE_TO]      = 2,
    [PROBE_OP_PROBE_Z]      = 2,
    [PROBE_OP_PROBE_POCKET] = 4,
    [PROBE_OP_PROBE_RECT]   = 4,
    [PROBE_OP_PROBE_BORE]   = 4,
    [PROBE_OP_PROBE_BOSS]   = 4,
};

#define WIZ_OP_COUNT 5
static const wiz_op_def_t k_op_defs[WIZ_OP_COUNT] = {
    { PROBE_OP_MOVE_TO,      LV_SYMBOL_GPS,     "Move to", WIZ_BTN_MOVE_COLOR },
    { PROBE_OP_PROBE_RECT,   LV_SYMBOL_LOOP,    "Block",   WIZ_BTN_RECT_COLOR },
    { PROBE_OP_PROBE_POCKET, LV_SYMBOL_LOOP,    "Pocket",  WIZ_BTN_RECT_COLOR },
    { PROBE_OP_PROBE_BOSS,   LV_SYMBOL_POWER,   "Boss",    WIZ_BTN_CIRC_COLOR },
    { PROBE_OP_PROBE_BORE,   LV_SYMBOL_POWER,   "Bore",    WIZ_BTN_CIRC_COLOR },
};

// ---------------------------------------------------------------------------
// Probe wizard — sidebar management
// ---------------------------------------------------------------------------

/// Style a button produced by lv_list_add_button() for the sidebar.
/// Animation callback for the active wizard step button: blinks between
/// green+white (v < 128) and yellow+black (v >= 128).
static void _step_blink_cb(void *obj, int32_t v)
{
    lv_obj_t *btn = (lv_obj_t *)obj;
    lv_color_t bg  = (v < 128) ? lv_color_hex(WIZ_STEP_CURR_COLOR) : lv_color_hex(0xEAB308);
    lv_color_t txt = (v < 128) ? lv_color_white()                   : lv_color_black();
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_text_color(btn, txt, 0);
    for (uint32_t ci = 0; ci < lv_obj_get_child_count(btn); ci++)
        lv_obj_set_style_text_color(lv_obj_get_child(btn, ci), txt, 0);
}

static void _lst_style(lv_obj_t *btn, uint32_t bg_hex, bool enabled)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_opa(btn,   enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn,   LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn,   WIZ_STEP_RADIUS, 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    /* Tint all child labels (icon + text children from lv_list_add_button) */
    for (uint32_t ci = 0; ci < lv_obj_get_child_count(btn); ci++)
        lv_obj_set_style_text_color(lv_obj_get_child(btn, ci), lv_color_white(), 0);
    if (!enabled) lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    else          lv_obj_add_flag (btn, LV_OBJ_FLAG_CLICKABLE);
}

/// Rebuild the coord_panel to show currently collected point coordinates.
static void sidebar_update_coords(lv_cam_pos_priv_t *priv)
{
    if (!priv->coord_panel || !lv_obj_is_valid(priv->coord_panel)) return;
    lv_obj_clean(priv->coord_panel);

    const lv_cam_pos_point_t *pts[3] = {
        &priv->wiz_pt1, &priv->wiz_pt2, &priv->wiz_zref };
    const bool set[3] = {
        priv->wiz_pt1_set, priv->wiz_pt2_set, priv->wiz_zref_set };
    const char *labels[3] = {"P1","P2","Z "};

    bool any = false;
    for (int i = 0; i < 3; i++) {
        if (!set[i]) continue;
        any = true;
        char buf[36];

        if (pts[i]->has_physical) {
            snprintf(buf, sizeof(buf), "%s  X:%.2f  Y:%.2f",
                     labels[i], (double)pts[i]->phys_x, (double)pts[i]->phys_y);
        } else {
            snprintf(buf, sizeof(buf), "%s  X:--  Y:--", labels[i]);
        }
        lv_obj_t *lbl = lv_label_create(priv->coord_panel);
        lv_label_set_text(lbl, buf);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_obj_set_style_text_color(lbl, lv_color_hex(WIZ_XY_COLOR), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    }
    (void)any;  // hint is now shown in step_panel (below op buttons), not here
}

/// Op-selector button: activate wizard with chosen operation.
static void _wiz_op_btn_cb(lv_event_t *e)
{
    sidebar_btn_ctx_t *ctx  = (sidebar_btn_ctx_t *)lv_event_get_user_data(e);
    if (!ctx || !ctx->priv) return;
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)ctx->priv;
    if (priv->wiz_state != WIZ_OP_SELECT && priv->wiz_state != WIZ_IDLE) return;

    priv->wiz_op = ctx->op;

    // Always clear pt2 and zref — they belong to the previous op (if any).
    priv->wiz_pt2_set  = false;  memset(&priv->wiz_pt2,  0, sizeof(priv->wiz_pt2));
    priv->wiz_zref_set = false;  memset(&priv->wiz_zref, 0, sizeof(priv->wiz_zref));

    if (priv->wiz_state == WIZ_OP_SELECT) {
        // pt1 already placed before mode was chosen — count it as step 0 done
        // and jump straight to step 1 (pt2 / zref / run, depending on op).
        priv->wiz_step_idx = 1;
        priv->wiz_state    = (k_op_step_counts[ctx->op] > 2) ? WIZ_COLLECTING : WIZ_READY;
        // For single-point ops (move-to / probe-z) that jump straight to READY,
        // activate zoom now so the user can fine-tune pt1 before executing.
        if (priv->wiz_state == WIZ_READY) {
            wiz_deactivate_zoom(priv);
            wiz_activate_zoom(priv);
        }
    } else {
        // No point placed yet — start collection from step 0.
        priv->wiz_pt1_set  = false;  memset(&priv->wiz_pt1, 0, sizeof(priv->wiz_pt1));
        priv->wiz_step_idx = 0;
        priv->wiz_state    = WIZ_COLLECTING;
    }
    sidebar_rebuild(priv);
    sidebar_update_coords(priv);
    lv_obj_invalidate(priv->overlay);
    LOGD(TAG, "Op selected: %d state=%d", ctx->op, priv->wiz_state);
}

/// Step button: rewind the wizard to collect (or re-collect) an earlier point.
static void _wiz_step_btn_cb(lv_event_t *e)
{
    sidebar_btn_ctx_t *ctx  = (sidebar_btn_ctx_t *)lv_event_get_user_data(e);
    if (!ctx || !ctx->priv) return;
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)ctx->priv;
    int k = ctx->step_idx;
    if (k < 0 || k >= priv->wiz_step_idx) return;  // guard: only past steps

    // Clear collected points from step k forward
    if (k <= 0) { priv->wiz_pt1_set  = false; memset(&priv->wiz_pt1,  0, sizeof(priv->wiz_pt1));  }
    if (k <= 1) { priv->wiz_pt2_set  = false; memset(&priv->wiz_pt2,  0, sizeof(priv->wiz_pt2));  }
    if (k <= 2) { priv->wiz_zref_set = false; memset(&priv->wiz_zref, 0, sizeof(priv->wiz_zref)); }

    priv->wiz_step_idx = k;
    priv->wiz_state    = WIZ_COLLECTING;

    // Deactivate zoom if we're rewinding to a step before the trigger point.
    // This prevents a stale zoom (from now-invalid pt2/pt1) from persisting.
#if CAM_POS_ZOOM_AFTER_PT2
    if (k <= 1 && priv->wiz_zoom_active) wiz_deactivate_zoom(priv);
#else
    if (k <= 2 && priv->wiz_zoom_active) wiz_deactivate_zoom(priv);
#endif

    sidebar_rebuild(priv);
    sidebar_update_coords(priv);
    lv_obj_invalidate(priv->overlay);
}

/// Reset/cancel button: clear everything and return to idle.
static void _wiz_reset_btn_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;
    wiz_reset(priv);
}

/// Run button: launch the confirmation dialog.
static void _wiz_run_btn_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;
    if (priv->wiz_state != WIZ_READY) return;
    wiz_show_confirm(priv);
}

/// Create and return an lv_list that fills priv->step_panel.
/// The list is dark-themed, no scrolling, LV_SIZE_CONTENT height.
static lv_obj_t *_make_sidebar_list(lv_cam_pos_priv_t *priv)
{
    lv_obj_t *lst = lv_list_create(priv->step_panel);
    lv_obj_set_size(lst, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(lst, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(lst,      lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(lst,        LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lst,  0, 0);
    lv_obj_set_style_pad_all(lst,       0, 0);
    lv_obj_set_style_pad_row(lst,       WIZ_PANEL_PAD, 0);
    return lst;
}

/// Style the header text row produced by lv_list_add_text().
static void _lst_hdr_style(lv_obj_t *hdr)
{
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(hdr,   LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(WIZ_TXT_COLOR), 0);
    lv_obj_set_style_text_font(hdr,  &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(hdr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(hdr,  2, 0);
}

/// Rebuild the step_panel content for the current wizard state.
/// Called after any state change that affects the sidebar.
static void sidebar_rebuild(lv_cam_pos_priv_t *priv)
{
    if (!priv->step_panel || !lv_obj_is_valid(priv->step_panel)) return;
    lv_obj_clean(priv->step_panel);

    lv_obj_t *lst = _make_sidebar_list(priv);
    int btn_idx = 0;  // index into sidebar_btn_ctxs[]

    // --- Op selector (IDLE or OP_SELECT) -----------------------------------
    if (priv->wiz_state == WIZ_IDLE || priv->wiz_state == WIZ_OP_SELECT) {
        for (int i = 0; i < WIZ_OP_COUNT && btn_idx < 8; i++) {
            priv->sidebar_btn_ctxs[btn_idx].priv     = priv;
            priv->sidebar_btn_ctxs[btn_idx].op       = k_op_defs[i].op;
            priv->sidebar_btn_ctxs[btn_idx].step_idx = -1;

            lv_obj_t *btn = lv_list_add_button(lst,
                                k_op_defs[i].symbol, k_op_defs[i].label);
            _lst_style(btn, k_op_defs[i].color, true);
            lv_obj_add_event_cb(btn, _wiz_op_btn_cb, LV_EVENT_CLICKED,
                                &priv->sidebar_btn_ctxs[btn_idx]);
            btn_idx++;
        }
        // "Tap camera to begin" hint — only in IDLE (no point placed yet),
        // shown below the op buttons, white, max-width 80 px.
        if (priv->wiz_state == WIZ_IDLE) {
            lv_obj_t *hint = lv_label_create(priv->step_panel);
            lv_label_set_text(hint, "Tap camera to begin");
            lv_obj_set_style_text_color(hint, lv_color_white(), 0);
            lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(hint, 80);
            lv_obj_set_style_pad_top(hint, WIZ_PANEL_PAD, 0);
        }
        return;
    }

    // --- Step list (COLLECTING / READY / CONFIRM / RUNNING) -----------------
    if (priv->wiz_op == PROBE_OP_NONE) return;

    int step_count               = k_op_step_counts[priv->wiz_op];
    const wiz_step_def_t *steps  = k_op_steps[priv->wiz_op];
    bool running = (priv->wiz_state == WIZ_RUNNING || priv->wiz_state == WIZ_CONFIRM);

    for (int i = 0; i < step_count && btn_idx < 8; i++) {
        bool is_run = (steps[i].kind == WIZ_SK_RUN);
        bool done   = (i < priv->wiz_step_idx);
        bool active = (i == priv->wiz_step_idx);

        uint32_t bg =
            running ? (is_run ? WIZ_RUN_COLOR      : WIZ_STEP_DONE_COLOR) :
            is_run  ? WIZ_RUN_COLOR                                        :
            done    ? WIZ_STEP_DONE_COLOR                                   :
            active  ? WIZ_STEP_CURR_COLOR                                   :
                      WIZ_STEP_NEXT_COLOR;

        bool tappable = !running && (done || (active && is_run));

        priv->sidebar_btn_ctxs[btn_idx].priv     = priv;
        priv->sidebar_btn_ctxs[btn_idx].op       = PROBE_OP_NONE;
        priv->sidebar_btn_ctxs[btn_idx].step_idx = (int8_t)i;

        const char *sym = (active && !is_run && !running) ? LV_SYMBOL_RIGHT : NULL;
        lv_obj_t *btn = lv_list_add_button(lst, sym, steps[i].label);
        _lst_style(btn, bg, tappable || (active && !is_run));

        // Blink the active collection step so it's obvious which step is next.
        if (active && !is_run && !running) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, btn);
            lv_anim_set_exec_cb(&a, _step_blink_cb);
            lv_anim_set_values(&a, 0, 255);
            lv_anim_set_duration(&a, 500);
            lv_anim_set_reverse_duration(&a, 500);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
        }

        if (!running) {
            if (is_run && active)
                lv_obj_add_event_cb(btn, _wiz_run_btn_cb, LV_EVENT_CLICKED, priv);
            else if (done)
                lv_obj_add_event_cb(btn, _wiz_step_btn_cb, LV_EVENT_CLICKED,
                                    &priv->sidebar_btn_ctxs[btn_idx]);
        }
        btn_idx++;
    }

    // Reset/abort button — always tappable so the user can cancel at any
    // point including while an operation is running (wiz_reset cancels it).
    if (btn_idx < 8) {
        lv_obj_t *rst = lv_list_add_button(lst, LV_SYMBOL_CLOSE, "Reset");
        _lst_style(rst, WIZ_CANCEL_COLOR, true);
        lv_obj_add_event_cb(rst, _wiz_reset_btn_cb, LV_EVENT_CLICKED, priv);
    }
}

// ---------------------------------------------------------------------------
// Probe wizard — confirmation modal
// ---------------------------------------------------------------------------

static void _wiz_confirm_ok_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;
    lv_obj_t *mbox = priv->wiz_confirm_modal;
    priv->wiz_confirm_modal = NULL;
    /* Use async close: synchronous lv_msgbox_close() deletes the button's
     * ancestor while the click event is still being dispatched, causing
     * a use-after-free and FreeRTOS draw-thread deadlock. */
    if (mbox && lv_obj_is_valid(mbox)) lv_msgbox_close_async(mbox);
    wiz_execute(priv);
}

static void _wiz_confirm_cancel_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;
    lv_obj_t *mbox = priv->wiz_confirm_modal;
    priv->wiz_confirm_modal = NULL;
    if (mbox && lv_obj_is_valid(mbox)) lv_msgbox_close_async(mbox);
    wiz_reset(priv);
}

static void wiz_show_confirm(lv_cam_pos_priv_t *priv)
{
    // Dismiss any prior modal
    if (priv->wiz_confirm_modal && lv_obj_is_valid(priv->wiz_confirm_modal)) {
        lv_msgbox_close(priv->wiz_confirm_modal);
        priv->wiz_confirm_modal = NULL;
    }

    const probe_settings_t *cfg = probe_settings_get();
    char title[64], text[256];

    switch (priv->wiz_op) {
    case PROBE_OP_MOVE_TO:
        snprintf(title, sizeof(title), "Move to position");
        snprintf(text,  sizeof(text),
                 "X: %.3f  Y: %.3f\nRetract to Z = %.1f mm",
                 (double)priv->wiz_pt1.phys_x,
                 (double)priv->wiz_pt1.phys_y,
                 (double)cfg->safe_z);
        break;
    case PROBE_OP_PROBE_Z:
        snprintf(title, sizeof(title), "Probe Z surface");
        snprintf(text,  sizeof(text),
                 "Move to X: %.3f  Y: %.3f\nthen probe Z (max %.1f mm down)",
                 (double)priv->wiz_pt1.phys_x,
                 (double)priv->wiz_pt1.phys_y,
                 (double)cfg->max_z_depth);
        break;
    case PROBE_OP_PROBE_POCKET:
        snprintf(title, sizeof(title), "Probe pocket (inside rect)");
        snprintf(text,  sizeof(text),
                 "A: (%.3f, %.3f)\nB: (%.3f, %.3f)\nZ-ref: (%.3f, %.3f)",
                 (double)priv->wiz_pt1.phys_x,  (double)priv->wiz_pt1.phys_y,
                 (double)priv->wiz_pt2.phys_x,  (double)priv->wiz_pt2.phys_y,
                 (double)priv->wiz_zref.phys_x, (double)priv->wiz_zref.phys_y);
        break;
    case PROBE_OP_PROBE_RECT:
        snprintf(title, sizeof(title), "Probe rectangle (outside)");
        snprintf(text,  sizeof(text),
                 "A: (%.3f, %.3f)\nB: (%.3f, %.3f)\nZ-ref: (%.3f, %.3f)",
                 (double)priv->wiz_pt1.phys_x,  (double)priv->wiz_pt1.phys_y,
                 (double)priv->wiz_pt2.phys_x,  (double)priv->wiz_pt2.phys_y,
                 (double)priv->wiz_zref.phys_x, (double)priv->wiz_zref.phys_y);
        break;
    case PROBE_OP_PROBE_BORE:
        snprintf(title, sizeof(title), "Probe bore (inside circle)");
        snprintf(text,  sizeof(text),
                 "Centre: (%.3f, %.3f)\nEdge:   (%.3f, %.3f)\nZ-ref:  (%.3f, %.3f)",
                 (double)priv->wiz_pt1.phys_x,  (double)priv->wiz_pt1.phys_y,
                 (double)priv->wiz_pt2.phys_x,  (double)priv->wiz_pt2.phys_y,
                 (double)priv->wiz_zref.phys_x, (double)priv->wiz_zref.phys_y);
        break;
    case PROBE_OP_PROBE_BOSS:
        snprintf(title, sizeof(title), "Probe boss (outside circle)");
        snprintf(text,  sizeof(text),
                 "Centre: (%.3f, %.3f)\nEdge:   (%.3f, %.3f)\nZ-ref:  (%.3f, %.3f)",
                 (double)priv->wiz_pt1.phys_x,  (double)priv->wiz_pt1.phys_y,
                 (double)priv->wiz_pt2.phys_x,  (double)priv->wiz_pt2.phys_y,
                 (double)priv->wiz_zref.phys_x, (double)priv->wiz_zref.phys_y);
        break;
    default:
        return;
    }

    priv->wiz_state = WIZ_CONFIRM;

    lv_obj_t *mbox = lv_msgbox_create(lv_screen_active());
    lv_msgbox_add_title(mbox, title);
    lv_msgbox_add_text(mbox, text);

    lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "Execute");
    lv_obj_add_event_cb(ok,   _wiz_confirm_ok_cb,     LV_EVENT_CLICKED, priv);
    lv_obj_t *ca = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(ca,   _wiz_confirm_cancel_cb, LV_EVENT_CLICKED, priv);

    lv_obj_center(mbox);
    priv->wiz_confirm_modal = mbox;
    LOGD(TAG, "Wizard confirm modal shown for op=%d", priv->wiz_op);
}

// ---------------------------------------------------------------------------
// Probe wizard — execute selected operation
// ---------------------------------------------------------------------------

static void wiz_execute(lv_cam_pos_priv_t *priv)
{
    if (!priv->probe_cbs_set) {
        LOGE(TAG, "Probe callbacks not set — cannot execute op=%d", priv->wiz_op);
        wiz_reset(priv);
        return;
    }

    const probe_settings_t *cfg = probe_settings_get();
    bool ok = false;

    // Re-register event callbacks (they were nulled by wiz_reset in cancel path,
    // but here we are in the confirmed-execute path, so probe_ctx is fresh).
    _wiz_reinit_probe_ctx(priv);

    switch (priv->wiz_op) {
    case PROBE_OP_MOVE_TO: {
        probe_api_move_to_params_t p = {
            .target_xy = { priv->wiz_pt1.phys_x, priv->wiz_pt1.phys_y },
            .safe_z    = cfg->safe_z,
        };
        ok = probe_api_move_to(&priv->probe_ctx, &p);
        break;
    }
    case PROBE_OP_PROBE_Z: {
        probe_api_probe_z_params_t p = {
            .target_xy = { priv->wiz_pt1.phys_x, priv->wiz_pt1.phys_y },
            .safe_z    = cfg->safe_z,
            .max_depth = cfg->max_z_depth,
        };
        ok = probe_api_probe_z(&priv->probe_ctx, &p);
        break;
    }
    case PROBE_OP_PROBE_POCKET: {
        probe_api_probe_pocket_params_t p = {
            .corner_a       = { priv->wiz_pt1.phys_x,  priv->wiz_pt1.phys_y  },
            .corner_b       = { priv->wiz_pt2.phys_x,  priv->wiz_pt2.phys_y  },
            .z_probe_xy     = { priv->wiz_zref.phys_x, priv->wiz_zref.phys_y },
            .safe_z         = cfg->safe_z,
            .max_z_depth    = cfg->max_z_depth,
            .xy_probe_depth = cfg->xy_probe_depth,
        };
        ok = probe_api_probe_pocket(&priv->probe_ctx, &p);
        break;
    }
    case PROBE_OP_PROBE_RECT: {
        probe_api_probe_rect_params_t p = {
            .corner_a       = { priv->wiz_pt1.phys_x,  priv->wiz_pt1.phys_y  },
            .corner_b       = { priv->wiz_pt2.phys_x,  priv->wiz_pt2.phys_y  },
            .z_probe_xy     = { priv->wiz_zref.phys_x, priv->wiz_zref.phys_y },
            .safe_z         = cfg->safe_z,
            .max_z_depth    = cfg->max_z_depth,
            .xy_probe_depth = cfg->xy_probe_depth,
        };
        ok = probe_api_probe_rect(&priv->probe_ctx, &p);
        break;
    }
    case PROBE_OP_PROBE_BORE: {
        probe_api_probe_bore_params_t p = {
            .center         = { priv->wiz_pt1.phys_x,  priv->wiz_pt1.phys_y  },
            .edge           = { priv->wiz_pt2.phys_x,  priv->wiz_pt2.phys_y  },
            .z_probe_xy     = { priv->wiz_zref.phys_x, priv->wiz_zref.phys_y },
            .safe_z         = cfg->safe_z,
            .max_z_depth    = cfg->max_z_depth,
            .xy_probe_depth = cfg->xy_probe_depth,
        };
        ok = probe_api_probe_bore(&priv->probe_ctx, &p);
        break;
    }
    case PROBE_OP_PROBE_BOSS: {
        probe_api_probe_boss_params_t p = {
            .center         = { priv->wiz_pt1.phys_x,  priv->wiz_pt1.phys_y  },
            .edge           = { priv->wiz_pt2.phys_x,  priv->wiz_pt2.phys_y  },
            .z_probe_xy     = { priv->wiz_zref.phys_x, priv->wiz_zref.phys_y },
            .safe_z         = cfg->safe_z,
            .max_z_depth    = cfg->max_z_depth,
            .xy_probe_depth = cfg->xy_probe_depth,
        };
        ok = probe_api_probe_boss(&priv->probe_ctx, &p);
        break;
    }
    default:
        wiz_reset(priv);
        return;
    }

    if (!ok) {
        LOGE(TAG, "probe_api returned false for op=%d (already running?)",
             priv->wiz_op);
        wiz_reset(priv);
        return;
    }

    priv->wiz_state = WIZ_RUNNING;
    sidebar_rebuild(priv);
    lv_obj_invalidate(priv->overlay);
    LOGI(TAG, "Wizard executing op=%d", priv->wiz_op);
}

// ---------------------------------------------------------------------------
// Probe wizard — click handler
// ---------------------------------------------------------------------------

static void wiz_handle_click(lv_cam_pos_priv_t *priv, lv_point_t scr_pt)
{
    // -----------------------------------------------------------------------
    // Move-point mode (set when the user taps near an existing point).
    // The next tap repositions that specific point and exits the mode,
    // resuming the wizard at whatever step was already active.
    // -----------------------------------------------------------------------
    if (priv->wiz_move_pt_active && priv->wiz_move_pt_which >= 0) {
        int16_t img_x = 0, img_y = 0;
        bool ok = pixel_to_image_coords(priv, scr_pt, &img_x, &img_y);
        lv_cam_pos_point_t new_pt = ok ? make_point(priv, img_x, img_y)
                                       : (lv_cam_pos_point_t){0};
        switch (priv->wiz_move_pt_which) {
        case 0: priv->wiz_pt1  = new_pt; break;
        case 1: priv->wiz_pt2  = new_pt; break;
        case 2: priv->wiz_zref = new_pt; break;
        default: break;
        }
        wiz_cancel_move_pt(priv);
        sidebar_update_coords(priv);
        lv_obj_invalidate(priv->overlay);
        return;
    }

    // -----------------------------------------------------------------------
    // Extended hit detection: if the tap is within (POINT_RADIUS + 5) px of
    // any already-placed wizard point, enter move-point mode instead of
    // placing a new point in the normal wizard flow.
    // -----------------------------------------------------------------------
    if (priv->wiz_state >= WIZ_OP_SELECT) {
        typedef struct { lv_cam_pos_point_t *pt; bool set; } pt_hit_t;
        pt_hit_t pts[3] = {
            { &priv->wiz_pt1,  priv->wiz_pt1_set  },
            { &priv->wiz_pt2,  priv->wiz_pt2_set  },
            { &priv->wiz_zref, priv->wiz_zref_set },
        };
        const int32_t HIT_R = POINT_RADIUS + 5;
        for (int i = 0; i < 3; i++) {
            if (!pts[i].set) continue;
            int32_t sx, sy;
            if (!image_to_screen_coords(priv,
                    pts[i].pt->px_x, pts[i].pt->px_y, &sx, &sy)) continue;
            int32_t dx = sx - scr_pt.x, dy = sy - scr_pt.y;
            if (dx * dx + dy * dy <= HIT_R * HIT_R) {
                wiz_start_move_pt(priv, (int8_t)i);
                return;
            }
        }
    }

    switch (priv->wiz_state) {

    case WIZ_IDLE: {
        // First tap → record pt1, advance to op-selection.
        // If image coords are unavailable (camera offline / no grid) we store a
        // zeroed point; the operation will use the machine's current position.
        int16_t img_x = 0, img_y = 0;
        bool has_grid = pixel_to_image_coords(priv, scr_pt, &img_x, &img_y);
        priv->wiz_pt1      = has_grid ? make_point(priv, img_x, img_y)
                                      : (lv_cam_pos_point_t){0};
        priv->wiz_scr_tap  = scr_pt;
        priv->wiz_pt1_set  = true;
        priv->wiz_step_idx = 0;   // pt1 step just completed; op still pending
        priv->wiz_state    = WIZ_OP_SELECT;
        sidebar_rebuild(priv);
        sidebar_update_coords(priv);
        lv_obj_invalidate(priv->overlay);
        break;
    }

    case WIZ_COLLECTING: {
        // Determine what kind of point the current step expects.
        const wiz_step_def_t *steps = k_op_steps[priv->wiz_op];
        int n = k_op_step_counts[priv->wiz_op];
        if (priv->wiz_step_idx < 0 || priv->wiz_step_idx >= n) break;

        wiz_step_kind_t kind = steps[priv->wiz_step_idx].kind;
        int16_t img_x = 0, img_y = 0;
        bool has_img_coords = pixel_to_image_coords(priv, scr_pt, &img_x, &img_y);

        // Allow wizard collection even when image/grid coords are unavailable
        // (camera offline / connecting).  In that case store a zeroed point
        // and record the raw screen tap for pt1 so the UI can still show a
        // meaningful marker and advance the flow.
        if (kind == WIZ_SK_PT1) {
            if (has_img_coords) {
                priv->wiz_pt1 = make_point(priv, img_x, img_y);
            } else {
                priv->wiz_pt1 = (lv_cam_pos_point_t){0};
            }
            priv->wiz_pt1_set = true;
            priv->wiz_scr_tap = scr_pt;
        } else if (kind == WIZ_SK_PT2) {
            if (has_img_coords) {
                priv->wiz_pt2 = make_point(priv, img_x, img_y);
            } else {
                priv->wiz_pt2 = (lv_cam_pos_point_t){0};
            }
            priv->wiz_pt2_set = true;
        } else if (kind == WIZ_SK_ZREF) {
            if (has_img_coords) {
                priv->wiz_zref = make_point(priv, img_x, img_y);
            } else {
                priv->wiz_zref = (lv_cam_pos_point_t){0};
            }
            priv->wiz_zref_set = true;
        }

        priv->wiz_step_idx++;

        // Zoom trigger: activate (or re-activate) zoom at the configured point.
#if CAM_POS_ZOOM_AFTER_PT2
        // Trigger after pt2 has been collected (kind == WIZ_SK_PT2 was just done).
        if (kind == WIZ_SK_PT2) {
            wiz_deactivate_zoom(priv);  // clear any prior zoom before re-zooming
            wiz_activate_zoom(priv);
        }
#else
        // Trigger after Z-surf has been collected (kind == WIZ_SK_ZREF was just done).
        if (kind == WIZ_SK_ZREF) {
            wiz_deactivate_zoom(priv);
            wiz_activate_zoom(priv);
        }
#endif

        // If the next step is RUN (last step in sequence), transition to READY.
        if (priv->wiz_step_idx == n - 1)
            priv->wiz_state = WIZ_READY;

        sidebar_rebuild(priv);
        sidebar_update_coords(priv);
        lv_obj_invalidate(priv->overlay);
        break;
    }

    case WIZ_READY: {
        // Move the nearest collected point to the tapped position.
        int16_t img_x, img_y;
        if (!pixel_to_image_coords(priv, scr_pt, &img_x, &img_y)) return;

        // Find the closest set point (in screen space).
        int32_t best_d2 = INT32_MAX;
        int     best_pt = -1;
        typedef struct { lv_cam_pos_point_t *pt; bool set; } pt_entry_t;
        pt_entry_t pts[3] = {
            { &priv->wiz_pt1,  priv->wiz_pt1_set  },
            { &priv->wiz_pt2,  priv->wiz_pt2_set  },
            { &priv->wiz_zref, priv->wiz_zref_set },
        };
        for (int i = 0; i < 3; i++) {
            if (!pts[i].set) continue;
            int32_t sx, sy;
            if (!image_to_screen_coords(priv, pts[i].pt->px_x, pts[i].pt->px_y, &sx, &sy))
                continue;
            int32_t dx = sx - scr_pt.x, dy = sy - scr_pt.y;
            int32_t d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best_pt = i; }
        }

        if (best_pt >= 0)
            *pts[best_pt].pt = make_point(priv, img_x, img_y);

        sidebar_update_coords(priv);
        lv_obj_invalidate(priv->overlay);
        break;
    }

    case WIZ_OP_SELECT:
    case WIZ_CONFIRM:
    case WIZ_RUNNING:
        // UI is managed via sidebar/modal — ignore bare overlay taps.
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Overlay draw event — renders shapes
// ---------------------------------------------------------------------------

static void overlay_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;

    lv_layer_t *layer = lv_event_get_layer(e);
    if (!layer) return;

    (void)obj;

    // If the camera receiver isn't attached or hasn't produced a frame yet,
    // skip interactive visuals.  The status_label widget shows the connecting
    // message (managed by grid_dirty_timer_cb).
    if (!priv->receiver || !cam_receiver_has_frame(priv->receiver)) {
        return;
    }

    // --- Axis-aligned physical grid (idealized straight lines). ---
    // Previously disabled due to DRAM/draw-task heap exhaustion.  With the
    // increased LV_MEM_SIZE (105 KB) there is sufficient headroom for the
    // extra lv_draw_task_t allocations.  draw_calib_grid (actual calibration
    // offsets) remains disabled — see its doc-comment above draw_axis_grid.
    draw_axis_grid(layer, priv);

    // --- Point mode ---
    if (priv->mode == LV_CAM_POS_MODE_POINT && priv->point_valid) {
        int32_t sx, sy;
        if (image_to_screen_coords(priv, priv->point.px_x, priv->point.px_y,
                                    &sx, &sy)) {
            draw_point(layer, sx, sy);
        }
    }

    // --- Rectangle mode ---
    if (priv->mode == LV_CAM_POS_MODE_RECTANGLE) {
        if (priv->rect_valid || priv->sel_state == SEL_FIRST_POINT) {
            int32_t sx1, sy1, sx2, sy2;
            int16_t c0x = priv->rect.corners[0].px_x;
            int16_t c0y = priv->rect.corners[0].px_y;
            int16_t c1x, c1y;

            if (priv->rect_valid) {
                c1x = priv->rect.corners[1].px_x;
                c1y = priv->rect.corners[1].px_y;
            } else if (priv->preview_active) {
                c1x = priv->preview_px_x;
                c1y = priv->preview_px_y;
            } else {
                // Only first corner placed — draw just the point
                if (image_to_screen_coords(priv, c0x, c0y, &sx1, &sy1))
                    draw_point(layer, sx1, sy1);
                return;
            }

            if (image_to_screen_coords(priv, c0x, c0y, &sx1, &sy1) &&
                image_to_screen_coords(priv, c1x, c1y, &sx2, &sy2)) {
                draw_rectangle(layer, priv, sx1, sy1, sx2, sy2);
            }
        }
    }

    // --- Circle mode ---
    if (priv->mode == LV_CAM_POS_MODE_CIRCLE) {
        if (priv->circle_valid || priv->sel_state == SEL_FIRST_POINT) {
            int32_t cx_s, cy_s, ex_s, ey_s;
            int16_t c_x = priv->circle.center.px_x;
            int16_t c_y = priv->circle.center.px_y;
            int16_t e_x, e_y;

            if (priv->circle_valid) {
                e_x = priv->circle.edge.px_x;
                e_y = priv->circle.edge.px_y;
            } else if (priv->preview_active) {
                e_x = priv->preview_px_x;
                e_y = priv->preview_px_y;
            } else {
                // Only centre placed
                if (image_to_screen_coords(priv, c_x, c_y, &cx_s, &cy_s))
                    draw_point(layer, cx_s, cy_s);
                return;
            }

            if (image_to_screen_coords(priv, c_x, c_y, &cx_s, &cy_s) &&
                image_to_screen_coords(priv, e_x, e_y, &ex_s, &ey_s)) {
                draw_circle(layer, priv, cx_s, cy_s, ex_s, ey_s);
            }
        }
    }

    // --- Wizard overlay markers (mode == NONE, first tap onwards) ---
    // Shown from WIZ_OP_SELECT upwards so the tap point is visible immediately.
    // Symbol badges appear once the user picks an operation:
    //   pt1 → GPS (Move), DOWNLOAD (ProbeZ), PLUS (corner/center of shapes)
    //   pt2 → POWER (second corner / circumference point)
    //   zref → DOWNLOAD
    if (priv->mode == LV_CAM_POS_MODE_NONE && priv->wiz_state >= WIZ_OP_SELECT) {
        bool is_rect_op = (priv->wiz_op == PROBE_OP_PROBE_POCKET ||
                           priv->wiz_op == PROBE_OP_PROBE_RECT);
        bool is_circ_op = (priv->wiz_op == PROBE_OP_PROBE_BORE  ||
                           priv->wiz_op == PROBE_OP_PROBE_BOSS);

        // pt1 symbol: none until op selected, then depends on op
        const char *sym1 = NULL;
        if (priv->wiz_state > WIZ_OP_SELECT) {
            switch (priv->wiz_op) {
                case PROBE_OP_MOVE_TO:  sym1 = LV_SYMBOL_GPS;      break;
                case PROBE_OP_PROBE_Z:  sym1 = LV_SYMBOL_DOWNLOAD; break;
                default:                sym1 = LV_SYMBOL_PLUS;     break; // corner/center
            }
        }

        // Resolve pt1 to screen coords; fall back to the raw screen tap if
        // image coords are unavailable (camera offline, no grid).
        int32_t sx1 = priv->wiz_scr_tap.x, sy1 = priv->wiz_scr_tap.y;
        {
            int32_t mx, my;
            if (image_to_screen_coords(priv,
                    priv->wiz_pt1.px_x, priv->wiz_pt1.px_y, &mx, &my)) {
                sx1 = mx; sy1 = my;
            }
        }
        draw_wiz_marker(layer, sx1, sy1, sym1);
        // Blink halo when this point is selected for repositioning
        if (priv->wiz_move_pt_active && priv->wiz_move_pt_which == 0 &&
                priv->wiz_move_pt_blink_on)
            draw_move_pt_halo(layer, sx1, sy1);

        // pt2 marker + shape overlay (shown once wiz_pt2 is collected)
        if (priv->wiz_pt2_set) {
            int32_t sx2, sy2;
            if (image_to_screen_coords(priv,
                    priv->wiz_pt2.px_x, priv->wiz_pt2.px_y, &sx2, &sy2)) {
                // Shape outline for context
                if (is_rect_op)         draw_rectangle(layer, priv, sx1, sy1, sx2, sy2);
                else if (is_circ_op)    draw_circle(layer, priv, sx1, sy1, sx2, sy2);
                // Second point: POWER symbol (second corner / circumference)
                bool rect_mode = priv->wiz_op == PROBE_OP_PROBE_RECT || priv->wiz_op == PROBE_OP_PROBE_POCKET;
                draw_wiz_marker(layer, sx2, sy2, (rect_mode ? LV_SYMBOL_PLUS : LV_SYMBOL_POWER));
                if (priv->wiz_move_pt_active && priv->wiz_move_pt_which == 1 &&
                        priv->wiz_move_pt_blink_on)
                    draw_move_pt_halo(layer, sx2, sy2);
            }
        }

        // Z-ref marker: DOWNLOAD symbol
        if (priv->wiz_zref_set &&
                (priv->wiz_zref.px_x != 0 || priv->wiz_zref.px_y != 0)) {
            int32_t szx, szy;
            if (image_to_screen_coords(priv,
                    priv->wiz_zref.px_x, priv->wiz_zref.px_y, &szx, &szy)) {
                draw_wiz_marker(layer, szx, szy, LV_SYMBOL_DOWNLOAD);
                if (priv->wiz_move_pt_active && priv->wiz_move_pt_which == 2 &&
                        priv->wiz_move_pt_blink_on)
                    draw_move_pt_halo(layer, szx, szy);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Touch handlers
// ---------------------------------------------------------------------------

static void overlay_click_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;

        /* If camera not ready, ignore non-wizard clicks to disable interactive UI
        * for now.  Allow wizard-mode taps (mode == LV_CAM_POS_MODE_NONE) even
        * if no frame is available so the user can start the probe flow. */
        if ((!priv->receiver || !cam_receiver_has_frame(priv->receiver)) &&
            priv->mode != LV_CAM_POS_MODE_NONE) return;

    // Ignore clicks that originated from a child widget (e.g. zoom badge).
    // Without this guard the click would bubble up and wiz_handle_click would
    // place a spurious wizard point at the badge's screen position.
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) {
        lv_event_stop_bubbling(e);
        return;
    }

    lv_point_t scr_pt;
    lv_indev_get_point(lv_indev_active(), &scr_pt);

    // When mode is NONE the overlay drives the probe wizard instead of
    // the shape-selection logic below.  Stop bubbling so parent tileview
    // does not start swiping when the user is interacting with points.
    if (priv->mode == LV_CAM_POS_MODE_NONE) {
        lv_event_stop_bubbling(e);
        wiz_handle_click(priv, scr_pt);
        return;
    }

    int16_t img_x, img_y;
    if (!pixel_to_image_coords(priv, scr_pt, &img_x, &img_y)) return;

    LOGD(TAG, "Click at image (%d, %d)", img_x, img_y);

    /* Capture clicks for our overlay modes so parent tileview doesn't
     * start scrolling/swiping while the user is placing/adjusting points. */
    lv_event_stop_bubbling(e);

    switch (priv->mode) {
    case LV_CAM_POS_MODE_POINT: {
        priv->point       = make_point(priv, img_x, img_y);
        priv->point_valid = true;
        priv->sel_state   = SEL_COMPLETE;
        lv_obj_invalidate(priv->overlay);

        if (priv->point_cb)
            priv->point_cb(priv->root, &priv->point, priv->point_cb_user);
        break;
    }
    case LV_CAM_POS_MODE_RECTANGLE: {
        if (priv->sel_state == SEL_NONE || priv->sel_state == SEL_COMPLETE) {
            // First corner
            priv->rect.corners[0] = make_point(priv, img_x, img_y);
            priv->rect_valid       = false;
            priv->sel_state        = SEL_FIRST_POINT;
            priv->preview_active   = false;
        } else {
            // Second corner → complete
            priv->rect.corners[1] = make_point(priv, img_x, img_y);
            priv->rect.width_mm  = fabsf(priv->rect.corners[1].phys_x -
                                          priv->rect.corners[0].phys_x);
            priv->rect.height_mm = fabsf(priv->rect.corners[1].phys_y -
                                          priv->rect.corners[0].phys_y);
            priv->rect_valid      = true;
            priv->sel_state       = SEL_COMPLETE;
            priv->preview_active  = false;

            if (priv->rect_cb)
                priv->rect_cb(priv->root, &priv->rect, priv->rect_cb_user);
        }
        lv_obj_invalidate(priv->overlay);
        break;
    }
    case LV_CAM_POS_MODE_CIRCLE: {
        if (priv->sel_state == SEL_NONE || priv->sel_state == SEL_COMPLETE) {
            // Centre
            priv->circle.center   = make_point(priv, img_x, img_y);
            priv->circle_valid    = false;
            priv->sel_state       = SEL_FIRST_POINT;
            priv->preview_active  = false;
        } else {
            // Edge point → compute radius
            priv->circle.edge     = make_point(priv, img_x, img_y);

            float dx_px = (float)(img_x - priv->circle.center.px_x);
            float dy_px = (float)(img_y - priv->circle.center.px_y);
            priv->circle.radius_px = sqrtf(dx_px * dx_px + dy_px * dy_px);

            if (priv->circle.center.has_physical && priv->circle.edge.has_physical) {
                float dx_mm = priv->circle.edge.phys_x - priv->circle.center.phys_x;
                float dy_mm = priv->circle.edge.phys_y - priv->circle.center.phys_y;
                priv->circle.radius_mm = sqrtf(dx_mm * dx_mm + dy_mm * dy_mm);
            } else {
                priv->circle.radius_mm = 0;
            }

            priv->circle_valid   = true;
            priv->sel_state      = SEL_COMPLETE;
            priv->preview_active = false;

            if (priv->circle_cb)
                priv->circle_cb(priv->root, &priv->circle, priv->circle_cb_user);
        }
        lv_obj_invalidate(priv->overlay);
        break;
    }
    default:
        break;
    }
}

/// While dragging / pressing anywhere on the overlay:
/// • shape selection modes: update the rubber-band / preview for the 2nd point.
/// • wizard READY state: move the nearest collected point in real-time (drag to adjust).
static void overlay_press_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;

        /* If camera not ready, ignore non-wizard presses to disable interactive UI
        * for now.  Allow wizard-mode presses (mode == LV_CAM_POS_MODE_NONE) so
        * dragging to adjust points still works while offline. */
        if ((!priv->receiver || !cam_receiver_has_frame(priv->receiver)) &&
            priv->mode != LV_CAM_POS_MODE_NONE) return;

    // Same child-propagation guard as overlay_click_cb.
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) {
        lv_event_stop_bubbling(e);
        return;
    }

    lv_point_t scr_pt;
    lv_indev_get_point(lv_indev_active(), &scr_pt);

    // Wizard point-drag in READY state: move the nearest collected point.
    // NOTE: do NOT call wiz_handle_click here — it calls sidebar_update_coords
    // which does lv_obj_clean + lv_label_create.  That LVGL tree mutation
    // fires on every indev tick during a drag (PRESSING) and crashes
    // lv_obj_class_create_obj because the object tree is modified while
    // an event is still being dispatched.  Instead we inline the minimal
    // drag logic: update the point struct, invalidate for visual feedback.
    // sidebar_update_coords fires on the final CLICKED event (overlay_click_cb
    // → wiz_handle_click → sidebar_update_coords) when the finger lifts.
    if (priv->mode == LV_CAM_POS_MODE_NONE && priv->wiz_state == WIZ_READY) {
        lv_event_stop_bubbling(e);
        // Move-point mode is active: a tap on the overlay is being held for
        // the confirm click.  Suppress drag so we don't accidentally displace
        // a different point while waiting for the lift-up CLICKED event.
        if (priv->wiz_move_pt_active) return;
        int16_t img_x, img_y;
        if (!pixel_to_image_coords(priv, scr_pt, &img_x, &img_y)) return;
        // Find and move nearest collected point — same logic as wiz_handle_click.
        int32_t best_d2 = INT32_MAX;
        int     best_pt = -1;
        typedef struct { lv_cam_pos_point_t *pt; bool set; } pt_drag_t;
        pt_drag_t pts[3] = {
            { &priv->wiz_pt1,  priv->wiz_pt1_set  },
            { &priv->wiz_pt2,  priv->wiz_pt2_set  },
            { &priv->wiz_zref, priv->wiz_zref_set },
        };
        for (int i = 0; i < 3; i++) {
            if (!pts[i].set) continue;
            int32_t sx, sy;
            if (!image_to_screen_coords(priv, pts[i].pt->px_x, pts[i].pt->px_y,
                                         &sx, &sy)) continue;
            int32_t dx = sx - scr_pt.x, dy = sy - scr_pt.y;
            int32_t d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best_pt = i; }
        }
        if (best_pt >= 0)
            *pts[best_pt].pt = make_point(priv, img_x, img_y);
        lv_obj_invalidate(priv->overlay);  // redraw only — no LVGL tree changes
        return;
    }

    if (priv->sel_state != SEL_FIRST_POINT) return;
    if (priv->mode != LV_CAM_POS_MODE_RECTANGLE &&
        priv->mode != LV_CAM_POS_MODE_CIRCLE) return;

    int16_t img_x, img_y;
    if (!pixel_to_image_coords(priv, scr_pt, &img_x, &img_y)) return;

    /* User is actively dragging the second point — capture input so the
     * parent tileview does not start a swipe gesture while dragging. */
    lv_event_stop_bubbling(e);
    priv->preview_active = true;
    priv->preview_px_x   = img_x;
    priv->preview_px_y   = img_y;
    lv_obj_invalidate(priv->overlay);
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

// Async trampoline: runs on the LVGL task after lv_async_call() schedules it.
// on_grid_update is called from the cam-receiver transport-RX FreeRTOS task.
// It MUST NOT call any LVGL API (including lv_async_call / lv_timer_create):
// LVGL has no OS-level mutex in this build, so mutating the timer linked list
// from a non-LVGL task races with lv_task_handler() and causes a NULL-deref
// crash inside lv_ll_ins_head (backtrace: lv_async.c:54).
//
// Safe pattern: set a volatile bool flag here; the widget-owned lv_timer
// (grid_dirty_timer_cb, 50 ms, LVGL task) checks and clears it.
static void on_grid_update(const cam_grid_info_t *grid, void *user_data)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)user_data;
    if (!priv) return;
    (void)grid;
    priv->grid_dirty = true;   // atomic on Xtensa (single-byte aligned store)
}

static void grid_dirty_timer_cb(lv_timer_t *t)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_timer_get_user_data(t);
    if (!priv) return;

    // Auto-manage the local camera-connection status label.
    // Runs every 50 ms regardless of grid_dirty so the label appears / hides
    // promptly as the camera comes online.
    if (priv->status_label && !priv->status_locked) {
        bool has_frame = priv->receiver && cam_receiver_has_frame(priv->receiver);
        if (has_frame) {
            if (!lv_obj_has_flag(priv->status_label, LV_OBJ_FLAG_HIDDEN))
                lv_obj_add_flag(priv->status_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (lv_obj_has_flag(priv->status_label, LV_OBJ_FLAG_HIDDEN))
                lv_obj_clear_flag(priv->status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!priv->grid_dirty) return;
    priv->grid_dirty = false;

    // If zoom was activated and we were waiting for the first post-zoom grid
    // update (which arrives with the new keyframe), now apply the px remap.
    // This runs in the LVGL task so LVGL and float writes are safe.
    if (priv->wiz_zoom_remap_pending && priv->wiz_zoom_active) {
        priv->wiz_zoom_remap_pending = false;
        if (priv->stream && priv->wiz_zoom_w && priv->wiz_zoom_h) {
            uint16_t out_w = 0, out_h = 0;
            lv_cam_stream_get_image_size(priv->stream, &out_w, &out_h);
            if (out_w && out_h) {
                float left = priv->wiz_zoom_cx - priv->wiz_zoom_w * 0.5f;
                float top  = priv->wiz_zoom_cy - priv->wiz_zoom_h * 0.5f;
                _wiz_zoom_remap_pt(&priv->wiz_pt1,  priv->wiz_pt1_set,  left, top,
                                    out_w, out_h, priv->wiz_zoom_w, priv->wiz_zoom_h, true);
                _wiz_zoom_remap_pt(&priv->wiz_pt2,  priv->wiz_pt2_set,  left, top,
                                    out_w, out_h, priv->wiz_zoom_w, priv->wiz_zoom_h, true);
                _wiz_zoom_remap_pt(&priv->wiz_zref, priv->wiz_zref_set, left, top,
                                    out_w, out_h, priv->wiz_zoom_w, priv->wiz_zoom_h, true);
                LOGD(TAG, "Zoom remap applied after keyframe (left=%.1f top=%.1f out=%ux%u crop=%ux%u)",
                     (double)left, (double)top, out_w, out_h,
                     priv->wiz_zoom_w, priv->wiz_zoom_h);
            }
        }
    }

    if (priv->overlay && lv_obj_is_valid(priv->overlay)) {
        lv_obj_invalidate(priv->overlay);
    }
}

static void on_delete(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;

    // Stop the grid-dirty poll timer before deregistering the callback so
    // there is no window where grid_dirty_timer_cb runs after priv is freed.
    if (priv->grid_timer) {
        lv_timer_delete(priv->grid_timer);
        priv->grid_timer = NULL;
    }

    // Deregister grid callback so the receiver won't call into freed memory.
    if (priv->receiver) {
        cam_receiver_remove_grid_cb(priv->receiver, on_grid_update);
    }

    // Tear down wizard UI and cancel any running probe op.
    // wiz_reset nulls the probe event callbacks before cancelling so that
    // probe_api_cancel does not fire into already-freed memory.
    wiz_reset(priv);

    // Ensure sidebar and coord panel children are deleted now so no button
    // event callbacks remain pointing at `priv` after we free it.  This is
    // necessary because the component may be destroyed and recreated when
    // tab-view pages are deferred; lingering callbacks can call into freed
    // `priv` and cause incorrect wizard behaviour.
    if (priv->step_panel && lv_obj_is_valid(priv->step_panel)) lv_obj_clean(priv->step_panel);
    if (priv->coord_panel && lv_obj_is_valid(priv->coord_panel)) lv_obj_clean(priv->coord_panel);
    if (priv->right_panel && lv_obj_is_valid(priv->right_panel)) lv_obj_clean(priv->right_panel);

    // Remove all callbacks from the overlay that hold `priv` as raw user_data.
    if (priv->overlay && lv_obj_is_valid(priv->overlay)) {
        lv_obj_remove_event_cb_with_user_data(priv->overlay, overlay_draw_cb,  priv);
        lv_obj_remove_event_cb_with_user_data(priv->overlay, overlay_click_cb, priv);
        lv_obj_remove_event_cb_with_user_data(priv->overlay, overlay_press_cb, priv);
    }

    free(priv);
    lv_obj_set_user_data(obj, NULL);
}

// ---------------------------------------------------------------------------
// Public API — lifecycle
// ---------------------------------------------------------------------------

lv_obj_t *lv_cam_positioning_create(lv_obj_t *parent)
{
    lv_cam_pos_priv_t *priv = calloc(1, sizeof(*priv));
    if (!priv) return NULL;
    priv->wiz_drag_pt       = -1;  // calloc gives 0, which must not default to "pt1"
    priv->wiz_move_pt_which = -1;  // same: 0 maps to pt1, must default to none

    // Root container — fills parent, flex row: [cam_area | right_panel]
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_layout(root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_user_data(root, priv);
    lv_obj_add_event_cb(root, on_delete, LV_EVENT_DELETE, NULL);
    priv->root = root;

    // -----------------------------------------------------------------------
    // Camera area — grows to fill all space left of the sidebar
    // -----------------------------------------------------------------------
    lv_obj_t *cam_area = lv_obj_create(root);
    lv_obj_remove_style_all(cam_area);
    lv_obj_set_flex_grow(cam_area, 1);
    lv_obj_set_height(cam_area, LV_PCT(100));
    lv_obj_clear_flag(cam_area, LV_OBJ_FLAG_SCROLLABLE);
    // No padding — the stream fills the area edge-to-edge with no black bars.

    // Camera stream — fills cam_area; inner image uses CONTAIN for aspect ratio
    lv_obj_t *stream = lv_cam_stream_create(cam_area);
    lv_obj_set_size(stream, LV_PCT(100), LV_PCT(100));
    lv_obj_set_align(stream, LV_ALIGN_CENTER);
    // No border, no padding — transparent container, image fills area cleanly.

    lv_obj_t *img_obj = lv_cam_stream_get_image_obj(stream);
    if (img_obj)
        lv_image_set_inner_align(img_obj, LV_IMAGE_ALIGN_CONTAIN);

    priv->stream = stream;

    // Propagate default receiver if already auto-attached
    cam_receiver_t *default_recv = lv_cam_stream_get_receiver(stream);
    if (default_recv)
        priv->receiver = default_recv;

    // Transparent overlay — same size as cam_area, above stream for events/draw
    lv_obj_t *overlay = lv_obj_create(cam_area);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_align(overlay, LV_ALIGN_CENTER);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(overlay, overlay_draw_cb,
                         LV_EVENT_DRAW_MAIN_END, priv);
    lv_obj_add_event_cb(overlay, overlay_click_cb,
                         LV_EVENT_CLICKED, priv);
    lv_obj_add_event_cb(overlay, overlay_press_cb,
                         LV_EVENT_PRESSING, priv);
    priv->overlay = overlay;

    // -------------------------------------------------------------------------
    // Local camera-connection status label.
    // Created as a direct child of the overlay so it renders above the stream
    // and shares the same event-clipping area.
    // -------------------------------------------------------------------------
    lv_obj_t *status_lbl = lv_label_create(overlay);
    lv_obj_remove_style_all(status_lbl);
    lv_label_set_text(status_lbl, "Connecting to camera...");
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_WRAP);
    /* Style: white text on a semi-transparent dark pill */
    lv_obj_set_style_text_color(status_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_lbl, &font_kode_20, 0);
    lv_obj_set_style_text_align(status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(status_lbl, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(status_lbl, LV_OPA_60, 0);
    lv_obj_set_style_radius(status_lbl, 8, 0);
    lv_obj_set_style_pad_hor(status_lbl, 16, 0);
    lv_obj_set_style_pad_ver(status_lbl, 8, 0);
    lv_obj_set_width(status_lbl, LV_PCT(80));
    lv_obj_set_height(status_lbl, LV_SIZE_CONTENT);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 0);
    /* Start visible — the timer will hide it once a frame arrives. */
    priv->status_label  = status_lbl;
    priv->status_locked = false;

    // Register grid-update callback (overlay ptr must be set first)
    if (priv->receiver)
        cam_receiver_add_grid_cb(priv->receiver, on_grid_update, priv);

    // Create the grid-dirty poll timer.  Runs in the LVGL task every 50 ms;
    // calls lv_obj_invalidate only when on_grid_update set the flag.
    priv->grid_timer = lv_timer_create(grid_dirty_timer_cb, 50, priv);
    if (priv->grid_timer) {
        lv_timer_set_repeat_count(priv->grid_timer, -1); // repeat forever
    }

    // -----------------------------------------------------------------------
    // Right sidebar — fixed SIDEBAR_W width, flex column
    // -----------------------------------------------------------------------
    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, SIDEBAR_W, LV_PCT(100));
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111827), 0);  // slate-900
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, WIZ_PANEL_PAD, 0);
    lv_obj_set_style_pad_row(panel, WIZ_PANEL_PAD, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    priv->right_panel = panel;

    // Coordinate readout panel (auto-height, content-driven)
    lv_obj_t *coord_panel = lv_obj_create(panel);
    lv_obj_remove_style_all(coord_panel);
    lv_obj_set_size(coord_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(coord_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(coord_panel, 0, 0);
    lv_obj_set_style_pad_row(coord_panel, 2, 0);
    lv_obj_set_layout(coord_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(coord_panel, LV_FLEX_FLOW_COLUMN);
    priv->coord_panel = coord_panel;

    // Step list panel (takes remaining vertical space)
    lv_obj_t *step_panel = lv_obj_create(panel);
    lv_obj_remove_style_all(step_panel);
    lv_obj_set_flex_grow(step_panel, 1);
    lv_obj_set_width(step_panel, LV_PCT(100));
    lv_obj_clear_flag(step_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(step_panel, 0, 0);
    lv_obj_set_style_pad_row(step_panel, WIZ_PANEL_PAD, 0);
    lv_obj_set_layout(step_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(step_panel, LV_FLEX_FLOW_COLUMN);
    priv->step_panel = step_panel;

    // Initial sidebar render
    sidebar_rebuild(priv);
    sidebar_update_coords(priv);

    lv_cam_positioning_set_mode(root, LV_CAM_POS_MODE_NONE);

    LOGI(TAG, "cam_positioning widget created");
    return root;
}

void lv_cam_positioning_set_receiver(lv_obj_t *obj, cam_receiver_t *receiver)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;

    // Unregister grid callback from old receiver
    if (priv->receiver) {
        cam_receiver_remove_grid_cb(priv->receiver, on_grid_update);
    }

    priv->receiver = receiver;
    lv_cam_stream_set_receiver(priv->stream, receiver);

    // Register grid callback on new receiver so overlay redraws on calibration updates
    if (receiver) {
        cam_receiver_add_grid_cb(receiver, on_grid_update, priv);
        // If grid data is already available, trigger an immediate redraw
        if (cam_receiver_get_grid(receiver) && priv->overlay) {
            lv_obj_invalidate(priv->overlay);
        }
    }
}

cam_receiver_t *lv_cam_positioning_get_receiver(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    return priv ? priv->receiver : NULL;
}

lv_obj_t *lv_cam_positioning_get_stream(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    return priv ? priv->stream : NULL;
}

void lv_cam_positioning_set_machine(lv_obj_t *obj, machine_interface_t *machine)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;
    priv->machine = machine;
    // Invalidate overlay so the grid is redrawn immediately
    if (priv->overlay) lv_obj_invalidate(priv->overlay);
}

machine_interface_t *lv_cam_positioning_get_machine(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    return priv ? priv->machine : NULL;
}

// ---------------------------------------------------------------------------
// Mode control
// ---------------------------------------------------------------------------

void lv_cam_positioning_set_mode(lv_obj_t *obj, lv_cam_pos_mode_t mode)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;
    if (priv->mode == mode) return;

    priv->mode           = mode;
    priv->sel_state      = SEL_NONE;
    priv->point_valid    = false;
    priv->rect_valid     = false;
    priv->circle_valid   = false;
    priv->preview_active = false;

    lv_obj_invalidate(priv->overlay);
    LOGD(TAG, "Mode set to %d", mode);
}

lv_cam_pos_mode_t lv_cam_positioning_get_mode(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    return priv ? priv->mode : LV_CAM_POS_MODE_NONE;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void lv_cam_positioning_set_point_cb(lv_obj_t *obj,
                                      lv_cam_pos_point_cb_t cb, void *user_data)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;
    priv->point_cb      = cb;
    priv->point_cb_user = user_data;
}

void lv_cam_positioning_set_rect_cb(lv_obj_t *obj,
                                     lv_cam_pos_rect_cb_t cb, void *user_data)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;
    priv->rect_cb      = cb;
    priv->rect_cb_user = user_data;
}

void lv_cam_positioning_set_circle_cb(lv_obj_t *obj,
                                       lv_cam_pos_circle_cb_t cb,
                                       void *user_data)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;
    priv->circle_cb      = cb;
    priv->circle_cb_user = user_data;
}

// ---------------------------------------------------------------------------
// Shape queries / manipulation
// ---------------------------------------------------------------------------

void lv_cam_positioning_clear_shapes(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;

    priv->sel_state      = SEL_NONE;
    priv->point_valid    = false;
    priv->rect_valid     = false;
    priv->circle_valid   = false;
    priv->preview_active = false;

    lv_obj_invalidate(priv->overlay);
}

void lv_cam_positioning_set_point(lv_obj_t *obj, const lv_cam_pos_point_t *pt)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv || !pt) return;
    priv->point       = *pt;
    priv->point_valid = true;
    priv->sel_state   = SEL_COMPLETE;
    lv_obj_invalidate(priv->overlay);
}

const lv_cam_pos_point_t *lv_cam_positioning_get_point(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    return (priv && priv->point_valid) ? &priv->point : NULL;
}

const lv_cam_pos_rect_t *lv_cam_positioning_get_rect(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    return (priv && priv->rect_valid) ? &priv->rect : NULL;
}

const lv_cam_pos_circle_t *lv_cam_positioning_get_circle(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    return (priv && priv->circle_valid) ? &priv->circle : NULL;
}

// ---------------------------------------------------------------------------
// Coordinate mapping (public)
// ---------------------------------------------------------------------------

bool lv_cam_positioning_pixel_to_physical(lv_obj_t *obj,
                                           int16_t px_x, int16_t px_y,
                                           float *out_phys_x, float *out_phys_y)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv || !priv->receiver) return false;

    uint16_t iw = 0, ih = 0;
    lv_cam_stream_get_image_size(priv->stream, &iw, &ih);

    const cam_grid_info_t *g = cam_receiver_get_grid(priv->receiver);
    if (!g || iw == 0 || ih == 0) return false;

    return grid_interpolate(g, iw, ih, px_x, px_y, out_phys_x, out_phys_y);
}

bool lv_cam_positioning_has_grid(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv || !priv->receiver) return false;
    return cam_receiver_get_grid(priv->receiver) != NULL;
}

// ---------------------------------------------------------------------------
// Public probe wizard API
// ---------------------------------------------------------------------------

probe_api_ctx_t *lv_cam_positioning_get_probe_ctx(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return NULL;
    return &priv->probe_ctx;
}

void lv_cam_positioning_set_probe_cbs(lv_obj_t *obj,
                                       const probe_api_callbacks_t *cbs)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv || !cbs) return;

    // Store the caller's command callbacks and user_data.
    priv->probe_cmd_move_to      = cbs->cmd_move_to;
    priv->probe_cmd_probe_z      = cbs->cmd_probe_z;
    priv->probe_cmd_probe_rect   = cbs->cmd_probe_rect;
    priv->probe_cmd_probe_circle = cbs->cmd_probe_circle;
    priv->probe_user_data        = cbs->user_data;

    // Initialise the internal probe_api context with forwarding wrappers for
    // the cmd callbacks and widget-internal wrappers for the event callbacks.
    _wiz_reinit_probe_ctx(priv);

    LOGI(TAG, "Probe callbacks registered");
}

void lv_cam_positioning_wizard_cancel(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;
    wiz_reset(priv);
}

// ---------------------------------------------------------------------------
// Local camera-connection status label — public API
// ---------------------------------------------------------------------------

void lv_cam_positioning_show_status_text(lv_obj_t *obj, const char *text)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv || !priv->status_label) return;

    if (text && text[0]) {
        lv_label_set_text(priv->status_label, text);
        lv_obj_clear_flag(priv->status_label, LV_OBJ_FLAG_HIDDEN);
        priv->status_locked = true;   /* Prevent auto-clear by the timer */
    } else {
        /* Empty string: behave like clear */
        priv->status_locked = false;
        lv_obj_add_flag(priv->status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void lv_cam_positioning_clear_status_text(lv_obj_t *obj)
{
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;
    priv->status_locked = false;
    /* Restore the auto-managed "Connecting..." default text for next time. */
    if (priv->status_label)
        lv_label_set_text(priv->status_label, "Connecting to camera...");
    /* Visibility will be corrected by grid_dirty_timer_cb on the next tick. */
}
