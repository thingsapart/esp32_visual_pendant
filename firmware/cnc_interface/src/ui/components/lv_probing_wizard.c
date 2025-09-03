#include "lv_probing_wizard.h"
#include <math.h>
#include <string.h>
#include "misc/lv_math.h"
#include "ui/assets.h"

// Defines for configurable drawing parameters
#define PROBING_WIZARD_CROSSHAIR_SIZE 20
#define PROBING_WIZARD_CROSSHAIR_WIDTH 2
#define PROBING_WIZARD_DASH_LINE_LEN_PCT 25 // As a percentage of min(width, height)
#define PROBING_WIZARD_DASH_LINE_WIDTH 2
#define PROBING_WIZARD_PAD_PCT 5 // Additional padding percentage

// Your debugging toggles can remain here if you still need them
#define PROBING_WIZARD_ENABLE_LEFT_PANEL
#define PROBING_WIZARD_ENABLE_CANVAS
#define PROBING_WIZARD_ENABLE_STEPS_LIST
#define PROBING_WIZARD_ENABLE_WCS_SELECTOR
#define PROBING_WIZARD_ENABLE_RESULTS_DISPLAY

// ... (struct and function prototypes are unchanged) ...
#define MAX_PROBE_POINTS 4
#define MAX_STEP_LABELS 4

typedef struct {
    float x;
    float y;
    bool is_set;
} probe_point_t;

typedef struct {
    lv_obj_t * canvas;
    lv_obj_t * instruction_label;
    lv_obj_t * steps_container;
    lv_obj_t * step_item_containers[MAX_STEP_LABELS];
    lv_obj_t * step_num_labels[MAX_STEP_LABELS];
    lv_obj_t * step_text_labels[MAX_STEP_LABELS];
    lv_obj_t * result_label_x;
    lv_obj_t * result_label_y;
    lv_obj_t * mode_btnm;
    lv_obj_t * variant_btnm;
    lv_probing_wizard_mode_t mode;
    bool is_inside;
    lv_probing_wizard_phase_t phase;
    lv_probing_wizard_corner_t corner_type;
    uint8_t active_step;
    uint8_t num_steps;
    probe_point_t points[MAX_PROBE_POINTS];
    lv_probing_wizard_point_float_t result;
    bool result_valid;
    char instruction_text[128];
    char step_label_texts[MAX_STEP_LABELS][64];
    char result_label_x_text[32];
    char result_label_y_text[32];
    char step_num_texts[MAX_STEP_LABELS][4];
} lv_probing_wizard_t;

// Probing instructions
static const char * rect_probe_instructions[] = {
    "Probe negative X surface for the first point (PX1).",
    "Probe positive X surface for the second point (PX2).",
    "Probe positive Y surface for the first point (PY1).",
    "Probe negative Y surface for the second point (PY2)."
};
static const char * circle_probe_instructions[] = {
    "Probe the first point on the circumference.",
    "Probe the second point on the circumference.",
    "Probe the third point on the circumference."
};
static const char * corner_probe_instructions[] = {
    "Probe the first surface to set the X coordinate.",
    "Probe the second surface to set the Y coordinate."
};
static const char * probe_complete_instruction = "Probing complete. Result is calculated.";
static const char * probe_setup_instruction = "Configure probing options and start the cycle.";

