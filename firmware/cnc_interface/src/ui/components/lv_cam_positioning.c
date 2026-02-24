// lv_cam_positioning.c — Camera-based positioning view implementation

#define UI_DEBUG_LOCAL_LEVEL D_DEBUG
#include "debug.h"

#include "lv_cam_positioning.h"
#include "lv_cam_stream.h"
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

// Grid overlay
#define GRID_LINE_COLOR     0x808080   // Medium gray
#define GRID_LINE_OPA       LV_OPA_50

// Calibration grid overlay (actual calibration points from the camera)
#define CALIB_GRID_LINE_COLOR  0x44FF44   // Vivid green
#define CALIB_GRID_LINE_OPA    LV_OPA_60
#define CALIB_GRID_DOT_COLOR   0xFFFF44   // Yellow
#define CALIB_GRID_DOT_OPA     LV_OPA_80
#define CALIB_GRID_DOT_R       2          // dot half-size in screen pixels

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
static void  draw_calib_grid(lv_layer_t *layer, lv_cam_pos_priv_t *priv);

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
static bool grid_interpolate(const cam_grid_info_t *g,
                              uint16_t img_w, uint16_t img_h,
                              int16_t px_x, int16_t px_y,
                              float *out_x, float *out_y)
{
    if (!g || !g->points || g->nx < 2 || g->ny < 2) return false;

    // Fractional grid cell position
    float gx = (float)px_x * (float)(g->nx - 1) / (float)(img_w > 1 ? img_w - 1 : 1);
    float gy = (float)px_y * (float)(g->ny - 1) / (float)(img_h > 1 ? img_h - 1 : 1);

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

/// Draw the calibration grid overlay using the actual pixel positions broadcast
/// by the camera (cam_grid_info_t.px_points, available when the companion sends
/// the extended compact grid message that includes calibration image dimensions).
/// Draws green lines between adjacent grid points and yellow dots at each node.
static void draw_calib_grid(lv_layer_t *layer, lv_cam_pos_priv_t *priv)
{
    if (!priv->receiver) return;

    // --- Step 1: snapshot scalar metadata under the mutex, then release. ---
    // LVGL's draw functions (lv_draw_line, lv_draw_rect, etc.) allocate from
    // LVGL's internal pool.  Calling them while holding grid_mutex would invert
    // the lock order vs. handle_grid_map, which calls CAM_ALLOC_LARGE (system
    // heap) before taking grid_mutex.  Follow the same snapshot pattern used by
    // draw_axis_grid: lock → copy scalars → unlock → do all allocations.
    cam_receiver_lock_grid(priv->receiver);
    const cam_grid_info_t *g = cam_receiver_get_grid(priv->receiver);
    if (!g || !g->px_points || g->nx < 1 || g->ny < 1 || g->point_count == 0) {
        cam_receiver_unlock_grid(priv->receiver);
        return;
    }
    uint16_t nx          = g->nx;
    uint16_t ny          = g->ny;
    uint16_t point_count = g->point_count;
    cam_receiver_unlock_grid(priv->receiver);

    uint16_t img_w = 0, img_h = 0;
    lv_cam_stream_get_image_size(priv->stream, &img_w, &img_h);
    if (img_w == 0 || img_h == 0) return;

    // --- Step 2: allocate px_points copy OUTSIDE the mutex. ---
    // Then re-acquire briefly to memcpy (re-validate in case the grid changed
    // while we were calling lv_malloc).
    size_t copy_sz = (size_t)point_count * 2 * sizeof(float);
    float *px_copy = (float *)lv_malloc(copy_sz);
    if (!px_copy) return;

    cam_receiver_lock_grid(priv->receiver);
    g = cam_receiver_get_grid(priv->receiver);
    if (!g || !g->px_points ||
        g->nx != nx || g->ny != ny || g->point_count != point_count) {
        cam_receiver_unlock_grid(priv->receiver);
        lv_free(px_copy);
        return;
    }
    lv_memcpy(px_copy, g->px_points, copy_sz);
    cam_receiver_unlock_grid(priv->receiver);
    // mutex released — safe to call LVGL drawing functions from here on.

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = lv_color_hex(CALIB_GRID_LINE_COLOR);
    ldsc.width = 1;
    ldsc.opa   = CALIB_GRID_LINE_OPA;

    lv_draw_rect_dsc_t ddsc;
    lv_draw_rect_dsc_init(&ddsc);
    ddsc.bg_color = lv_color_hex(CALIB_GRID_DOT_COLOR);
    ddsc.bg_opa   = CALIB_GRID_DOT_OPA;
    ddsc.radius   = LV_RADIUS_CIRCLE;

    // --- Step 3: draw using polyline mode. ---
    // One lv_draw_line call per row / column instead of one per segment.
    // This reduces LVGL draw-task allocations from O(nx × ny) to O(nx + ny),
    // preventing pool exhaustion (LV_ASSERT_MALLOC crash) for large grids.
    // LV_DRAW_LINE_POINT_NONE marks points that could not be mapped to screen
    // so the renderer creates a gap instead of connecting through them.
#define CALIB_POLY_MAX 64u
    lv_point_precise_t pts[CALIB_POLY_MAX];

    // --- Horizontal polylines (one per row) ---
    uint16_t cols = (nx < CALIB_POLY_MAX) ? nx : (uint16_t)CALIB_POLY_MAX;
    for (uint16_t j = 0; j < ny; j++) {
        uint16_t cnt = 0;
        for (uint16_t i = 0; i < cols; i++) {
            uint16_t idx = j * nx + i;
            if (idx >= point_count) {
                pts[cnt].x = LV_DRAW_LINE_POINT_NONE;
                pts[cnt].y = LV_DRAW_LINE_POINT_NONE;
            } else {
                int16_t px = (int16_t)(px_copy[idx * 2 + 0] * (float)img_w);
                int16_t py = (int16_t)(px_copy[idx * 2 + 1] * (float)img_h);
                int32_t sx, sy;
                if (!image_to_screen_coords(priv, px, py, &sx, &sy)) {
                    pts[cnt].x = LV_DRAW_LINE_POINT_NONE;
                    pts[cnt].y = LV_DRAW_LINE_POINT_NONE;
                } else {
                    pts[cnt].x = (lv_value_precise_t)sx;
                    pts[cnt].y = (lv_value_precise_t)sy;
                }
            }
            cnt++;
        }
        if (cnt >= 2) {
            ldsc.points    = pts;
            ldsc.point_cnt = cnt;
            lv_draw_line(layer, &ldsc);
        }
    }

    // --- Vertical polylines (one per column) ---
    uint16_t rows = (ny < CALIB_POLY_MAX) ? ny : (uint16_t)CALIB_POLY_MAX;
    for (uint16_t i = 0; i < nx; i++) {
        uint16_t cnt = 0;
        for (uint16_t j = 0; j < rows; j++) {
            uint16_t idx = j * nx + i;
            if (idx >= point_count) {
                pts[cnt].x = LV_DRAW_LINE_POINT_NONE;
                pts[cnt].y = LV_DRAW_LINE_POINT_NONE;
            } else {
                int16_t px = (int16_t)(px_copy[idx * 2 + 0] * (float)img_w);
                int16_t py = (int16_t)(px_copy[idx * 2 + 1] * (float)img_h);
                int32_t sx, sy;
                if (!image_to_screen_coords(priv, px, py, &sx, &sy)) {
                    pts[cnt].x = LV_DRAW_LINE_POINT_NONE;
                    pts[cnt].y = LV_DRAW_LINE_POINT_NONE;
                } else {
                    pts[cnt].x = (lv_value_precise_t)sx;
                    pts[cnt].y = (lv_value_precise_t)sy;
                }
            }
            cnt++;
        }
        if (cnt >= 2) {
            ldsc.points    = pts;
            ldsc.point_cnt = cnt;
            lv_draw_line(layer, &ldsc);
        }
    }
#undef CALIB_POLY_MAX

    // --- Dots at each calibration point ---
    for (uint16_t k = 0; k < point_count; k++) {
        int16_t px = (int16_t)(px_copy[k * 2 + 0] * (float)img_w);
        int16_t py = (int16_t)(px_copy[k * 2 + 1] * (float)img_h);
        int32_t sx, sy;
        if (!image_to_screen_coords(priv, px, py, &sx, &sy)) continue;
        lv_area_t dot = {
            .x1 = sx - CALIB_GRID_DOT_R, .y1 = sy - CALIB_GRID_DOT_R,
            .x2 = sx + CALIB_GRID_DOT_R, .y2 = sy + CALIB_GRID_DOT_R,
        };
        lv_draw_rect(layer, &ddsc, &dot);
    }

    lv_free(px_copy);
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

/// Draw a millimetre-spaced grid over the camera image using the physical
/// bounds from the camera calibration grid and the step derived from the
/// machine axis limits.
static void draw_axis_grid(lv_layer_t *layer, lv_cam_pos_priv_t *priv)
{
    if (!priv->machine || !priv->receiver) return;

    // Snapshot the grid scalar bounds under the grid lock so we can't race
    // with a concurrent handle_grid_map that may update them.
    cam_receiver_lock_grid(priv->receiver);
    const cam_grid_info_t *g = cam_receiver_get_grid(priv->receiver);
    if (!g) {
        cam_receiver_unlock_grid(priv->receiver);
        return;  // No calibration grid — physical bounds unknown
    }
    float cam_min_x = g->min_x, cam_max_x = g->max_x;
    float cam_min_y = g->min_y, cam_max_y = g->max_y;
    cam_receiver_unlock_grid(priv->receiver);

    uint16_t img_w = 0, img_h = 0;
    lv_cam_stream_get_image_size(priv->stream, &img_w, &img_h);
    if (img_w == 0 || img_h == 0) return;

    // Machine axis ranges drive step-size selection
    float range_x = priv->machine->axis_max[0] - priv->machine->axis_min[0];
    float range_y = priv->machine->axis_max[1] - priv->machine->axis_min[1];
    if (range_x <= 0.0f && range_y <= 0.0f) return;  // Limits not yet received

    float step = cam_pos_choose_grid_step(range_x, range_y);

    // Camera physical view bounds (from snapshot above)
    float phys_w = cam_max_x - cam_min_x;
    float phys_h = cam_max_y - cam_min_y;
    if (phys_w <= 0.0f || phys_h <= 0.0f) return;

    // Screen area occupied by the image object (absolute coordinates)
    lv_obj_t *img_obj = lv_cam_stream_get_image_obj(priv->stream);
    if (!img_obj) return;
    lv_area_t img_area;
    lv_obj_get_coords(img_obj, &img_area);
    int32_t scr_w = (int32_t)lv_area_get_width(&img_area);
    int32_t scr_h = (int32_t)lv_area_get_height(&img_area);
    if (scr_w <= 0 || scr_h <= 0) return;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(GRID_LINE_COLOR);
    line_dsc.width = 1;
    line_dsc.opa   = GRID_LINE_OPA;

    // Vertical grid lines — constant physical-X values
    float x_first = ceilf(cam_min_x / step) * step;
    for (float x = x_first; x <= cam_max_x + 0.001f; x += step) {
        int32_t scr_x = img_area.x1 +
                        (int32_t)((x - cam_min_x) / phys_w * (float)scr_w);
        if (scr_x < img_area.x1 || scr_x > img_area.x2) continue;
        line_dsc.p1.x = (lv_value_precise_t)scr_x;
        line_dsc.p1.y = (lv_value_precise_t)img_area.y1;
        line_dsc.p2.x = (lv_value_precise_t)scr_x;
        line_dsc.p2.y = (lv_value_precise_t)img_area.y2;
        lv_draw_line(layer, &line_dsc);
    }

    // Horizontal grid lines — constant physical-Y values
    float y_first = ceilf(cam_min_y / step) * step;
    for (float y = y_first; y <= cam_max_y + 0.001f; y += step) {
        int32_t scr_y = img_area.y1 +
                        (int32_t)((y - cam_min_y) / phys_h * (float)scr_h);
        if (scr_y < img_area.y1 || scr_y > img_area.y2) continue;
        line_dsc.p1.x = (lv_value_precise_t)img_area.x1;
        line_dsc.p1.y = (lv_value_precise_t)scr_y;
        line_dsc.p2.x = (lv_value_precise_t)img_area.x2;
        line_dsc.p2.y = (lv_value_precise_t)scr_y;
        lv_draw_line(layer, &line_dsc);
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

    // --- Axis-aligned physical grid (drawn first, below shape overlays) ---
    draw_axis_grid(layer, priv);

    // --- Calibration grid from camera (drawn over axis grid) ---
    draw_calib_grid(layer, priv);

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
}

// ---------------------------------------------------------------------------
// Touch handlers
// ---------------------------------------------------------------------------

static void overlay_click_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv || priv->mode == LV_CAM_POS_MODE_NONE) return;

    lv_point_t scr_pt;
    lv_indev_get_point(lv_indev_active(), &scr_pt);

    int16_t img_x, img_y;
    if (!pixel_to_image_coords(priv, scr_pt, &img_x, &img_y)) return;

    LOGD(TAG, "Click at image (%d, %d)", img_x, img_y);

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

/// While dragging / moving after first click, update the preview.
static void overlay_press_cb(lv_event_t *e)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;
    if (priv->sel_state != SEL_FIRST_POINT) return;
    if (priv->mode != LV_CAM_POS_MODE_RECTANGLE &&
        priv->mode != LV_CAM_POS_MODE_CIRCLE) return;

    lv_point_t scr_pt;
    lv_indev_get_point(lv_indev_active(), &scr_pt);

    int16_t img_x, img_y;
    if (!pixel_to_image_coords(priv, scr_pt, &img_x, &img_y)) return;

    priv->preview_active = true;
    priv->preview_px_x   = img_x;
    priv->preview_px_y   = img_y;
    lv_obj_invalidate(priv->overlay);
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

// Async trampoline: runs on the LVGL task after lv_async_call() schedules it.
// on_grid_update is called from the cam-receiver transport-RX FreeRTOS task,
// so it must never touch LVGL objects directly.  Instead it queues this
// function which runs safely inside lv_timer_handler().
static void grid_update_async_cb(void *user_data)
{
    lv_obj_t *overlay = (lv_obj_t *)user_data;
    if (lv_obj_is_valid(overlay)) {
        lv_obj_invalidate(overlay);
    }
}

static void on_grid_update(const cam_grid_info_t *grid, void *user_data)
{
    lv_cam_pos_priv_t *priv = (lv_cam_pos_priv_t *)user_data;
    if (!priv || !priv->overlay) return;
    (void)grid;
    // Post the invalidation to the LVGL thread — never call lv_obj_invalidate()
    // directly from a FreeRTOS task; it is not thread-safe.
    lv_async_call(grid_update_async_cb, priv->overlay);
}

static void on_delete(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_cam_pos_priv_t *priv = get_priv(obj);
    if (!priv) return;

    // Deregister grid callback so the receiver won't call into freed memory.
    if (priv->receiver) {
        cam_receiver_remove_grid_cb(priv->receiver, on_grid_update);
    }

    // Remove all callbacks from the overlay that hold `priv` as raw user_data.
    // LVGL fires LV_EVENT_DELETE on the root before deleting children; if any
    // queued draw or input event fires on the overlay after we free `priv` it
    // would dereference freed memory.  Removing the callbacks here prevents that.
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

    // Root container
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(root, priv);
    lv_obj_add_event_cb(root, on_delete, LV_EVENT_DELETE, NULL);

    priv->root = root;

    // Embedded camera stream widget, inset 5 px from the root edges.
    // We achieve this by giving the root container a 5 px content-padding so
    // that children sized at LV_PCT(100) are already inset.
    lv_obj_set_style_pad_all(root, CAM_INSET_MARGIN_PX, 0);

    lv_obj_t *stream = lv_cam_stream_create(root);
    lv_obj_set_size(stream, LV_PCT(100), LV_PCT(100));
    lv_obj_set_align(stream, LV_ALIGN_CENTER);

    // 5 px rounded border in medium gray + 5 px inner padding
    lv_obj_set_style_border_width(stream, CAM_BORDER_WIDTH_PX, 0);
    lv_obj_set_style_border_color(stream, lv_color_hex(CAM_BORDER_COLOR), 0);
    lv_obj_set_style_border_opa(stream, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(stream, CAM_BORDER_RADIUS_PX, 0);
    lv_obj_set_style_pad_all(stream, CAM_INNER_PAD_PX, 0);  // space between border and image

    priv->stream = stream;

    // If the stream was auto-attached to the default receiver, propagate it
    // so priv->receiver is consistent without requiring an explicit set_receiver
    // call from the parent.
    cam_receiver_t *default_recv = lv_cam_stream_get_receiver(stream);
    if (default_recv) {
        priv->receiver = default_recv;
    }

    // Transparent overlay for touch events + custom draw
    lv_obj_t *overlay = lv_obj_create(root);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_align(overlay, LV_ALIGN_CENTER);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    // Register draw + input events
    lv_obj_add_event_cb(overlay, overlay_draw_cb,
                         LV_EVENT_DRAW_MAIN_END, priv);
    lv_obj_add_event_cb(overlay, overlay_click_cb,
                         LV_EVENT_CLICKED, priv);
    lv_obj_add_event_cb(overlay, overlay_press_cb,
                         LV_EVENT_PRESSING, priv);
    priv->overlay = overlay;

    // Register grid-update callback so the overlay redraws when new calibration
    // data arrives (the overlay ptr must be set first, hence done here).
    if (priv->receiver) {
        cam_receiver_add_grid_cb(priv->receiver, on_grid_update, priv);
    }

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
