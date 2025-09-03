#include "lv_probing_wizard.h"
#include <math.h>
#include <string.h>
#include "misc/lv_math.h"

#include "misc/lv_area_private.h"

// Defines for configurable drawing parameters
#define PROBING_WIZARD_CROSSHAIR_SIZE 20
#define PROBING_WIZARD_CROSSHAIR_WIDTH 5
#define PROBING_WIZARD_DASH_LINE_LEN_PCT 25 // As a percentage of min(width, height)
#define PROBING_WIZARD_DASH_LINE_WIDTH 2
#define PROBING_WIZARD_PAD_PCT -5 // Additional padding percentage

#define MAX_SETUP_POINTS 2
#define MAX_PROBE_POINTS 4

// Helper macros for defining probe actions succinctly
#define SETUP_PARAM(i) .param = {.setup_point_index = i}
#define PROBE_PARAM(i) .param = {.probe_index = i}

// --- Probe Routine Definitions ---

static const lv_probing_action_t rectangle_probe_actions[] = {
    {.instruction_text = "Probe workpiece Z height.", .highlight_mask = HIGHLIGHT_Z_PROBE | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_Z_TOP},
    {.instruction_text = "Jog to the back-left corner of the probing area.", .highlight_mask = HIGHLIGHT_CORNER_BL | HIGHLIGHT_OUTLINE, .type = ACTION_JOG_AND_CONFIRM, SETUP_PARAM(0)},
    {.instruction_text = "Jog to the front-right corner of the probing area.", .highlight_mask = HIGHLIGHT_CORNER_FR | HIGHLIGHT_OUTLINE, .type = ACTION_JOG_AND_CONFIRM, SETUP_PARAM(1)},
    {.instruction_text = "Probing negative X surface (PX1)...", .highlight_mask = HIGHLIGHT_PROBE_POINT_0 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(0)},
    {.instruction_text = "Probing positive X surface (PX2)...", .highlight_mask = HIGHLIGHT_PROBE_POINT_1 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(1)},
    {.instruction_text = "Probing positive Y surface (PY1)...", .highlight_mask = HIGHLIGHT_PROBE_POINT_2 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(2)},
    {.instruction_text = "Probing negative Y surface (PY2)...", .highlight_mask = HIGHLIGHT_PROBE_POINT_3 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(3)},
    {.instruction_text = "Probing complete. Result is calculated.", .highlight_mask = HIGHLIGHT_CENTER | HIGHLIGHT_OUTLINE, .type = ACTION_COMPLETE},
};

static const lv_probing_action_t circle_probe_actions[] = {
    {.instruction_text = "Probe workpiece Z height.", .highlight_mask = HIGHLIGHT_Z_PROBE | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_Z_TOP},
    {.instruction_text = "Jog roughly to the center of the circle.", .highlight_mask = HIGHLIGHT_CENTER | HIGHLIGHT_OUTLINE, .type = ACTION_JOG_AND_CONFIRM, SETUP_PARAM(0)},
    {.instruction_text = "Probing point 1...", .highlight_mask = HIGHLIGHT_PROBE_POINT_0 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(0)},
    {.instruction_text = "Probing point 2...", .highlight_mask = HIGHLIGHT_PROBE_POINT_1 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(1)},
    {.instruction_text = "Probing point 3...", .highlight_mask = HIGHLIGHT_PROBE_POINT_2 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(2)},
    {.instruction_text = "Probing complete. Result is calculated.", .highlight_mask = HIGHLIGHT_CENTER | HIGHLIGHT_OUTLINE, .type = ACTION_COMPLETE},
};

static const lv_probing_action_t corner_probe_actions[] = {
    {.instruction_text = "Click on the corner you wish to probe.", .highlight_mask = HIGHLIGHT_CORNER_BL | HIGHLIGHT_CORNER_BR | HIGHLIGHT_CORNER_FL | HIGHLIGHT_CORNER_FR | HIGHLIGHT_OUTLINE, .type = ACTION_SELECT_CORNER},
    {.instruction_text = "Probe workpiece Z height.", .highlight_mask = HIGHLIGHT_Z_PROBE | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_Z_TOP},
    {.instruction_text = "Probing first surface (X)...", .highlight_mask = HIGHLIGHT_PROBE_POINT_0 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(0)},
    {.instruction_text = "Probing second surface (Y)...", .highlight_mask = HIGHLIGHT_PROBE_POINT_1 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(1)},
    {.instruction_text = "Probing complete. Corner is set.", .highlight_mask = HIGHLIGHT_CORNER_BL | HIGHLIGHT_OUTLINE, .type = ACTION_COMPLETE},
};