static void wizard_destructor(lv_event_t * e);
static void draw_event_cb(lv_event_t * e);
static void rebuild_ui(lv_obj_t * obj);
static void update_ui_elements(lv_obj_t * obj);
static void calculate_result(lv_obj_t * obj);
static void draw_rectangle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static void draw_circle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static void draw_corner_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static void create_mode_selectors(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_steps_list(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_wcs_selector(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_results_display(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_left_panel(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_canvas(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void mode_selector_event_cb(lv_event_t * e);


/***************************************************
 * LAYOUT FIX IS IN THESE TWO FUNCTIONS
 ***************************************************/

static void create_left_panel(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    #ifdef PROBING_WIZARD_ENABLE_LEFT_PANEL
    lv_obj_t * left_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(left_panel);

    // --- KEY CHANGE ---
    // Set the base width, but do NOT set flex grow. This panel should be a fixed size.
    lv_obj_set_width(left_panel, 220);
    lv_obj_set_height(left_panel, lv_pct(100));

    lv_obj_set_layout(left_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left_panel, 15, 0);

    create_mode_selectors(left_panel, wiz);

    #ifdef PROBING_WIZARD_ENABLE_STEPS_LIST
    create_steps_list(left_panel, wiz);
    #endif

    #ifdef PROBING_WIZARD_ENABLE_WCS_SELECTOR
    create_wcs_selector(left_panel, wiz);
    #endif

    #ifdef PROBING_WIZARD_ENABLE_RESULTS_DISPLAY
    create_results_display(left_panel, wiz);
    #endif
    #endif
}

static void create_canvas(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    #ifdef PROBING_WIZARD_ENABLE_CANVAS
    wiz->canvas = lv_obj_create(parent);
    lv_obj_remove_style_all(wiz->canvas);

    // --- KEY CHANGE ---
    // Remove all explicit sizing. Let the flex layout control the size completely.
    // The flex_grow=1 property tells it to fill the remaining horizontal space.
    lv_obj_set_flex_grow(wiz->canvas, 1);
    lv_obj_set_size(wiz->canvas, LV_PCT(100), LV_PCT(100));
    lv_obj_set_align(wiz->canvas, LV_ALIGN_CENTER);

    lv_obj_add_event_cb(wiz->canvas, draw_event_cb, LV_EVENT_DRAW_POST, wiz);
    lv_obj_set_style_bg_color(wiz->canvas, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(wiz->canvas, 8, 0);
    lv_obj_set_style_bg_opa(wiz->canvas, LV_OPA_COVER, 0);
    #endif
}

static void mode_selector_event_cb(lv_event_t * e) {
    lv_obj_t * btnm = lv_event_get_target(e);
    lv_obj_t * wizard_obj = lv_event_get_user_data(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(wizard_obj);
    if(!wiz) return;

    uint32_t id = lv_btnmatrix_get_selected_btn(btnm);
    if (id == LV_BTNMATRIX_BTN_NONE) return;

    if (btnm == wiz->mode_btnm) {
        // Map ID back to mode enum. Button map order MUST match enum order.
        lv_probing_wizard_mode_t new_mode = (lv_probing_wizard_mode_t)id;
        if (new_mode != wiz->mode) {
            lv_probing_wizard_set_mode(wizard_obj, new_mode, wiz->is_inside);
        }
    } else if (btnm == wiz->variant_btnm) {
        // ID 0 is "Inside", 1 is "Outside"
        bool new_is_inside = (id == 0);
        if (new_is_inside != wiz->is_inside) {
            lv_probing_wizard_set_mode(wizard_obj, wiz->mode, new_is_inside);
        }
    }
}

static void create_mode_selectors(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    lv_obj_t* wizard_obj = lv_obj_get_parent(lv_obj_get_parent(parent)); // this is hacky, but gets us the main container

    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_width(cont, lv_pct(100));
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(cont, 5, 0);

    // Mode: Block, Circular, Corner
    static const char * mode_map[] = {"Block", "Circular", "Corner", ""};
    wiz->mode_btnm = lv_btnmatrix_create(cont);
    lv_btnmatrix_set_map(wiz->mode_btnm, mode_map);
    lv_btnmatrix_set_btn_ctrl_all(wiz->mode_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(wiz->mode_btnm, true);
    lv_obj_set_style_pad_all(wiz->mode_btnm, 2, 0);
    lv_obj_set_size(wiz->mode_btnm, lv_pct(100), 60);
    lv_obj_add_event_cb(wiz->mode_btnm, mode_selector_event_cb, LV_EVENT_VALUE_CHANGED, wizard_obj);
    lv_obj_set_style_text_font(wiz->mode_btnm, &lv_font_montserrat_14, 0);

    // Variant: Inside, Outside
    static const char * variant_map[] = {"Inside", "Outside", ""};
    wiz->variant_btnm = lv_btnmatrix_create(cont);
    lv_btnmatrix_set_map(wiz->variant_btnm, variant_map);
    lv_btnmatrix_set_btn_ctrl_all(wiz->variant_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(wiz->variant_btnm, true);
    lv_obj_set_size(wiz->variant_btnm, lv_pct(100), 60);
    lv_obj_set_style_pad_all(wiz->variant_btnm, 2, 0);
    lv_obj_add_event_cb(wiz->variant_btnm, mode_selector_event_cb, LV_EVENT_VALUE_CHANGED, wizard_obj);
    lv_obj_set_style_text_font(wiz->variant_btnm, &lv_font_montserrat_14, 0);
}

static void create_steps_list(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    #if defined(PROBING_WIZARD_ENABLE_LEFT_PANEL) && defined(PROBING_WIZARD_ENABLE_STEPS_LIST)
    wiz->steps_container = lv_obj_create(parent);
    lv_obj_remove_style_all(wiz->steps_container);
    lv_obj_set_width(wiz->steps_container, lv_pct(100));
    lv_obj_set_flex_grow(wiz->steps_container, 1);
    lv_obj_set_layout(wiz->steps_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wiz->steps_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wiz->steps_container, 5, 0);
    #endif
}

static void create_wcs_selector(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    #if defined(PROBING_WIZARD_ENABLE_LEFT_PANEL) && defined(PROBING_WIZARD_ENABLE_WCS_SELECTOR)
    lv_obj_t * gcs_cont = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(gcs_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gcs_cont, 0, 0);
    lv_obj_set_style_pad_all(gcs_cont, 0, 0);
    lv_obj_set_size(gcs_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(gcs_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gcs_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(gcs_cont, 5, 0);

    lv_obj_t* gcs_label = lv_label_create(gcs_cont);
    lv_label_set_text_static(gcs_label, "Workpiece Coordinate");
    lv_obj_set_style_text_color(gcs_label, lv_color_hex(0x888888), 0);

    static const char * btnm_map[] = {"G54", "G55", "G56", "\n", "G57", "G58", "G59", ""};
    lv_obj_t * btnm = lv_btnmatrix_create(gcs_cont);
    lv_btnmatrix_set_map(btnm, btnm_map);
    lv_obj_set_style_pad_all(btnm, 2, 0);
    lv_btnmatrix_set_btn_ctrl_all(btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(btnm, true);
    lv_obj_set_size(btnm, lv_pct(100), 120);
    lv_obj_add_flag(btnm, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_text_font(btnm, &lv_font_montserrat_14, 0);
    #endif
}

static void create_results_display(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    #if defined(PROBING_WIZARD_ENABLE_LEFT_PANEL) && defined(PROBING_WIZARD_ENABLE_RESULTS_DISPLAY)
    lv_obj_t * res_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(res_cont);
    lv_obj_set_width(res_cont, lv_pct(100));
    lv_obj_set_layout(res_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(res_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(res_cont, 5, 0);
    lv_obj_t* res_label = lv_label_create(res_cont);
    lv_label_set_text_static(res_label, "Probing Result");
    lv_obj_set_style_text_color(res_label, lv_color_hex(0x888888), 0);
    wiz->result_label_x = lv_label_create(res_cont);
    wiz->result_label_y = lv_label_create(res_cont);
    lv_obj_set_style_text_font(wiz->result_label_x, lv_theme_get_font_large(parent), 0);
    lv_obj_set_style_text_font(wiz->result_label_y, lv_theme_get_font_large(parent), 0);
    lv_obj_set_style_text_color(wiz->result_label_x, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_text_color(wiz->result_label_y, lv_color_hex(0x007AFF), 0);
    #endif
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

    // Main container is a column
    lv_obj_set_layout(main_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(main_container, 10, 0);
    lv_obj_set_style_pad_gap(main_container, 10, 0);

    // 1. Instruction Label at the top
    wiz->instruction_label = lv_label_create(main_container);
    lv_obj_set_width(wiz->instruction_label, lv_pct(100));
    lv_obj_set_height(wiz->instruction_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(wiz->instruction_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(wiz->instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(wiz->instruction_label, lv_theme_get_font_large(parent), 0);
    lv_label_set_text_static(wiz->instruction_label, "Initializing...");

    // 2. Content container for the rest of the UI (left panel + canvas)
    lv_obj_t * content_container = lv_obj_create(main_container);
    lv_obj_remove_style_all(content_container);
    lv_obj_set_width(content_container, lv_pct(100));
    lv_obj_set_flex_grow(content_container, 1);
    lv_obj_set_layout(content_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(content_container, 10, 0);

    create_left_panel(content_container, wiz);
    create_canvas(content_container, wiz);

    wiz->mode = LV_PROBING_WIZARD_MODE_RECTANGLE;
    wiz->phase = LV_PROBING_WIZARD_PHASE_PROBING;
    wiz->is_inside = false;
    rebuild_ui(main_container);
    return main_container;
}

void lv_probing_wizard_set_mode(lv_obj_t * obj, lv_probing_wizard_mode_t mode, bool is_inside) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    if (wiz->mode == mode && wiz->is_inside == is_inside) return;
    wiz->mode = mode;
    wiz->is_inside = is_inside;
    lv_probing_wizard_clear_points(obj);
    rebuild_ui(obj);
}

void lv_probing_wizard_set_phase(lv_obj_t * obj, lv_probing_wizard_phase_t phase) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    if (wiz->phase == phase) return;
    wiz->phase = phase;
    rebuild_ui(obj);
}

void lv_probing_wizard_set_corner_type(lv_obj_t * obj, lv_probing_wizard_corner_t corner) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    if (wiz->mode != LV_PROBING_WIZARD_MODE_CORNER || wiz->corner_type == corner) return;
    wiz->corner_type = corner;
    if(wiz->canvas) lv_obj_invalidate(wiz->canvas);
}

void lv_probing_wizard_set_active_step(lv_obj_t * obj, uint8_t step_index) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    if (step_index >= wiz->num_steps) return;
    wiz->active_step = step_index;
    update_ui_elements(obj);
}

void lv_probing_wizard_set_point_value(lv_obj_t * obj, uint8_t point_index, float x, float y) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    if (point_index >= wiz->num_steps) return;
    wiz->points[point_index].x = x;
    wiz->points[point_index].y = y;
    wiz->points[point_index].is_set = true;
    calculate_result(obj);
    update_ui_elements(obj);
}

void lv_probing_wizard_clear_points(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    for (int i = 0; i < MAX_PROBE_POINTS; i++) {
        wiz->points[i].is_set = false;
    }
    wiz->result_valid = false;
    update_ui_elements(obj);
}

lv_probing_wizard_point_float_t lv_probing_wizard_get_result(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (wiz && wiz->result_valid) {
        return wiz->result;
    }
    return (lv_probing_wizard_point_float_t){0.0f, 0.0f};
}

static void wizard_destructor(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (wiz) {
        lv_free(wiz);
    }
}

static void rebuild_ui(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    #if defined(PROBING_WIZARD_ENABLE_LEFT_PANEL) && defined(PROBING_WIZARD_ENABLE_STEPS_LIST)
    if(!wiz->steps_container) return;
    uint32_t child_cnt = lv_obj_get_child_cnt(wiz->steps_container);
    for (int32_t i = child_cnt - 1; i >= 0; i--) {
        lv_obj_t * child = lv_obj_get_child(wiz->steps_container, i);
        lv_obj_del(child);
    }
    for(int i=0; i<MAX_STEP_LABELS; i++) {
        wiz->step_item_containers[i] = NULL;
        wiz->step_num_labels[i] = NULL;
        wiz->step_text_labels[i] = NULL;
    }
    const char * step_texts[MAX_STEP_LABELS];
    switch (wiz->mode) {
        case LV_PROBING_WIZARD_MODE_RECTANGLE:
            wiz->num_steps = 4;
            step_texts[0] = "Set PX1 X="; step_texts[1] = "Set PX2 X=";
            step_texts[2] = "Set PY1 Y="; step_texts[3] = "Set PY2 Y=";
            break;
        case LV_PROBING_WIZARD_MODE_CIRCLE:
            wiz->num_steps = 3;
            step_texts[0] = "Set P1"; step_texts[1] = "Set P2"; step_texts[2] = "Set P3";
            break;
        case LV_PROBING_WIZARD_MODE_CORNER:
            wiz->num_steps = 2;
            step_texts[0] = "Set X"; step_texts[1] = "Set Y";
            break;
    }
    for (uint8_t i = 0; i < wiz->num_steps; i++) {
        lv_obj_t* cont = lv_obj_create(wiz->steps_container);
        lv_obj_remove_style_all(cont);
        lv_obj_set_size(cont, lv_pct(100), 36);
        lv_obj_set_style_radius(cont, 5, 0);
        lv_obj_set_style_bg_color(cont, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(cont, 10, 0);
        lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        wiz->step_item_containers[i] = cont;
        lv_obj_t* num_label = lv_label_create(cont);
        snprintf(wiz->step_num_texts[i], sizeof(wiz->step_num_texts[i]), "%d", i + 1);
        lv_label_set_text(num_label, wiz->step_num_texts[i]);
        wiz->step_num_labels[i] = num_label;
        lv_obj_t* text_label = lv_label_create(cont);
        lv_label_set_text_static(text_label, step_texts[i]);
        lv_obj_set_flex_grow(text_label, 1);
        wiz->step_text_labels[i] = text_label;
    }
    wiz->active_step = 0;
    lv_probing_wizard_clear_points(obj);
    #endif
}

static void update_ui_elements(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;

    // Update mode selector buttons
    if (wiz->mode_btnm) {
        lv_btnmatrix_set_btn_ctrl(wiz->mode_btnm, (uint16_t)wiz->mode, LV_BTNMATRIX_CTRL_CHECKED);
    }
    if (wiz->variant_btnm) {
        lv_btnmatrix_set_btn_ctrl(wiz->variant_btnm, wiz->is_inside ? 0 : 1, LV_BTNMATRIX_CTRL_CHECKED);
    }

    // Update instruction label
    if(wiz->instruction_label) {
        const char * instruction = probe_setup_instruction;
        if(wiz->phase == LV_PROBING_WIZARD_PHASE_PROBING) {
            if(wiz->result_valid) {
                instruction = probe_complete_instruction;
            } else {
                switch(wiz->mode) {
                    case LV_PROBING_WIZARD_MODE_RECTANGLE: instruction = rect_probe_instructions[wiz->active_step]; break;
                    case LV_PROBING_WIZARD_MODE_CIRCLE:   instruction = circle_probe_instructions[wiz->active_step]; break;
                    case LV_PROBING_WIZARD_MODE_CORNER:   instruction = corner_probe_instructions[wiz->active_step]; break;
                }
            }
        }
        snprintf(wiz->instruction_text, sizeof(wiz->instruction_text), "%s", instruction);
        lv_label_set_text(wiz->instruction_label, wiz->instruction_text);
    }

    #if defined(PROBING_WIZARD_ENABLE_LEFT_PANEL) && defined(PROBING_WIZARD_ENABLE_STEPS_LIST)
    if(wiz->steps_container) {
        const char * step_texts[MAX_STEP_LABELS] = {0};
        switch (wiz->mode) {
            case LV_PROBING_WIZARD_MODE_RECTANGLE:
                step_texts[0] = "Set PX1 X="; step_texts[1] = "Set PX2 X=";
                step_texts[2] = "Set PY1 Y="; step_texts[3] = "Set PY2 Y=";
                break;
            case LV_PROBING_WIZARD_MODE_CIRCLE:
                step_texts[0] = "Set P1"; step_texts[1] = "Set P2"; step_texts[2] = "Set P3";
                break;
            case LV_PROBING_WIZARD_MODE_CORNER:
                step_texts[0] = "Set X"; step_texts[1] = "Set Y";
                break;
        }
        for (uint8_t i = 0; i < wiz->num_steps; i++) {
            lv_obj_t* cont = wiz->step_item_containers[i];
            if(!cont) continue;
            if(i == wiz->active_step) {
                lv_obj_set_style_border_width(cont, 2, 0);
                lv_obj_set_style_border_color(cont, lv_color_hex(0xFF9500), 0);
            } else {
                lv_obj_set_style_border_width(cont, 0, 0);
            }
            if (wiz->points[i].is_set) {
                if (wiz->mode == LV_PROBING_WIZARD_MODE_RECTANGLE) {
                    if (i < 2) snprintf(wiz->step_label_texts[i], sizeof(wiz->step_label_texts[i]), "%s  %.2f", step_texts[i], wiz->points[i].x);
                    else snprintf(wiz->step_label_texts[i], sizeof(wiz->step_label_texts[i]), "%s  %.2f", step_texts[i], wiz->points[i].y);
                } else snprintf(wiz->step_label_texts[i], sizeof(wiz->step_label_texts[i]), "%s (%.1f, %.1f)", step_texts[i], wiz->points[i].x, wiz->points[i].y);
                lv_label_set_text(wiz->step_text_labels[i], wiz->step_label_texts[i]);
            } else lv_label_set_text_static(wiz->step_text_labels[i], step_texts[i]);
        }
    }
    #endif
    #if defined(PROBING_WIZARD_ENABLE_LEFT_PANEL) && defined(PROBING_WIZARD_ENABLE_RESULTS_DISPLAY)
    if(wiz->result_label_x) {
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
    #endif
    #ifdef PROBING_WIZARD_ENABLE_CANVAS
    if(wiz->canvas) lv_obj_invalidate(wiz->canvas);
    #endif
}

static void calculate_result(lv_obj_t* obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    for (uint8_t i = 0; i < wiz->num_steps; i++) {
        if (!wiz->points[i].is_set) {
            wiz->result_valid = false;
            return;
        }
    }
    switch (wiz->mode) {
        case LV_PROBING_WIZARD_MODE_RECTANGLE:
            wiz->result.x = (wiz->points[0].x + wiz->points[1].x) / 2.0f;
            wiz->result.y = (wiz->points[2].y + wiz->points[3].y) / 2.0f;
            break;
        case LV_PROBING_WIZARD_MODE_CIRCLE:
             wiz->result.x = (wiz->points[0].x + wiz->points[1].x + wiz->points[2].x) / 3.0f;
             wiz->result.y = (wiz->points[0].y + wiz->points[1].y + wiz->points[2].y) / 3.0f;
            break;
        case LV_PROBING_WIZARD_MODE_CORNER:
            wiz->result.x = wiz->points[0].x;
            wiz->result.y = wiz->points[1].y;
            break;
    }
    wiz->result_valid = true;
}

/***************************************************
 * DRAWING SUB-FUNCTIONS
 ***************************************************/

/**
 * @brief Draws the main workpiece shape (a rectangle).
 */
static inline void draw_workpiece_rectangle(lv_layer_t * layer, const lv_area_t * draw_area, lv_color_t color) {
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.border_width = 2;              // Thickness of the rectangle outline.
    rect_dsc.border_color = color;          // Color of the outline.
    rect_dsc.bg_opa = LV_OPA_TRANSP;        // Make the inside of the rectangle transparent.
    rect_dsc.radius = 5;                    // Slightly rounded corners for the shape.
    lv_draw_rect(layer, &rect_dsc, draw_area); // Draw the rectangle on the canvas layer.
}

/**
 * @brief Draws the main workpiece shape (a circle).
 */
static inline void draw_workpiece_circle(lv_layer_t * layer, const lv_area_t * draw_area, lv_color_t color) {
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = color;
    arc_dsc.width = 2;
    arc_dsc.center = (lv_point_t){(draw_area->x1 + draw_area->x2) / 2, (draw_area->y1 + draw_area->y2) / 2};
    arc_dsc.radius = (LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area))) / 2;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;
    lv_draw_arc(layer, &arc_dsc);
}

/**
 * @brief Draws the main workpiece shape (a corner).
 */
static inline void draw_workpiece_corner(lv_layer_t * layer, lv_point_t corner_pt, lv_point_t h_end, lv_point_t v_end, lv_color_t color) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = 2;

    // Draw horizontal line of the corner.
    line_dsc.p1.x = corner_pt.x;
    line_dsc.p1.y = corner_pt.y;
    line_dsc.p2.x = h_end.x;
    line_dsc.p2.y = h_end.y;
    lv_draw_line(layer, &line_dsc);

    // Draw vertical line of the corner.
    line_dsc.p2.x = v_end.x;
    line_dsc.p2.y = v_end.y;
    lv_draw_line(layer, &line_dsc);
}

/**
 * @brief Draws a small crosshair at the center of the drawing area.
 */
static inline void draw_center_crosshair(lv_layer_t * layer, const lv_area_t * draw_area, lv_color_t color) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = 1;

    lv_point_t center = {(draw_area->x1 + draw_area->x2) / 2, (draw_area->y1 + draw_area->y2) / 2};
    lv_coord_t size = PROBING_WIZARD_CROSSHAIR_SIZE / 2;

    // Draw horizontal line of the crosshair.
    line_dsc.p1.x = center.x - size;
    line_dsc.p1.y = center.y;
    line_dsc.p2.x = center.x + size;
    line_dsc.p2.y = center.y;
    lv_draw_line(layer, &line_dsc);

    // Draw vertical line of the crosshair.
    line_dsc.p1.x = center.x;
    line_dsc.p1.y = center.y - size;
    line_dsc.p2.x = center.x;
    line_dsc.p2.y = center.y + size;
    lv_draw_line(layer, &line_dsc);
}

/**
 * @brief Draws a crosshair at a specific point to indicate the final calculated result.
 */
static inline void draw_result_crosshair(lv_layer_t * layer, lv_point_t center, lv_color_t color) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = PROBING_WIZARD_CROSSHAIR_WIDTH;
    lv_coord_t size = PROBING_WIZARD_CROSSHAIR_SIZE / 2;

    // Draw horizontal line of the result crosshair.
    line_dsc.p1.x = center.x - size;
    line_dsc.p1.y = center.y;
    line_dsc.p2.x = center.x + size;
    line_dsc.p2.y = center.y;
    lv_draw_line(layer, &line_dsc);

    // Draw vertical line of the result crosshair.
    line_dsc.p1.x = center.x;
    line_dsc.p1.y = center.y - size;
    line_dsc.p2.x = center.x;
    line_dsc.p2.y = center.y + size;
    lv_draw_line(layer, &line_dsc);
}


static void draw_event_cb(lv_event_t * e) {
    #ifdef PROBING_WIZARD_ENABLE_CANVAS
    lv_obj_t * obj = lv_event_get_target(e);
    lv_probing_wizard_t * wiz = lv_event_get_user_data(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t canvas_area;
    lv_obj_get_coords(obj, &canvas_area);

    // Create a smaller, padded drawing area inside the canvas.
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
    #endif
}

static void draw_rectangle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area) {
    #ifdef PROBING_WIZARD_ENABLE_CANVAS
    lv_color_t color_active = lv_color_hex(0xFF9500);
    lv_color_t color_done = lv_color_hex(0x888888);
    lv_color_t color_pending = lv_color_white();
    lv_color_t color_result = lv_color_hex(0x007AFF);

    // Create a square area for the workpiece, centered in the draw_area
    lv_area_t square_area;
    lv_coord_t side = LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area));
    lv_area_set_width(&square_area, side);
    lv_area_set_height(&square_area, side);
    lv_area_align(draw_area, &square_area, LV_ALIGN_CENTER, 0, 0);

    // 1. Draw the main shape (square) and center crosshair
    draw_workpiece_rectangle(layer, &square_area, color_pending);
    draw_center_crosshair(layer, &square_area, color_pending);

    // 2. Prepare to draw probe paths
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = PROBING_WIZARD_DASH_LINE_WIDTH;
    line_dsc.dash_width = 6;
    line_dsc.dash_gap = 4;

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);

    // Symbolic points on the canvas for the 4 probe locations
    lv_point_t points[4] = {
        {square_area.x1, (square_area.y1 + square_area.y2) / 2}, // Left
        {square_area.x2, (square_area.y1 + square_area.y2) / 2}, // Right
        {(square_area.x1 + square_area.x2) / 2, square_area.y2}, // Bottom
        {(square_area.x1 + square_area.x2) / 2, square_area.y1}, // Top
    };

    // Calculate the length of the dashed approach lines
    int32_t min_dim = LV_MIN(lv_area_get_width(&square_area), lv_area_get_height(&square_area));
    int path_len = (min_dim * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;

    // Define start points for the dashed lines based on probing direction (inside/outside)
    lv_point_t path_starts[4] = {
        {points[0].x + (wiz->is_inside ? path_len : -path_len), points[0].y},
        {points[1].x + (wiz->is_inside ? -path_len : path_len), points[1].y},
        {points[2].x, points[2].y + (wiz->is_inside ? -path_len : path_len)},
        {points[3].x, points[3].y + (wiz->is_inside ? path_len : -path_len)},
    };

    // 3. Draw each probe path and point indicator
    for(int i=0; i < 4; i++) {
        lv_color_t current_color = wiz->points[i].is_set ? color_done : (i == wiz->active_step ? color_active : color_pending);

        // Dashed line representing the probe path
        line_dsc.color = current_color;
        line_dsc.p1.x = path_starts[i].x;
        line_dsc.p1.y = path_starts[i].y;
        line_dsc.p2.x = points[i].x;
        line_dsc.p2.y = points[i].y;
        lv_draw_line(layer, &line_dsc);

        // Circle representing the probe point
        rect_dsc.bg_color = current_color;
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        lv_area_t point_area = {points[i].x - 4, points[i].y - 4, points[i].x + 4, points[i].y + 4};
        lv_draw_rect(layer, &rect_dsc, &point_area);
    }

    // 4. If all points are set, draw the result crosshair at the center
    if(wiz->result_valid) {
        lv_point_t center = {(square_area.x1 + square_area.x2) / 2, (square_area.y1 + square_area.y2) / 2};
        draw_result_crosshair(layer, center, color_result);
    }
    #endif
}

static void draw_circle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area) {
    #ifdef PROBING_WIZARD_ENABLE_CANVAS
    lv_color_t color_active = lv_color_hex(0xFF9500);
    lv_color_t color_done = lv_color_hex(0x888888);
    lv_color_t color_pending = lv_color_white();
    lv_color_t color_result = lv_color_hex(0x007AFF);

    // 1. Draw the main shape (circle) and center crosshair
    draw_workpiece_circle(layer, draw_area, color_pending);
    draw_center_crosshair(layer, draw_area, color_pending);

    // 2. Prepare to draw probe paths
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = PROBING_WIZARD_DASH_LINE_WIDTH;
    line_dsc.dash_width = 6;
    line_dsc.dash_gap = 4;

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);

    lv_point_t center = {(draw_area->x1 + draw_area->x2) / 2, (draw_area->y1 + draw_area->y2) / 2};
    int32_t radius = (LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area))) / 2;
    int32_t min_dim = LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area));
    int path_len = (min_dim * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;

    float angles[] = { -90, 150, 30 };
    lv_point_t points[3];
    lv_point_t path_starts[3];

    // 3. Draw each probe path and point indicator
    for(int i=0; i<3; i++) {
        float rad = angles[i] * (M_PI / 180.0f);
        points[i].x = center.x + radius * cosf(rad);
        points[i].y = center.y + radius * sinf(rad);

        int path_radius = radius + (wiz->is_inside ? -path_len : path_len);
        path_starts[i].x = center.x + path_radius * cosf(rad);
        path_starts[i].y = center.y + path_radius * sinf(rad);

        lv_color_t current_color = wiz->points[i].is_set ? color_done : (i == wiz->active_step ? color_active : color_pending);
        line_dsc.color = current_color;
        line_dsc.p1.x = path_starts[i].x;
        line_dsc.p1.y = path_starts[i].y;
        line_dsc.p2.x = points[i].x;
        line_dsc.p2.y = points[i].y;
        lv_draw_line(layer, &line_dsc);

        rect_dsc.bg_color = current_color;
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        lv_area_t point_area = {points[i].x - 4, points[i].y - 4, points[i].x + 4, points[i].y + 4};
        lv_draw_rect(layer, &rect_dsc, &point_area);
    }

    // 4. If all points are set, draw the result crosshair at the center
    if(wiz->result_valid) {
        draw_result_crosshair(layer, center, color_result);
    }
    #endif
}

static void draw_corner_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area) {
    #ifdef PROBING_WIZARD_ENABLE_CANVAS
    lv_color_t color_active = lv_color_hex(0xFF9500);
    lv_color_t color_done = lv_color_hex(0x888888);
    lv_color_t color_pending = lv_color_white();
    lv_color_t color_result = lv_color_hex(0x007AFF);

    lv_point_t corner_pt, h_end, v_end;
    lv_point_t points[2];
    lv_point_t path_starts[2];
    lv_coord_t center_x = (draw_area->x1 + draw_area->x2) / 2;
    lv_coord_t center_y = (draw_area->y1 + draw_area->y2) / 2;

    // Determine corner points based on selected type
    switch (wiz->corner_type) {
        case LV_PROBING_CORNER_FRONT_LEFT:
            corner_pt = (lv_point_t){draw_area->x2, draw_area->y2};
            h_end = (lv_point_t){draw_area->x1, draw_area->y2};
            v_end = (lv_point_t){draw_area->x2, draw_area->y1};
            break;
        case LV_PROBING_CORNER_FRONT_RIGHT:
            corner_pt = (lv_point_t){draw_area->x1, draw_area->y2};
            h_end = (lv_point_t){draw_area->x2, draw_area->y2};
            v_end = (lv_point_t){draw_area->x1, draw_area->y1};
            break;
        case LV_PROBING_CORNER_BACK_LEFT:
            corner_pt = (lv_point_t){draw_area->x2, draw_area->y1};
            h_end = (lv_point_t){draw_area->x1, draw_area->y1};
            v_end = (lv_point_t){draw_area->x2, draw_area->y2};
            break;
        case LV_PROBING_CORNER_BACK_RIGHT:
        default:
            corner_pt = (lv_point_t){draw_area->x1, draw_area->y1};
            h_end = (lv_point_t){draw_area->x2, draw_area->y1};
            v_end = (lv_point_t){draw_area->x1, draw_area->y2};
            break;
    }

    // Probe points are on the corner lines
    points[0] = (lv_point_t){center_x, corner_pt.y}; // X probe point
    points[1] = (lv_point_t){corner_pt.x, center_y}; // Y probe point

    // 1. Draw the main shape (corner)
    draw_workpiece_corner(layer, corner_pt, h_end, v_end, color_pending);

    // 2. Prepare to draw probe paths
    int32_t min_dim = LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area));
    int path_len = (min_dim * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = PROBING_WIZARD_DASH_LINE_WIDTH;
    line_dsc.dash_width = 6;
    line_dsc.dash_gap = 4;

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);

    // Path start points depend on direction
    path_starts[0] = (lv_point_t){points[0].x + (wiz->is_inside ? (points[0].x > corner_pt.x ? -path_len : path_len) : (points[0].x < corner_pt.x ? path_len : -path_len)), points[0].y};
    path_starts[1] = (lv_point_t){points[1].x, points[1].y + (wiz->is_inside ? (points[1].y > corner_pt.y ? -path_len : path_len) : (points[1].y < corner_pt.y ? path_len : -path_len))};

    // 3. Draw each probe path and point indicator
    for(int i=0; i<2; i++) {
        lv_color_t current_color = wiz->points[i].is_set ? color_done : (i == wiz->active_step ? color_active : color_pending);
        line_dsc.color = current_color;
        line_dsc.p1.x = path_starts[i].x;
        line_dsc.p1.y = path_starts[i].y;
        line_dsc.p2.x = points[i].x;
        line_dsc.p2.y = points[i].y;
        lv_draw_line(layer, &line_dsc);

        rect_dsc.bg_color = current_color;
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        lv_area_t point_area = {points[i].x - 4, points[i].y - 4, points[i].x + 4, points[i].y + 4};
        lv_draw_rect(layer, &rect_dsc, &point_area);
    }

    // 4. If all points are set, draw the result crosshair at the corner
    if(wiz->result_valid) {
        draw_result_crosshair(layer, corner_pt, color_result);
    }
    #endif
}