static const lv_probing_action_t * const probe_routines[] = {
    [LV_PROBING_WIZARD_MODE_RECTANGLE] = rectangle_probe_actions,
    [LV_PROBING_WIZARD_MODE_CIRCLE] = circle_probe_actions,
    [LV_PROBING_WIZARD_MODE_CORNER] = corner_probe_actions,
};
static const uint8_t probe_routine_sizes[] = {
    [LV_PROBING_WIZARD_MODE_RECTANGLE] = sizeof(rectangle_probe_actions) / sizeof(lv_probing_action_t),
    [LV_PROBING_WIZARD_MODE_CIRCLE] = sizeof(circle_probe_actions) / sizeof(lv_probing_action_t),
    [LV_PROBING_WIZARD_MODE_CORNER] = sizeof(corner_probe_actions) / sizeof(lv_probing_action_t),
};

typedef struct {
    lv_obj_t * canvas;
    lv_obj_t * instruction_label;
    lv_obj_t * result_label_x;
    lv_obj_t * result_label_y;
    lv_obj_t * mode_btnm;
    lv_obj_t * variant_btnm;
    lv_obj_t * next_btn;
    lv_obj_t * cancel_btn;
    
    lv_probing_wizard_mode_t mode;
    bool is_inside;
    lv_probing_wizard_corner_t corner_type;
    uint8_t active_step;
    const lv_probing_action_t * current_action;

    probe_point_t setup_points[MAX_SETUP_POINTS];
    probe_point_t probe_results[MAX_PROBE_POINTS];
    float z_top;
    bool z_top_is_set;

    lv_probing_wizard_point_float_t result;
    bool result_valid;

    get_current_jogged_position_cb_t get_pos_cb;
    execute_probe_cb_t exec_probe_cb;

    char result_label_x_text[32];
    char result_label_y_text[32];
} lv_probing_wizard_t;

static void wizard_destructor(lv_event_t * e);
static void draw_event_cb(lv_event_t * e);
static void calculate_result(lv_obj_t * obj);
static void set_active_step(lv_obj_t * obj, uint8_t step_index);
static void next_btn_event_cb(lv_event_t * e);
static void cancel_btn_event_cb(lv_event_t * e);
static void mode_selector_event_cb(lv_event_t * e);
static void canvas_click_event_cb(lv_event_t * e);

static void create_mode_selectors(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_wcs_selector(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_results_display(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_left_panel(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_canvas_and_controls(lv_obj_t * parent, lv_probing_wizard_t * wiz);

static void draw_rectangle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static void draw_circle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static void draw_corner_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static inline void draw_result_crosshair(lv_layer_t * layer, lv_point_t center, lv_color_t color);

static void create_left_panel(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    lv_obj_t * left_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(left_panel);
    lv_obj_set_width(left_panel, 220);
    lv_obj_set_height(left_panel, lv_pct(100));
    lv_obj_set_layout(left_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(left_panel, 1);
    lv_obj_set_style_pad_gap(left_panel, 15, 0);

    create_mode_selectors(left_panel, wiz);
    create_wcs_selector(left_panel, wiz);
    create_results_display(left_panel, wiz);
}

static void create_canvas_and_controls(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    lv_obj_t* wizard_obj = lv_obj_get_parent(lv_obj_get_parent(parent));

    lv_obj_t * right_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(right_panel);
    lv_obj_set_flex_grow(right_panel, 1);
    lv_obj_set_size(right_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(right_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(right_panel, 10, 0);

    wiz->canvas = lv_obj_create(right_panel);
    lv_obj_remove_style_all(wiz->canvas);
    lv_obj_set_flex_grow(wiz->canvas, 1);
    lv_obj_set_size(wiz->canvas, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(wiz->canvas, draw_event_cb, LV_EVENT_DRAW_POST, wiz);
    lv_obj_set_style_bg_color(wiz->canvas, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(wiz->canvas, 8, 0);
    lv_obj_set_style_bg_opa(wiz->canvas, LV_OPA_COVER, 0);
    lv_obj_add_flag(wiz->canvas, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * btn_bar = lv_obj_create(right_panel);
    lv_obj_remove_style_all(btn_bar);
    lv_obj_set_width(btn_bar, lv_pct(100));
    lv_obj_set_height(btn_bar, LV_SIZE_CONTENT);
    lv_obj_set_layout(btn_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_bar, LV_FLEX_FLOW_ROW);
    
    wiz->cancel_btn = lv_btn_create(btn_bar);
    lv_obj_set_flex_grow(wiz->cancel_btn, 1);
    lv_obj_add_event_cb(wiz->cancel_btn, cancel_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_t * cancel_label = lv_label_create(wiz->cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    lv_obj_t* spacer = lv_obj_create(btn_bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 30, 10);

    wiz->next_btn = lv_btn_create(btn_bar);
    lv_obj_set_flex_grow(wiz->next_btn, 1);
    lv_obj_add_event_cb(wiz->next_btn, next_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_t * next_label = lv_label_create(wiz->next_btn);
    lv_label_set_text(next_label, "Next");
    lv_obj_center(next_label);
}

static void mode_selector_event_cb(lv_event_t * e) {
    lv_obj_t * btnm = lv_event_get_target(e);
    lv_obj_t * wizard_obj = lv_event_get_user_data(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(wizard_obj);
    if(!wiz) return;

    uint32_t id = lv_btnmatrix_get_selected_btn(btnm);
    if (id == LV_BTNMATRIX_BTN_NONE) return;

    if (btnm == wiz->mode_btnm) {
        lv_probing_wizard_mode_t new_mode = (lv_probing_wizard_mode_t)id;
        if (new_mode != wiz->mode) {
            lv_probing_wizard_set_mode(wizard_obj, new_mode, wiz->is_inside);
        }
    } else if (btnm == wiz->variant_btnm) {
        bool new_is_inside = (id == 0);
        if (new_is_inside != wiz->is_inside) {
            lv_probing_wizard_set_mode(wizard_obj, wiz->mode, new_is_inside);
        }
    }
}

static void create_mode_selectors(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    lv_obj_t* wizard_obj = lv_obj_get_parent(lv_obj_get_parent(parent)); 

    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_width(cont, lv_pct(100));
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(cont, 5, 0);

    static const char * mode_map[] = {"Block", "Circular", "Corner", ""};
    wiz->mode_btnm = lv_btnmatrix_create(cont);
    lv_btnmatrix_set_map(wiz->mode_btnm, mode_map);
    lv_btnmatrix_set_btn_ctrl_all(wiz->mode_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(wiz->mode_btnm, true);
    lv_obj_set_size(wiz->mode_btnm, lv_pct(100), 45);
    lv_obj_add_event_cb(wiz->mode_btnm, mode_selector_event_cb, LV_EVENT_VALUE_CHANGED, wizard_obj);
    lv_obj_set_style_text_font(wiz->mode_btnm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(wiz->mode_btnm, 2, 0);

    static const char * variant_map[] = {"Inside", "Outside", ""};
    wiz->variant_btnm = lv_btnmatrix_create(cont);
    lv_btnmatrix_set_map(wiz->variant_btnm, variant_map);
    lv_btnmatrix_set_btn_ctrl_all(wiz->variant_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(wiz->variant_btnm, true);
    lv_obj_set_size(wiz->variant_btnm, lv_pct(100), 45);
    lv_obj_add_event_cb(wiz->variant_btnm, mode_selector_event_cb, LV_EVENT_VALUE_CHANGED, wizard_obj);
    lv_obj_set_style_text_font(wiz->variant_btnm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(wiz->variant_btnm, 2, 0);
}

static void create_wcs_selector(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    lv_obj_t * gcs_cont = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(gcs_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gcs_cont, 0, 0);
    lv_obj_set_style_pad_all(gcs_cont, 0, 0);
    lv_obj_set_width(gcs_cont, lv_pct(100));
    lv_obj_set_layout(gcs_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gcs_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(gcs_cont, 5, 0);

    lv_obj_t* gcs_label = lv_label_create(gcs_cont);
    lv_label_set_text_static(gcs_label, "Workpiece Coordinate");
    lv_obj_set_style_text_color(gcs_label, lv_color_hex(0x888888), 0);

    static const char * btnm_map[] = {"G55", "G56", "G57", "\n", "G58", "G59", ""};
    lv_obj_t * btnm = lv_btnmatrix_create(gcs_cont);
    lv_btnmatrix_set_map(btnm, btnm_map);
    lv_btnmatrix_set_btn_ctrl_all(btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(btnm, true);
    lv_obj_set_size(btnm, lv_pct(100), 95);
    lv_obj_add_flag(btnm, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_text_font(btnm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(btnm, 2, 0);
}

static void create_results_display(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    lv_obj_t * res_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(res_cont);
    lv_obj_set_width(res_cont, lv_pct(100));
    lv_obj_set_layout(res_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(res_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(res_cont, 5, 0);
    lv_obj_set_flex_grow(res_cont, 1);
    lv_obj_set_flex_align(res_cont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* res_label = lv_label_create(res_cont);
    lv_label_set_text_static(res_label, "Probing Result");
    lv_obj_set_style_text_color(res_label, lv_color_hex(0x888888), 0);
    wiz->result_label_x = lv_label_create(res_cont);
    wiz->result_label_y = lv_label_create(res_cont);
    lv_obj_set_style_text_font(wiz->result_label_x, lv_theme_get_font_large(parent), 0);
    lv_obj_set_style_text_font(wiz->result_label_y, lv_theme_get_font_large(parent), 0);
    lv_obj_set_style_text_color(wiz->result_label_x, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_text_color(wiz->result_label_y, lv_color_hex(0x007AFF), 0);
}

lv_obj_t * lv_probing_wizard_create(lv_obj_t * parent) {
    lv_probing_wizard_t * wiz = lv_malloc(sizeof(lv_probing_wizard_t));
    LV_ASSERT_MALLOC(wiz);
    if (wiz == NULL) return NULL;
    lv_memset(wiz, 0, sizeof(lv_probing_wizard_t));

    lv_obj_t * main_container = lv_obj_create(parent);
    lv_obj_remove_style_all(main_container);
    lv_obj_set_size(main_container, lv_pct(100), lv_pct(100));
    lv_obj_set_user_data(main_container, wiz);
    lv_obj_add_event_cb(main_container, wizard_destructor, LV_EVENT_DELETE, NULL);

    lv_obj_set_layout(main_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(main_container, 10, 0);
    lv_obj_set_style_pad_gap(main_container, 10, 0);

    wiz->instruction_label = lv_label_create(main_container);
    lv_obj_set_width(wiz->instruction_label, lv_pct(100));
    lv_obj_set_height(wiz->instruction_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(wiz->instruction_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(wiz->instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(wiz->instruction_label, lv_theme_get_font_large(parent), 0);

    lv_obj_t * content_container = lv_obj_create(main_container);
    lv_obj_remove_style_all(content_container);
    lv_obj_set_width(content_container, lv_pct(100));
    lv_obj_set_flex_grow(content_container, 1);
    lv_obj_set_layout(content_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(content_container, 10, 0);

    create_left_panel(content_container, wiz);
    create_canvas_and_controls(content_container, wiz);

    wiz->mode = LV_PROBING_WIZARD_MODE_RECTANGLE;
    wiz->is_inside = false;
    lv_probing_wizard_set_mode(main_container, wiz->mode, wiz->is_inside);

    return main_container;
}

static void reset_and_start_routine(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    for (int i = 0; i < MAX_SETUP_POINTS; i++) wiz->setup_points[i].is_set = false;
    for (int i = 0; i < MAX_PROBE_POINTS; i++) wiz->probe_results[i].is_set = false;
    
    wiz->result_valid = false;
    wiz->z_top_is_set = false;
    wiz->active_step = 0;

    if (wiz->mode_btnm) lv_btnmatrix_set_btn_ctrl(wiz->mode_btnm, (uint16_t)wiz->mode, LV_BTNMATRIX_CTRL_CHECKED);
    if (wiz->variant_btnm) lv_btnmatrix_set_btn_ctrl(wiz->variant_btnm, wiz->is_inside ? 0 : 1, LV_BTNMATRIX_CTRL_CHECKED);
    
    set_active_step(obj, 0);
}

void lv_probing_wizard_set_mode(lv_obj_t * obj, lv_probing_wizard_mode_t mode, bool is_inside) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;

    wiz->mode = mode;
    wiz->is_inside = is_inside;
    reset_and_start_routine(obj);
}

void lv_probing_wizard_set_corner_type(lv_obj_t * obj, lv_probing_wizard_corner_t corner) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    if (wiz->mode != LV_PROBING_WIZARD_MODE_CORNER || wiz->corner_type == corner) return;
    wiz->corner_type = corner;
    if(wiz->canvas) lv_obj_invalidate(wiz->canvas);
}

void lv_probing_wizard_register_callbacks(lv_obj_t * obj, get_current_jogged_position_cb_t get_pos_cb, execute_probe_cb_t exec_probe_cb) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    wiz->get_pos_cb = get_pos_cb;
    wiz->exec_probe_cb = exec_probe_cb;
}

void lv_probing_wizard_set_z_top(lv_obj_t * obj, float z_top) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    wiz->z_top = z_top;
    wiz->z_top_is_set = true;
}

void lv_probing_wizard_report_probe_result(lv_obj_t * obj, uint8_t probe_index, float x, float y) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz || probe_index >= MAX_PROBE_POINTS) return;

    wiz->probe_results[probe_index].x = x;
    wiz->probe_results[probe_index].y = y;
    wiz->probe_results[probe_index].is_set = true;
    calculate_result(obj);
}

void lv_probing_wizard_advance_step(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz || !wiz->current_action) return;

    if (wiz->current_action->type != ACTION_COMPLETE) {
        set_active_step(obj, wiz->active_step + 1);
    }
}

void lv_probing_wizard_set_active_step(lv_obj_t * obj, uint8_t step_index) {
    set_active_step(obj, step_index);
}

const probe_point_t * lv_probing_wizard_get_setup_points(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    return wiz ? wiz->setup_points : NULL;
}

float lv_probing_wizard_get_z_top(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    return (wiz && wiz->z_top_is_set) ? wiz->z_top : 0.0f;
}

lv_probing_wizard_point_float_t lv_probing_wizard_get_result(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (wiz && wiz->result_valid) {
        return wiz->result;
    }
    return (lv_probing_wizard_point_float_t){0.0f, 0.0f};
}

static void set_active_step(lv_obj_t * obj, uint8_t step_index) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    if (step_index >= probe_routine_sizes[wiz->mode]) return;

    lv_obj_remove_event_cb(wiz->canvas, canvas_click_event_cb);

    wiz->active_step = step_index;
    wiz->current_action = &probe_routines[wiz->mode][step_index];

    lv_label_set_text(wiz->instruction_label, wiz->current_action->instruction_text);
    if(wiz->canvas) lv_obj_invalidate(wiz->canvas);

    if (wiz->result_label_x) {
        if (wiz->result_valid) {
            snprintf(wiz->result_label_x_text, sizeof(wiz->result_label_x_text), "X=   %.2f", wiz->result.x);
            snprintf(wiz->result_label_y_text, sizeof(wiz->result_label_y_text), "Y=   %.2f", wiz->result.y);
        } else {
            snprintf(wiz->result_label_x_text, sizeof(wiz->result_label_x_text), "X=   - - -");
            snprintf(wiz->result_label_y_text, sizeof(wiz->result_label_y_text), "Y=   - - -");
        }
        lv_label_set_text(wiz->result_label_x, wiz->result_label_x_text);
        lv_label_set_text(wiz->result_label_y, wiz->result_label_y_text);
    }
    
    switch (wiz->current_action->type) {
        case ACTION_JOG_AND_CONFIRM:
        case ACTION_MESSAGE:
            lv_obj_clear_state(wiz->next_btn, LV_STATE_DISABLED);
            break;
        case ACTION_SELECT_CORNER:
            lv_obj_add_state(wiz->next_btn, LV_STATE_DISABLED); // Must click canvas
            lv_obj_add_event_cb(wiz->canvas, canvas_click_event_cb, LV_EVENT_CLICKED, obj);
            break;
        case ACTION_PROBE_POINT:
        case ACTION_PROBE_Z_TOP:
            lv_obj_add_state(wiz->next_btn, LV_STATE_DISABLED);
            if (wiz->exec_probe_cb) wiz->exec_probe_cb(obj, wiz->current_action);
            break;
        case ACTION_COMPLETE:
            lv_obj_add_state(wiz->next_btn, LV_STATE_DISABLED);
            break;
    }
}

static void next_btn_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz || !wiz->current_action) return;

    if (wiz->current_action->type == ACTION_JOG_AND_CONFIRM) {
        if (wiz->get_pos_cb) {
            uint8_t index = wiz->current_action->param.setup_point_index;
            if (index < MAX_SETUP_POINTS) {
                lv_probing_wizard_point_float_t pos = wiz->get_pos_cb();
                wiz->setup_points[index].x = pos.x;
                wiz->setup_points[index].y = pos.y;
                wiz->setup_points[index].is_set = true;
            }
        }
    }
    lv_probing_wizard_advance_step(obj);
}

static void cancel_btn_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_user_data(e);
    reset_and_start_routine(obj);
}

static void wizard_destructor(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (wiz) {
        lv_free(wiz);
    }
}

static void calculate_result(lv_obj_t* obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;

    wiz->result_valid = false;
    switch (wiz->mode) {
        case LV_PROBING_WIZARD_MODE_RECTANGLE:
            if (wiz->probe_results[0].is_set && wiz->probe_results[1].is_set && wiz->probe_results[2].is_set && wiz->probe_results[3].is_set) {
                wiz->result.x = (wiz->probe_results[0].x + wiz->probe_results[1].x) / 2.0f;
                wiz->result.y = (wiz->probe_results[2].y + wiz->probe_results[3].y) / 2.0f;
                wiz->result_valid = true;
            }
            break;
        case LV_PROBING_WIZARD_MODE_CIRCLE:
            if (wiz->probe_results[0].is_set && wiz->probe_results[1].is_set && wiz->probe_results[2].is_set) {
                 wiz->result.x = (wiz->probe_results[0].x + wiz->probe_results[1].x + wiz->probe_results[2].x) / 3.0f;
                 wiz->result.y = (wiz->probe_results[0].y + wiz->probe_results[1].y + wiz->probe_results[2].y) / 3.0f;
                 wiz->result_valid = true;
            }
            break;
        case LV_PROBING_WIZARD_MODE_CORNER:
            if (wiz->probe_results[0].is_set && wiz->probe_results[1].is_set) {
                wiz->result.x = wiz->probe_results[0].x;
                wiz->result.y = wiz->probe_results[1].y;
                wiz->result_valid = true;
            }
            break;
    }
}

/***************************************************
 * DRAWING IMPLEMENTATION
 ***************************************************/

static inline void draw_result_crosshair(lv_layer_t * layer, lv_point_t center, lv_color_t color) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = PROBING_WIZARD_CROSSHAIR_WIDTH;
    lv_coord_t size = PROBING_WIZARD_CROSSHAIR_SIZE / 2;
    line_dsc.p1.x = center.x - size; line_dsc.p1.y = center.y;
    line_dsc.p2.x = center.x + size; line_dsc.p2.y = center.y;
    lv_draw_line(layer, &line_dsc);
    line_dsc.p1.x = center.x; line_dsc.p1.y = center.y - size;
    line_dsc.p2.x = center.x; line_dsc.p2.y = center.y + size;
    lv_draw_line(layer, &line_dsc);
}

static void draw_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_probing_wizard_t * wiz = lv_event_get_user_data(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t canvas_area;
    lv_obj_get_coords(obj, &canvas_area);

    int32_t min_dim = LV_MIN(lv_area_get_width(&canvas_area), lv_area_get_height(&canvas_area));
    int32_t pad = (min_dim * (PROBING_WIZARD_DASH_LINE_LEN_PCT + PROBING_WIZARD_PAD_PCT)) / 100;
    lv_area_t draw_area = canvas_area;
    lv_area_increase(&draw_area, -pad, -pad);

    switch(wiz->mode) {
        case LV_PROBING_WIZARD_MODE_RECTANGLE:
            draw_rectangle_probe(wiz, layer, &draw_area);
            break;
        case LV_PROBING_WIZARD_MODE_CIRCLE:
            draw_circle_probe(wiz, layer, &draw_area);
            break;
        case LV_PROBING_WIZARD_MODE_CORNER:
            draw_corner_probe(wiz, layer, &draw_area);
            break;
    }
}

static void draw_rectangle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area) {
    lv_color_t color_active = lv_color_hex(0xFF9500);
    lv_color_t color_done = lv_color_hex(0x888888);
    lv_color_t color_pending = lv_color_white();
    lv_color_t color_result = lv_color_hex(0x007AFF);
    uint32_t mask = wiz->current_action ? wiz->current_action->highlight_mask : HIGHLIGHT_NONE;

    lv_area_t square_area;
    lv_coord_t side = LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area));
    lv_area_set_width(&square_area, side);
    lv_area_set_height(&square_area, side);
    lv_area_align(draw_area, &square_area, LV_ALIGN_CENTER, 0, 0);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.border_width = 2;
    rect_dsc.border_color = (mask & HIGHLIGHT_OUTLINE) ? color_active : color_pending;
    rect_dsc.bg_opa = LV_OPA_TRANSP;
    rect_dsc.radius = 5;
    lv_draw_rect(layer, &rect_dsc, &square_area);

    if (wiz->result_valid || (mask & HIGHLIGHT_CENTER)) {
         lv_point_t center = {(square_area.x1 + square_area.x2) / 2, (square_area.y1 + square_area.y2) / 2};
         draw_result_crosshair(layer, center, wiz->result_valid ? color_result : color_active);
    }
    
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = PROBING_WIZARD_DASH_LINE_WIDTH;
    line_dsc.dash_width = 6;
    line_dsc.dash_gap = 4;

    lv_point_t points[4] = {
        {square_area.x1, (square_area.y1 + square_area.y2) / 2}, {square_area.x2, (square_area.y1 + square_area.y2) / 2},
        {(square_area.x1 + square_area.x2) / 2, square_area.y2}, {(square_area.x1 + square_area.x2) / 2, square_area.y1},
    };
    int path_len = (side * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;
    lv_point_t path_starts[4] = {
        {points[0].x + (wiz->is_inside ? path_len : -path_len), points[0].y}, {points[1].x + (wiz->is_inside ? -path_len : path_len), points[1].y},
        {points[2].x, points[2].y + (wiz->is_inside ? -path_len : path_len)}, {points[3].x, points[3].y + (wiz->is_inside ? path_len : -path_len)},
    };
    uint32_t highlight_flags[] = {HIGHLIGHT_PROBE_POINT_0, HIGHLIGHT_PROBE_POINT_1, HIGHLIGHT_PROBE_POINT_2, HIGHLIGHT_PROBE_POINT_3};

    for(int i=0; i < 4; i++) {
        lv_color_t current_color = wiz->probe_results[i].is_set ? color_done : ((mask & highlight_flags[i]) ? color_active : color_pending);
        line_dsc.color = current_color;
        line_dsc.p1.x = path_starts[i].x; line_dsc.p1.y = path_starts[i].y;
        line_dsc.p2.x = points[i].x; line_dsc.p2.y = points[i].y;
        lv_draw_line(layer, &line_dsc);
        
        rect_dsc.bg_color = current_color;
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        rect_dsc.border_width = 0;
        lv_area_t point_area = {points[i].x - 4, points[i].y - 4, points[i].x + 4, points[i].y + 4};
        lv_draw_rect(layer, &rect_dsc, &point_area);
    }
}

static void draw_circle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area) {
    lv_color_t color_active = lv_color_hex(0xFF9500);
    lv_color_t color_done = lv_color_hex(0x888888);
    lv_color_t color_pending = lv_color_white();
    lv_color_t color_result = lv_color_hex(0x007AFF);
    uint32_t mask = wiz->current_action ? wiz->current_action->highlight_mask : HIGHLIGHT_NONE;

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = (mask & HIGHLIGHT_OUTLINE) ? color_active : color_pending;
    arc_dsc.width = 2;
    lv_point_t center = {(draw_area->x1 + draw_area->x2) / 2, (draw_area->y1 + draw_area->y2) / 2};
    arc_dsc.center = center;
    int32_t radius = (LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area))) / 2;
    arc_dsc.radius = radius;
    arc_dsc.start_angle = 0; arc_dsc.end_angle = 360;
    lv_draw_arc(layer, &arc_dsc);

    if (wiz->result_valid || (mask & HIGHLIGHT_CENTER)) {
         draw_result_crosshair(layer, center, wiz->result_valid ? color_result : color_active);
    }
    
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = PROBING_WIZARD_DASH_LINE_WIDTH;
    line_dsc.dash_width = 6;
    line_dsc.dash_gap = 4;
    
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    
    float angles[] = { -90, 150, 30 };
    lv_point_t points[3];
    int path_len = (radius * 2 * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;
    uint32_t highlight_flags[] = {HIGHLIGHT_PROBE_POINT_0, HIGHLIGHT_PROBE_POINT_1, HIGHLIGHT_PROBE_POINT_2};

    for(int i=0; i<3; i++) {
        float rad = angles[i] * (M_PI / 180.0f);
        points[i].x = center.x + radius * cosf(rad);
        points[i].y = center.y + radius * sinf(rad);
        
        int path_radius = radius + (wiz->is_inside ? -path_len : path_len);
        lv_point_t path_start = {center.x + path_radius * cosf(rad), center.y + path_radius * sinf(rad)};
        
        lv_color_t current_color = wiz->probe_results[i].is_set ? color_done : ((mask & highlight_flags[i]) ? color_active : color_pending);
        line_dsc.color = current_color;
        line_dsc.p1.x = path_start.x; line_dsc.p1.y = path_start.y;
        line_dsc.p2.x = points[i].x; line_dsc.p2.y = points[i].y;
        lv_draw_line(layer, &line_dsc);
        
        rect_dsc.bg_color = current_color;
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        rect_dsc.border_width = 0;
        lv_area_t point_area = {points[i].x - 4, points[i].y - 4, points[i].x + 4, points[i].y + 4};
        lv_draw_rect(layer, &rect_dsc, &point_area);
    }
}

static void draw_corner_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area) {
    lv_color_t color_active = lv_color_hex(0xFF9500);
    lv_color_t color_done = lv_color_hex(0x888888);
    lv_color_t color_pending = lv_color_white();
    lv_color_t color_result = lv_color_hex(0x007AFF);
    uint32_t mask = wiz->current_action ? wiz->current_action->highlight_mask : HIGHLIGHT_NONE;

    if (wiz->current_action->type == ACTION_SELECT_CORNER) {
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.border_width = 2;
        rect_dsc.border_color = (mask & HIGHLIGHT_OUTLINE) ? color_active : color_pending;
        rect_dsc.bg_opa = LV_OPA_TRANSP;
        rect_dsc.radius = 5;
        lv_draw_rect(layer, &rect_dsc, draw_area);

        lv_point_t corners[] = { {draw_area->x1, draw_area->y1}, {draw_area->x2, draw_area->y1}, {draw_area->x1, draw_area->y2}, {draw_area->x2, draw_area->y2} };
        for(int i=0; i<4; i++) {
            lv_area_t corner_area = {corners[i].x-5, corners[i].y-5, corners[i].x+5, corners[i].y+5};
            rect_dsc.bg_color = color_active;
            rect_dsc.border_width = 0;
            lv_draw_rect(layer, &rect_dsc, &corner_area);
        }
        return;
    }

    lv_point_t corner_pt, h_end, v_end;
    switch (wiz->corner_type) {
        case LV_PROBING_CORNER_FRONT_LEFT: corner_pt = (lv_point_t){draw_area->x1, draw_area->y2}; h_end = (lv_point_t){draw_area->x2, draw_area->y2}; v_end = (lv_point_t){draw_area->x1, draw_area->y1}; break;
        case LV_PROBING_CORNER_FRONT_RIGHT: corner_pt = (lv_point_t){draw_area->x2, draw_area->y2}; h_end = (lv_point_t){draw_area->x1, draw_area->y2}; v_end = (lv_point_t){draw_area->x2, draw_area->y1}; break;
        case LV_PROBING_CORNER_BACK_LEFT: corner_pt = (lv_point_t){draw_area->x1, draw_area->y1}; h_end = (lv_point_t){draw_area->x2, draw_area->y1}; v_end = (lv_point_t){draw_area->x1, draw_area->y2}; break;
        case LV_PROBING_CORNER_BACK_RIGHT: default: corner_pt = (lv_point_t){draw_area->x2, draw_area->y1}; h_end = (lv_point_t){draw_area->x1, draw_area->y1}; v_end = (lv_point_t){draw_area->x2, draw_area->y2}; break;
    }

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = (mask & HIGHLIGHT_OUTLINE) ? color_active : color_pending;
    line_dsc.width = 2;
    line_dsc.p1.x = corner_pt.x; line_dsc.p1.y = corner_pt.y; line_dsc.p2.x = h_end.x; line_dsc.p2.y = h_end.y;
    lv_draw_line(layer, &line_dsc);
    line_dsc.p2.x = v_end.x; line_dsc.p2.y = v_end.y;
    lv_draw_line(layer, &line_dsc);

    if (wiz->result_valid) {
        draw_result_crosshair(layer, corner_pt, color_result);
    }

    line_dsc.width = PROBING_WIZARD_DASH_LINE_WIDTH;
    line_dsc.dash_width = 6; line_dsc.dash_gap = 4;
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    
    lv_coord_t center_x = (draw_area->x1 + draw_area->x2) / 2;
    lv_coord_t center_y = (draw_area->y1 + draw_area->y2) / 2;
    lv_point_t points[2] = {{center_x, corner_pt.y}, {corner_pt.x, center_y}};
    int path_len = (LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area)) * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;

    lv_point_t path_starts[2];
    path_starts[0] = (lv_point_t){points[0].x, points[0].y + (wiz->is_inside ? (points[0].y > center_y ? -path_len : path_len) : (points[0].y < center_y ? path_len : -path_len))};
    path_starts[1] = (lv_point_t){points[1].x + (wiz->is_inside ? (points[1].x > center_x ? -path_len : path_len) : (points[1].x < center_x ? path_len : -path_len)), points[1].y};

    uint32_t highlight_flags[] = {HIGHLIGHT_PROBE_POINT_0, HIGHLIGHT_PROBE_POINT_1};

    for(int i=0; i<2; i++) {
        lv_color_t current_color = wiz->probe_results[i].is_set ? color_done : ((mask & highlight_flags[i]) ? color_active : color_pending);
        line_dsc.color = current_color;
        line_dsc.p1.x = path_starts[i].x; line_dsc.p1.y = path_starts[i].y;
        line_dsc.p2.x = points[i].x; line_dsc.p2.y = points[i].y;
        lv_draw_line(layer, &line_dsc);

        rect_dsc.bg_color = current_color;
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        rect_dsc.border_width = 0;
        lv_area_t point_area = {points[i].x - 4, points[i].y - 4, points[i].x + 4, points[i].y + 4};
        lv_draw_rect(layer, &rect_dsc, &point_area);
    }
}

static void canvas_click_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz || wiz->current_action->type != ACTION_SELECT_CORNER) return;

    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);

    lv_area_t canvas_area;
    lv_obj_get_coords(wiz->canvas, &canvas_area);

    lv_coord_t touch_area_size = 40;
    lv_area_t bl = {canvas_area.x1, canvas_area.y1, canvas_area.x1 + touch_area_size, canvas_area.y1 + touch_area_size};
    lv_area_t br = {canvas_area.x2 - touch_area_size, canvas_area.y1, canvas_area.x2, canvas_area.y1 + touch_area_size};
    lv_area_t fl = {canvas_area.x1, canvas_area.y2 - touch_area_size, canvas_area.x1 + touch_area_size, canvas_area.y2};
    lv_area_t fr = {canvas_area.x2 - touch_area_size, canvas_area.y2 - touch_area_size, canvas_area.x2, canvas_area.y2};

    if(lv_area_is_point_on(&bl, &p, 0)) wiz->corner_type = LV_PROBING_CORNER_BACK_LEFT;
    else if(lv_area_is_point_on(&br, &p, 0)) wiz->corner_type = LV_PROBING_CORNER_BACK_RIGHT;
    else if(lv_area_is_point_on(&fl, &p, 0)) wiz->corner_type = LV_PROBING_CORNER_FRONT_LEFT;
    else if(lv_area_is_point_on(&fr, &p, 0)) wiz->corner_type = LV_PROBING_CORNER_FRONT_RIGHT;
    else return;

    lv_probing_wizard_advance_step(obj);
}
