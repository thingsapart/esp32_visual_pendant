#define UI_DEBUG_LOCAL_LEVEL D_WARN
#include "debug.h"

#include "lv_probing_wizard.h"
#include <math.h>
#include <string.h>
#include "misc/lv_math.h"
#include "lv_probing_wizard_stubs.h"

#include "misc/lv_area_private.h"

static const char * TAG = "probing-wizard";

// Defines for configurable drawing parameters
#define PROBING_WIZARD_CROSSHAIR_SIZE 20
#define PROBING_WIZARD_CROSSHAIR_WIDTH 5
#define PROBING_WIZARD_DASH_LINE_LEN_PCT 25 // As a percentage of min(width, height)
#define PROBING_WIZARD_DASH_LINE_WIDTH 2
#define PROBING_WIZARD_PAD_PCT -5 // Additional padding percentage
#define CIRCULAR_PROBE_DASHED_LINES 1 // Emulate dashed lines for circle probe, as LVGL only supports horiz/vert dashed lines

#define MAX_SETUP_POINTS 2
#define MAX_PROBE_POINTS 4

// --- "Persistent" Storage for Last Probe Result ---
// In a real application, this should be saved to and loaded from NVS (e.g., EEPROM, Flash).
// For this example, we use static variables to simulate persistence across wizard instances.
static probe_point_t last_probe_result = { .x = 0.0f, .y = 0.0f, .is_set = false };
static float last_probe_z = 0.0f;

// Helper macros for defining probe actions succinctly
#define SETUP_PARAM(i) .param = {.setup_point_index = i}
#define PROBE_PARAM(i) .param = {.probe_index = i}

// --- Probe Routine Definitions ---
// The first step is ACTION_AWAIT_START. The multiple ACTION_PROBE_POINT steps are
// consolidated into one to match the single-macro execution model of the handler.
static const lv_probing_action_t rectangle_probe_actions[] = {
    {.instruction_text = "Select probe mode and variant, then start.", .highlight_mask = HIGHLIGHT_OUTLINE, .type = ACTION_AWAIT_START},
    {.instruction_text = "Jog to the back-left corner of the probing area.", .highlight_mask = HIGHLIGHT_CORNER_BL | HIGHLIGHT_OUTLINE, .type = ACTION_JOG_AND_CONFIRM, SETUP_PARAM(0)},
    {.instruction_text = "Jog to the front-right corner of the probing area.", .highlight_mask = HIGHLIGHT_CORNER_FR | HIGHLIGHT_OUTLINE, .type = ACTION_JOG_AND_CONFIRM, SETUP_PARAM(1)},
    {.instruction_text = "Probe workpiece Z height.", .highlight_mask = HIGHLIGHT_Z_PROBE | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_Z_TOP},
    {.instruction_text = "Probing workpiece...", .highlight_mask = HIGHLIGHT_PROBE_POINT_0 | HIGHLIGHT_PROBE_POINT_1 | HIGHLIGHT_PROBE_POINT_2 | HIGHLIGHT_PROBE_POINT_3 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(0)},
    {.instruction_text = "Probing complete. Result is calculated.", .highlight_mask = HIGHLIGHT_CENTER | HIGHLIGHT_OUTLINE, .type = ACTION_COMPLETE},
};

static const lv_probing_action_t circle_probe_actions[] = {
    {.instruction_text = "Select probe mode and variant, then start.", .highlight_mask = HIGHLIGHT_OUTLINE, .type = ACTION_AWAIT_START},
    {.instruction_text = "Jog roughly to the center of the circle.", .highlight_mask = HIGHLIGHT_CENTER | HIGHLIGHT_OUTLINE, .type = ACTION_JOG_AND_CONFIRM, SETUP_PARAM(0)},
    {.instruction_text = "Jog to the feature's edge (inside for boss, outside for bore).", .highlight_mask = HIGHLIGHT_OUTLINE | HIGHLIGHT_CENTER, .type = ACTION_JOG_AND_CONFIRM, SETUP_PARAM(1)},
    {.instruction_text = "Jog to a clear spot on the top surface and probe Z.", .highlight_mask = HIGHLIGHT_Z_PROBE | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_Z_TOP},
    {.instruction_text = "Probing workpiece...", .highlight_mask = HIGHLIGHT_PROBE_POINT_0 | HIGHLIGHT_PROBE_POINT_1 | HIGHLIGHT_PROBE_POINT_2 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(0)},
    {.instruction_text = "Probing complete. Result is calculated.", .highlight_mask = HIGHLIGHT_CENTER | HIGHLIGHT_OUTLINE, .type = ACTION_COMPLETE},
};

static const lv_probing_action_t corner_probe_actions[] = {
    {.instruction_text = "Select probe mode and variant, then start.", .highlight_mask = HIGHLIGHT_CORNER_BL | HIGHLIGHT_CORNER_BR | HIGHLIGHT_CORNER_FL | HIGHLIGHT_CORNER_FR | HIGHLIGHT_OUTLINE, .type = ACTION_AWAIT_START},
    {.instruction_text = "Click on the corner you wish to probe.", .highlight_mask = HIGHLIGHT_CORNER_BL | HIGHLIGHT_CORNER_BR | HIGHLIGHT_CORNER_FL | HIGHLIGHT_CORNER_FR | HIGHLIGHT_OUTLINE, .type = ACTION_SELECT_CORNER},
    {.instruction_text = "Jog near the selected corner, above the workpiece.", .highlight_mask = HIGHLIGHT_OUTLINE, .type = ACTION_JOG_AND_CONFIRM, SETUP_PARAM(0)},
    {.instruction_text = "Probe workpiece Z height.", .highlight_mask = HIGHLIGHT_Z_PROBE | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_Z_TOP},
    {.instruction_text = "Probing corner...", .highlight_mask = HIGHLIGHT_PROBE_POINT_0 | HIGHLIGHT_PROBE_POINT_1 | HIGHLIGHT_OUTLINE, .type = ACTION_PROBE_POINT, PROBE_PARAM(0)},
    {.instruction_text = "Probing complete. Result is calculated.", .highlight_mask = HIGHLIGHT_OUTLINE, .type = ACTION_COMPLETE},
};

static const lv_probing_action_t * const probe_routines[] = {
    [LV_PROBING_WIZARD_MODE_RECTANGLE] = rectangle_probe_actions,
    [LV_PROBING_WIZARD_MODE_CIRCLE] = circle_probe_actions,
    [LV_PROBING_WIZARD_MODE_CORNER] = corner_probe_actions,
};
const uint8_t probe_routine_sizes[] = {
    [LV_PROBING_WIZARD_MODE_RECTANGLE] = sizeof(rectangle_probe_actions) / sizeof(lv_probing_action_t),
    [LV_PROBING_WIZARD_MODE_CIRCLE] = sizeof(circle_probe_actions) / sizeof(lv_probing_action_t),
    [LV_PROBING_WIZARD_MODE_CORNER] = sizeof(corner_probe_actions) / sizeof(lv_probing_action_t),
};

/**
 * @brief Defines the major UI states of the probing wizard.
 *
 * The wizard progresses through these states to guide the user. The `PROBING` state
 * itself is composed of multiple "steps" defined by the `lv_probing_action_t` arrays.
 *
 * State Machine Flow:
 *
 * (Create/Reset)
 *      |
 *      V
 *  .---------------------------------------------------------------------------------.
 *  | [ WIZARD_STATE_CONFIG ]                                                         |
 *  | - User selects probe mode (Block, Circle, etc.).                                |
 *  | - User can apply a previously saved result.                                     |
 *  '---------------------------------------------------------------------------------'
 *      |
 *      | "Start Probing" button clicked
 *      V
 *  .---------------------------------------------------------------------------------.
 *  | [ WIZARD_STATE_PROBING ]                                                        |  <--.
 *  | - A sequence of steps from the active `probe_actions` array.                    |     | "Next" or
 *  | - User is prompted to jog, confirm points, or wait for automated probing.       |     | deferred_advance_step()
 *  | - Steps are advanced either by user clicking "Next" or by the machine handler   |     |
 *  |   reporting a probe action is complete.                                         | ----'
 *  '---------------------------------------------------------------------------------'
 *      |
 *      | Final `ACTION_COMPLETE` step is reached
 *      V
 *  .---------------------------------------------------------------------------------.
 *  | [ WIZARD_STATE_COMPLETE ]                                                       |
 *  | - Final calculated results (X, Y, Z, dimensions) are displayed.                 |
 *  | - User can select a WCS and click "Apply" to set the new origin.                |
 *  '---------------------------------------------------------------------------------'
 *      |
 *      '------------. "Apply" or "Cancel" button clicked
 *                   V
 *                 (Reset) -> moves back to CONFIG state
 */
typedef enum {
    WIZARD_STATE_CONFIG,    /**< The initial screen for selecting mode and starting. */
    WIZARD_STATE_PROBING,   /**< The active sequence of jogging and probing steps. */
    WIZARD_STATE_COMPLETE,
} wizard_state_t;

typedef enum {
    DEFERRED_ACTION_NONE,
    DEFERRED_ACTION_UPDATE_UI,
    DEFERRED_ACTION_SET_Z_AND_ADVANCE,
    DEFERRED_ACTION_SET_FINAL_AND_ADVANCE,
    DEFERRED_ACTION_ADVANCE_STEP,
    DEFERRED_ACTION_SET_FULL_RESULT_AND_ADVANCE,
} deferred_action_t;

typedef struct {
    lv_obj_t * canvas;
    lv_obj_t * instruction_label;
    lv_obj_t * result_label_x;
    lv_obj_t * result_label_y;
    lv_obj_t * result_label_z;
    lv_obj_t * setup_points_label;

    // Control Buttons
    lv_obj_t * btn_bar;
    lv_obj_t * next_btn;
    lv_obj_t * cancel_btn;
    lv_obj_t * start_btn;
    lv_obj_t * apply_xy_btn;
    lv_obj_t * apply_xyz_btn;
    
    // Left Panel Components
    lv_obj_t * mode_select_panel;
    lv_obj_t * progress_panel;
    lv_obj_t * wcs_select_panel;
    lv_obj_t * mode_btnm;
    lv_obj_t * variant_btnm;
    lv_obj_t * wcs_btnm;
    lv_obj_t * probe_point_labels[MAX_PROBE_POINTS];
    char probe_point_text[MAX_PROBE_POINTS][32];

    // Last Probe Result Display
    lv_obj_t * last_result_panel;
    lv_obj_t * last_result_label_x;
    lv_obj_t * last_result_label_y;
    lv_obj_t * last_result_label_z;
    lv_obj_t * apply_last_xy_btn;
    lv_obj_t * apply_last_xyz_btn;

    lv_probing_wizard_mode_t mode;
    bool is_inside;
    wizard_state_t wizard_state;
    lv_probing_wizard_corner_t corner_type;
    int8_t active_step;
    const lv_probing_action_t * current_action;

    probe_point_t setup_points[MAX_SETUP_POINTS];
    probe_point_t probe_results[MAX_PROBE_POINTS];
    float z_top;
    bool z_top_is_set;

    lv_probing_wizard_point_float_t result;
    bool result_valid;
    lv_probing_wizard_details_t result_details;

    get_current_jogged_position_cb_t get_pos_cb;
    execute_probe_cb_t exec_probe_cb;
    set_wcs_origin_cb_t set_wcs_cb;
    install_probe_tool_cb_t install_probe_cb;
    cancel_probe_cb_t cancel_probe_cb;

    char result_label_x_text[32];
    char result_label_y_text[32];
    char result_dim_w_text[16];
    char result_dim_h_text[16];
    char result_rad_text[16];
    bool machine_connected;

    // --- Deferred Update members for thread safety ---
    lv_timer_t * deferred_update_timer;
    volatile deferred_action_t deferred_action;
    volatile int8_t deferred_next_step;
    volatile float deferred_z_top;
    volatile lv_probing_wizard_point_float_t deferred_final_result;
    volatile lv_probing_wizard_details_t deferred_details;
} lv_probing_wizard_t;

static void deferred_update_timer_cb(lv_timer_t * timer);
static void wizard_destructor(lv_event_t * e);
static void draw_event_cb(lv_event_t * e);
static void calculate_result(lv_obj_t * obj);
static void set_active_step(lv_obj_t * obj, int8_t step_index, bool defer_ui_update);
static void next_btn_event_cb(lv_event_t * e);
static void schedule_deferred_update(lv_probing_wizard_t* wiz);
static void cancel_btn_event_cb(lv_event_t * e);
static void mode_selector_event_cb(lv_event_t * e);
static void canvas_click_event_cb(lv_event_t * e);
static void start_btn_event_cb(lv_event_t * e);
static void apply_btn_event_cb(lv_event_t * e);
static void apply_last_result_btn_event_cb(lv_event_t * e);

static void update_ui_state(lv_obj_t * obj);
static void update_progress_panel(lv_probing_wizard_t * wiz);
static void update_last_result_display(lv_probing_wizard_t * wiz);
static void update_setup_points_display(lv_probing_wizard_t * wiz);


static void create_mode_selectors(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_last_result_display(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_progress_panel(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_wcs_selector(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_results_display(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_left_panel(lv_obj_t * parent, lv_probing_wizard_t * wiz);
static void create_canvas_and_controls(lv_obj_t * parent, lv_probing_wizard_t * wiz);

static void draw_rectangle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static void draw_circle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static void draw_corner_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area);
static inline void draw_result_crosshair(lv_layer_t * layer, lv_point_t center, lv_color_t color);

// Helper function to manage object flags
static void lv_obj_manage_flag(lv_obj_t *obj, lv_obj_flag_t flag, bool on) {
    if (on) {
        lv_obj_add_flag(obj, flag);
    } else {
        lv_obj_clear_flag(obj, flag);
    }
}

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
    create_last_result_display(left_panel, wiz);
    create_progress_panel(left_panel, wiz);
    create_wcs_selector(left_panel, wiz);
    create_results_display(left_panel, wiz);
}

static void create_canvas_and_controls(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    lv_obj_t* wizard_obj = lv_obj_get_parent(wiz->instruction_label);

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

    wiz->btn_bar = lv_obj_create(right_panel);
    lv_obj_remove_style_all(wiz->btn_bar);
    lv_obj_set_width(wiz->btn_bar, lv_pct(100));
    lv_obj_set_height(wiz->btn_bar, LV_SIZE_CONTENT);
    lv_obj_set_layout(wiz->btn_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wiz->btn_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wiz->btn_bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(wiz->btn_bar, 5, 0);
    lv_obj_clear_flag(wiz->btn_bar, LV_OBJ_FLAG_SCROLLABLE);

    // --- Create all buttons, their visibility will be managed ---
    // Start Button
    wiz->start_btn = lv_btn_create(wiz->btn_bar);
    lv_obj_set_width(wiz->start_btn, LV_PCT(100));
    lv_obj_add_event_cb(wiz->start_btn, start_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_t * start_label = lv_label_create(wiz->start_btn);
    lv_label_set_text(start_label, "Start Probing");
    lv_obj_center(start_label);

    // Cancel Button
    wiz->cancel_btn = lv_btn_create(wiz->btn_bar);
    lv_obj_add_event_cb(wiz->cancel_btn, cancel_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_t * cancel_label = lv_label_create(wiz->cancel_btn);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE);
    lv_obj_center(cancel_label);

    lv_obj_t* spacer = lv_obj_create(wiz->btn_bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_height(spacer, 10);
    lv_obj_set_flex_grow(spacer, 1);

    // Next Button
    wiz->next_btn = lv_btn_create(wiz->btn_bar);
    lv_obj_add_event_cb(wiz->next_btn, next_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_t * next_label = lv_label_create(wiz->next_btn);
    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
    lv_obj_center(next_label);
    
    // Apply XY Button
    wiz->apply_xy_btn = lv_btn_create(wiz->btn_bar);
    lv_obj_add_event_cb(wiz->apply_xy_btn, apply_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_t* apply_xy_label = lv_label_create(wiz->apply_xy_btn);
    lv_label_set_text(apply_xy_label, LV_SYMBOL_DOWNLOAD " XY");
    lv_obj_center(apply_xy_label);
    lv_obj_set_user_data(wiz->apply_xy_btn, (void*)false); // apply_z = false

    // Apply XY+Z Button
    wiz->apply_xyz_btn = lv_btn_create(wiz->btn_bar);
    lv_obj_add_event_cb(wiz->apply_xyz_btn, apply_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_t* apply_xyz_label = lv_label_create(wiz->apply_xyz_btn);
    lv_label_set_text(apply_xyz_label, LV_SYMBOL_DOWNLOAD " XYZ");
    lv_obj_center(apply_xyz_label);
    lv_obj_set_user_data(wiz->apply_xyz_btn, (void*)true); // apply_z = true
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
    lv_obj_t* wizard_obj = lv_obj_get_parent(wiz->instruction_label);

    wiz->mode_select_panel = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(wiz->mode_select_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wiz->mode_select_panel, 0, 0);
    lv_obj_set_style_pad_all(wiz->mode_select_panel, 0, 0);
    lv_obj_set_width(wiz->mode_select_panel, lv_pct(100));
    lv_obj_set_layout(wiz->mode_select_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wiz->mode_select_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wiz->mode_select_panel, 5, 0);

    static const char * mode_map[] = {"Block", "Circular", "Corner", ""};
    wiz->mode_btnm = lv_btnmatrix_create(wiz->mode_select_panel);
    lv_btnmatrix_set_map(wiz->mode_btnm, mode_map);
    lv_btnmatrix_set_btn_ctrl_all(wiz->mode_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(wiz->mode_btnm, true);
    lv_obj_set_size(wiz->mode_btnm, lv_pct(100), 45);
    lv_obj_add_event_cb(wiz->mode_btnm, mode_selector_event_cb, LV_EVENT_VALUE_CHANGED, wizard_obj);
    lv_obj_set_style_text_font(wiz->mode_btnm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(wiz->mode_btnm, 2, 0);

    static const char * variant_map[] = {"Inside", "Outside", ""};
    wiz->variant_btnm = lv_btnmatrix_create(wiz->mode_select_panel);
    lv_btnmatrix_set_map(wiz->variant_btnm, variant_map);
    lv_btnmatrix_set_btn_ctrl_all(wiz->variant_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(wiz->variant_btnm, true);
    lv_obj_set_size(wiz->variant_btnm, lv_pct(100), 45);
    lv_obj_add_event_cb(wiz->variant_btnm, mode_selector_event_cb, LV_EVENT_VALUE_CHANGED, wizard_obj);
    lv_obj_set_style_text_font(wiz->variant_btnm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(wiz->variant_btnm, 2, 0);
}

static void create_last_result_display(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    lv_obj_t* wizard_obj = lv_obj_get_parent(wiz->instruction_label);

    wiz->last_result_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(wiz->last_result_panel);
    lv_obj_set_width(wiz->last_result_panel, lv_pct(100));
    lv_obj_set_layout(wiz->last_result_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wiz->last_result_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wiz->last_result_panel, 2, 0);

    lv_obj_t* title = lv_label_create(wiz->last_result_panel);
    lv_label_set_text_static(title, "Last Probe Result");
    lv_obj_set_style_text_color(title, lv_color_hex(0x888888), 0);

    wiz->last_result_label_x = lv_label_create(wiz->last_result_panel);
    wiz->last_result_label_y = lv_label_create(wiz->last_result_panel);
    wiz->last_result_label_z = lv_label_create(wiz->last_result_panel);

    lv_obj_t * btn_cont = lv_obj_create(wiz->last_result_panel);
    lv_obj_remove_style_all(btn_cont);
    lv_obj_set_width(btn_cont, lv_pct(100));
    lv_obj_set_layout(btn_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(btn_cont, 5, 0);
    lv_obj_set_style_pad_gap(btn_cont, 5, 0);
    
    wiz->apply_last_xy_btn = lv_btn_create(btn_cont);
    lv_obj_set_flex_grow(wiz->apply_last_xy_btn, 1);
    lv_obj_add_event_cb(wiz->apply_last_xy_btn, apply_last_result_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_set_user_data(wiz->apply_last_xy_btn, (void*)false);
    lv_obj_t* apply_last_xy_label = lv_label_create(wiz->apply_last_xy_btn);
    lv_label_set_text_static(apply_last_xy_label, LV_SYMBOL_DOWNLOAD " XY");
    lv_obj_center(apply_last_xy_label);

    wiz->apply_last_xyz_btn = lv_btn_create(btn_cont);
    lv_obj_set_flex_grow(wiz->apply_last_xyz_btn, 1);
    lv_obj_add_event_cb(wiz->apply_last_xyz_btn, apply_last_result_btn_event_cb, LV_EVENT_CLICKED, wizard_obj);
    lv_obj_set_user_data(wiz->apply_last_xyz_btn, (void*)true);
    lv_obj_t* apply_last_xyz_label = lv_label_create(wiz->apply_last_xyz_btn);
    lv_label_set_text_static(apply_last_xyz_label, LV_SYMBOL_DOWNLOAD " XYZ");
    lv_obj_center(apply_last_xyz_label);
}


static void create_progress_panel(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    wiz->progress_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(wiz->progress_panel);
    lv_obj_set_width(wiz->progress_panel, lv_pct(100));
    lv_obj_set_layout(wiz->progress_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wiz->progress_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wiz->progress_panel, 5, 0);
    lv_obj_add_flag(wiz->progress_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(wiz->progress_panel);
    lv_label_set_text_static(title, "Probe Points");
    lv_obj_set_style_text_color(title, lv_color_hex(0x888888), 0);

    for(int i = 0; i < MAX_PROBE_POINTS; i++) {
        wiz->probe_point_labels[i] = lv_label_create(wiz->progress_panel);
        lv_label_set_text(wiz->probe_point_labels[i], "");
    }
}

static void create_wcs_selector(lv_obj_t * parent, lv_probing_wizard_t * wiz) {
    wiz->wcs_select_panel = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(wiz->wcs_select_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wiz->wcs_select_panel, 0, 0);
    lv_obj_set_style_pad_all(wiz->wcs_select_panel, 0, 0);
    lv_obj_set_width(wiz->wcs_select_panel, lv_pct(100));
    lv_obj_set_layout(wiz->wcs_select_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wiz->wcs_select_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wiz->wcs_select_panel, 5, 0);
    lv_obj_add_flag(wiz->wcs_select_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* gcs_label = lv_label_create(wiz->wcs_select_panel);
    lv_label_set_text_static(gcs_label, "Workpiece Coordinate");
    lv_obj_set_style_text_color(gcs_label, lv_color_hex(0x888888), 0);

    static const char * btnm_map[] = {"G55", "G56", "G57", "\n", "G58", "G59", ""};
    wiz->wcs_btnm = lv_btnmatrix_create(wiz->wcs_select_panel);
    lv_btnmatrix_set_map(wiz->wcs_btnm, btnm_map);
    lv_btnmatrix_set_btn_ctrl_all(wiz->wcs_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(wiz->wcs_btnm, true);
    lv_obj_set_size(wiz->wcs_btnm, lv_pct(100), 95);
    lv_obj_add_flag(wiz->wcs_btnm, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_text_font(wiz->wcs_btnm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(wiz->wcs_btnm, 2, 0);
    lv_btnmatrix_set_btn_ctrl(wiz->wcs_btnm, 0, LV_BTNMATRIX_CTRL_CHECKED);
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

    wiz->setup_points_label = lv_label_create(res_cont);
    lv_label_set_text(wiz->setup_points_label, "");
    lv_obj_set_width(wiz->setup_points_label, lv_pct(100));
    lv_label_set_long_mode(wiz->setup_points_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(wiz->setup_points_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(wiz->setup_points_label, lv_color_hex(0xcccccc), 0);
    lv_obj_add_flag(wiz->setup_points_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* res_label = lv_label_create(res_cont);
    lv_label_set_text_static(res_label, "Probing Result");
    lv_obj_set_style_text_color(res_label, lv_color_hex(0x888888), 0);
    wiz->result_label_x = lv_label_create(res_cont);
    wiz->result_label_y = lv_label_create(res_cont);
    wiz->result_label_z = lv_label_create(res_cont);
    lv_obj_set_style_text_font(wiz->result_label_x, lv_theme_get_font_large(parent), 0);
    lv_obj_set_style_text_font(wiz->result_label_y, lv_theme_get_font_large(parent), 0);
    lv_obj_set_style_text_font(wiz->result_label_z, lv_theme_get_font_large(parent), 0);
    lv_obj_set_style_text_color(wiz->result_label_x, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_text_color(wiz->result_label_y, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_text_color(wiz->result_label_z, lv_color_hex(0x007AFF), 0);
}

lv_obj_t * lv_probing_wizard_create(lv_obj_t * parent) {
    lv_probing_wizard_t * wiz = lv_malloc(sizeof(lv_probing_wizard_t));
    LV_ASSERT_MALLOC(wiz);
    if (wiz == NULL) return NULL;
    lv_memset(wiz, 0, sizeof(lv_probing_wizard_t));
    LOGV(TAG, "Wizard created.");

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

    // Create a timer for deferred UI updates from other tasks.
    // We will manually trigger it, so the period doesn't matter much.
    // The main_container object is passed as user_data.
    wiz->deferred_update_timer = lv_timer_create(deferred_update_timer_cb, 50, main_container);
    lv_timer_set_auto_delete(wiz->deferred_update_timer, false); // Prevent auto-deletion
    lv_timer_set_repeat_count(wiz->deferred_update_timer, 1);   // Make it run only once per resume
    lv_timer_pause(wiz->deferred_update_timer);                 // Start in a paused state

    // Turn off default init for now.
    //wiz->mode = LV_PROBING_WIZARD_MODE_RECTANGLE;
    //wiz->is_inside = false;
    //lv_probing_wizard_set_mode(main_container, wiz->mode, wiz->is_inside);
    
    update_last_result_display(wiz);

    // --- DEBUG ---
    // Uncomment one of these defines to start the wizard in a "completed" state for UI testing.
    #define WIZARD_DEBUG_RECTANGLE
    // #define WIZARD_DEBUG_CIRCLE
    // #define WIZARD_DEBUG_CORNER

#if defined(WIZARD_DEBUG_RECTANGLE)
    LOGW(TAG, "WIZARD DEBUG: Simulating RECTANGLE probe completion.");
    // 1. Set initial mode so the correct routine size is known.
    wiz->mode = LV_PROBING_WIZARD_MODE_RECTANGLE;
    wiz->is_inside = false;
    wiz->z_top = -1.25; // Simulate Z having been probed.
    wiz->z_top_is_set = true;
    wiz->result_valid = true;
    wiz->result_details.dimensions.x = 50.0;
    wiz->result_details.dimensions.y = 100.0;

    // 2. Report the results exactly as the machine handler would.
    lv_probing_wizard_details_t details = { .dimensions = {49.8, 75.1} };
    lv_probing_wizard_report_details(main_container, &details);
    lv_probing_wizard_report_final_result(main_container, 100.55, 75.12);

    // 3. Trigger the deferred step change to the final "complete" step.
    uint8_t final_step = probe_routine_sizes[wiz->mode] - 1;
    lv_probing_wizard_set_active_step_deferred(main_container, final_step);
    update_ui_state(main_container);

#elif defined(WIZARD_DEBUG_CIRCLE)
    LOGW(TAG, "WIZARD DEBUG: Simulating CIRCLE probe completion.");
    wiz->mode = LV_PROBING_WIZARD_MODE_CIRCLE;
    wiz->is_inside = true;
    wiz->z_top = -0.78;
    wiz->z_top_is_set = true;
    wiz->result_valid = true;
    lv_probing_wizard_details_t details = { .radius = 25.1 };
    lv_probing_wizard_report_details(main_container, &details);
    lv_probing_wizard_report_final_result(main_container, 50.2, 48.9);
    uint8_t final_step = probe_routine_sizes[wiz->mode] - 1;
    lv_probing_wizard_set_active_step_deferred(main_container, final_step);

#elif defined(WIZARD_DEBUG_CORNER)
    LOGW(TAG, "WIZARD DEBUG: Simulating CORNER probe completion.");
    wiz->mode = LV_PROBING_WIZARD_MODE_CORNER;
    wiz->is_inside = false;
    wiz->z_top = -3.45;
    wiz->z_top_is_set = true;
    wiz->corner_type = LV_PROBING_CORNER_FRONT_LEFT;
    wiz->result_valid = true;
    // Corner has no details to report.
    lv_probing_wizard_report_final_result(main_container, 10.0, 12.5);
    uint8_t final_step = probe_routine_sizes[wiz->mode] - 1;
    lv_probing_wizard_set_active_step_deferred(main_container, final_step);

#else
    // If no debug mode is set, initialize normally.
    update_last_result_display(wiz); // Update with empty/persisted last result
    wiz->mode = LV_PROBING_WIZARD_MODE_RECTANGLE;
    wiz->is_inside = false;
    lv_probing_wizard_set_mode(main_container, wiz->mode, wiz->is_inside);
#endif

    return main_container;
}

static void reset_and_start_routine(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    LOGV(TAG, "Wizard reset. Active step: 0");

    for (int i = 0; i < MAX_SETUP_POINTS; i++) wiz->setup_points[i].is_set = false;
    for (int i = 0; i < MAX_PROBE_POINTS; i++) wiz->probe_results[i].is_set = false;
    
    wiz->result_valid = false;
    wiz->result_details.dimensions.x = NAN;
    wiz->result_details.dimensions.y = NAN;
    wiz->result_details.radius = NAN;
    wiz->result_details.rotation = NAN;
    wiz->result_dim_w_text[0] = '\0';
    wiz->result_dim_h_text[0] = '\0';
    wiz->result_rad_text[0] = '\0';
    wiz->z_top_is_set = false;
    wiz->active_step = -1; // Will be advanced to 0 by set_active_step
    wiz->wizard_state = WIZARD_STATE_CONFIG;
    wiz->corner_type = LV_PROBING_CORNER_NONE;

    // Clear any pending deferred actions or data so a fresh probe run
    // doesn't accidentally execute stale deferred callbacks from a prior run.
    wiz->deferred_action = DEFERRED_ACTION_NONE;
    wiz->deferred_next_step = -1;
    wiz->deferred_z_top = 0.0f;
    wiz->deferred_final_result.x = 0.0f;
    wiz->deferred_final_result.y = 0.0f;
    memset((void *)&wiz->deferred_details, 0, sizeof(wiz->deferred_details));
    if (wiz->deferred_update_timer) {
        lv_timer_pause(wiz->deferred_update_timer);
        lv_timer_set_repeat_count(wiz->deferred_update_timer, 0);
    }

    if (wiz->mode_btnm) lv_btnmatrix_set_btn_ctrl(wiz->mode_btnm, (uint16_t)wiz->mode, LV_BTNMATRIX_CTRL_CHECKED);
    if (wiz->variant_btnm) lv_btnmatrix_set_btn_ctrl(wiz->variant_btnm, wiz->is_inside ? 0 : 1, LV_BTNMATRIX_CTRL_CHECKED);
    
    update_last_result_display(wiz);
    set_active_step(obj, 0, false); // Start at the first step (config screen)
}

void lv_probing_wizard_set_mode(lv_obj_t * obj, lv_probing_wizard_mode_t mode, bool is_inside) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    LOGV(TAG, "Setting mode to %d, is_inside: %d", mode, is_inside);
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

void lv_probing_wizard_register_callbacks(lv_obj_t * obj, get_current_jogged_position_cb_t get_pos_cb, execute_probe_cb_t exec_probe_cb, set_wcs_origin_cb_t set_wcs_cb, install_probe_tool_cb_t install_probe_cb, cancel_probe_cb_t cancel_probe_cb) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    wiz->get_pos_cb = get_pos_cb;
    wiz->exec_probe_cb = exec_probe_cb;
    wiz->set_wcs_cb = set_wcs_cb;
    wiz->install_probe_cb = install_probe_cb;
    wiz->cancel_probe_cb = cancel_probe_cb;
    LOGV(TAG, "Callbacks registered.");
}

void lv_probing_wizard_set_connected(lv_obj_t * obj, bool connected) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    if (wiz->machine_connected != connected) {
        wiz->machine_connected = connected;
        // The start button is only visible in config mode.
        if (wiz->wizard_state == WIZARD_STATE_CONFIG) {
            update_ui_state(obj);
        }
    }
}

void lv_probing_wizard_set_z_top(lv_obj_t * obj, float z_top) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    LOGV(TAG, "Z-top reported: %.3f", z_top);
    wiz->z_top = z_top;
    wiz->z_top_is_set = true;
    update_ui_state(obj); // Re-evaluate button states now that Z is known
}

void lv_probing_wizard_report_probe_result(lv_obj_t * obj, uint8_t probe_index, float x, float y) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz || probe_index >= MAX_PROBE_POINTS) return;

    LOGV(TAG, "Probe result for index %d reported: X=%.3f, Y=%.3f", probe_index, x, y);
    wiz->probe_results[probe_index].x = x;
    wiz->probe_results[probe_index].y = y;
    wiz->probe_results[probe_index].is_set = true;
    calculate_result(obj);
    update_progress_panel(wiz);
}

void lv_probing_wizard_report_final_result(lv_obj_t * obj, float x, float y) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if(!wiz) return;
    wiz->result.x = x;
    wiz->result.y = y;
    wiz->result_valid = true;
    LOGV(TAG, "Final result reported: X=%.3f, Y=%.3f", x, y);
}

void lv_probing_wizard_report_details(lv_obj_t * obj, const lv_probing_wizard_details_t * details) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz || !details) return;
    wiz->result_details = *details;
    LOGV(TAG, "Probe details reported: Dims(%.2f, %.2f), Rad(%.2f), Rot(%.2f)",
         details->dimensions.x, details->dimensions.y, details->radius, details->rotation);
}


void lv_probing_wizard_advance_step(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    LOGV(TAG, "Advancing from step %d...", wiz->active_step);

    int8_t next_step = wiz->active_step + 1;
    if (next_step < probe_routine_sizes[wiz->mode]) {
        set_active_step(obj, next_step, false);
    } else {
        LOGV(TAG, "Cannot advance, already at last step of routine.");
    }
}

void lv_probing_wizard_set_active_step(lv_obj_t * obj, int8_t step_index, bool defer_ui_update) {
    set_active_step(obj, step_index, defer_ui_update);
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

lv_probing_wizard_mode_t lv_probing_wizard_get_mode(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    return wiz->mode;
}

bool lv_probing_wizard_get_is_inside(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    return wiz->is_inside;
}

lv_probing_wizard_corner_t lv_probing_wizard_get_corner_type(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    return wiz->corner_type;
}


static void update_progress_panel(lv_probing_wizard_t * wiz) {
    // This function is less relevant for single-macro probing, but kept for potential future use.
    if (wiz->result_valid) {
        snprintf(wiz->probe_point_text[0], sizeof(wiz->probe_point_text[0]),
                 "Result: X: %.3f Y: %.3f", wiz->result.x, wiz->result.y);
    } else {
        snprintf(wiz->probe_point_text[0], sizeof(wiz->probe_point_text[0]), "Probing...");
    }
    lv_label_set_text(wiz->probe_point_labels[0], wiz->probe_point_text[0]);
    lv_obj_clear_flag(wiz->probe_point_labels[0], LV_OBJ_FLAG_HIDDEN);

    for (int i = 1; i < MAX_PROBE_POINTS; i++) {
        lv_obj_add_flag(wiz->probe_point_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_last_result_display(lv_probing_wizard_t * wiz) {
    if (last_probe_result.is_set) {
        char buf[32];
        snprintf(buf, sizeof(buf), "X: %.3f", last_probe_result.x);
        lv_label_set_text(wiz->last_result_label_x, buf);
        snprintf(buf, sizeof(buf), "Y: %.3f", last_probe_result.y);
        lv_label_set_text(wiz->last_result_label_y, buf);
        snprintf(buf, sizeof(buf), "Z: %.3f", last_probe_z);
        lv_label_set_text(wiz->last_result_label_z, buf);

        lv_obj_clear_state(wiz->apply_last_xy_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(wiz->apply_last_xyz_btn, LV_STATE_DISABLED);
    } else {
        lv_label_set_text_static(wiz->last_result_label_x, "X:   - - -");
        lv_label_set_text_static(wiz->last_result_label_y, "Y:   - - -");
        lv_label_set_text_static(wiz->last_result_label_z, "Z:   - - -");
        lv_obj_add_state(wiz->apply_last_xy_btn, LV_STATE_DISABLED);
        lv_obj_add_state(wiz->apply_last_xyz_btn, LV_STATE_DISABLED);
    }
}

static void update_ui_state(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    bool config_mode = (wiz->wizard_state == WIZARD_STATE_CONFIG);
    bool probing_mode = (wiz->wizard_state == WIZARD_STATE_PROBING);
    bool complete_mode = (wiz->wizard_state == WIZARD_STATE_COMPLETE);

    // Panels
    lv_obj_manage_flag(wiz->mode_select_panel, LV_OBJ_FLAG_HIDDEN, !config_mode);
    lv_obj_manage_flag(wiz->last_result_panel, LV_OBJ_FLAG_HIDDEN, !config_mode);
    lv_obj_manage_flag(wiz->progress_panel, LV_OBJ_FLAG_HIDDEN, config_mode);
    lv_obj_manage_flag(wiz->wcs_select_panel, LV_OBJ_FLAG_HIDDEN, !complete_mode);

    // Buttons
    lv_obj_manage_flag(wiz->start_btn, LV_OBJ_FLAG_HIDDEN, !config_mode);
    lv_obj_manage_flag(wiz->next_btn, LV_OBJ_FLAG_HIDDEN, !probing_mode);
    lv_obj_manage_flag(wiz->cancel_btn, LV_OBJ_FLAG_HIDDEN, !(probing_mode || complete_mode));
    lv_obj_manage_flag(wiz->apply_xy_btn, LV_OBJ_FLAG_HIDDEN, !complete_mode);
    lv_obj_manage_flag(wiz->apply_xyz_btn, LV_OBJ_FLAG_HIDDEN, !complete_mode);

    if (config_mode) {
        if (wiz->machine_connected) {
            lv_obj_t * start_label = lv_obj_get_child(wiz->start_btn, 0);
            lv_label_set_text(start_label, "Start Probing");
            lv_obj_clear_state(wiz->start_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_t * start_label = lv_obj_get_child(wiz->start_btn, 0);
            lv_label_set_text(start_label, "Machine not connected");
            lv_obj_add_state(wiz->start_btn, LV_STATE_DISABLED);
        }
         lv_label_set_text(wiz->result_label_x, "X:   - - -");
         lv_label_set_text(wiz->result_label_y, "Y:   - - -");
         lv_label_set_text(wiz->result_label_z, "Z:   - - -");
    } else if (probing_mode && wiz->current_action) {
        // Handle enabling/disabling the Next button during a probing sequence
        if (wiz->current_action->type == ACTION_PROBE_Z_TOP && !wiz->z_top_is_set) {
            lv_obj_add_state(wiz->next_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(wiz->next_btn, LV_STATE_DISABLED);
        }
    } else if (complete_mode) {
        // Show cancel button to allow exiting the results screen
        lv_obj_clear_flag(wiz->cancel_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_setup_points_display(lv_probing_wizard_t * wiz) {
    char buf[128] = {0};
    bool any_set = false;

    if (wiz->mode == LV_PROBING_WIZARD_MODE_RECTANGLE) {
        if (wiz->setup_points[0].is_set) {
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "BL: (%.2f, %.2f)", wiz->setup_points[0].x, wiz->setup_points[0].y);
            any_set = true;
        }
        if (wiz->setup_points[1].is_set) {
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nFR: (%.2f, %.2f)", wiz->setup_points[1].x, wiz->setup_points[1].y);
            any_set = true;
        }
    } else if (wiz->mode == LV_PROBING_WIZARD_MODE_CIRCLE) {
        if (wiz->setup_points[0].is_set) {
            snprintf(buf, sizeof(buf), "Center: (%.2f, %.2f)", wiz->setup_points[0].x, wiz->setup_points[0].y);
            any_set = true;
        }
        if (wiz->setup_points[1].is_set) {
             snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nEdge: (%.2f, %.2f)", wiz->setup_points[1].x, wiz->setup_points[1].y);
             any_set = true;
        }
    }
    // No explicit setup points for corner mode.

    if (any_set) {
        lv_label_set_text(wiz->setup_points_label, buf);
        lv_obj_clear_flag(wiz->setup_points_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(wiz->setup_points_label, "");
        lv_obj_add_flag(wiz->setup_points_label, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
* @brief Performs the actual UI update based on the current wizard state.
 * This should only be called from the LVGL task context.
 */
static void deferred_update_ui(lv_obj_t* obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz || !wiz->current_action) return;

    LOGV(TAG, "Updating UI for step %d: '%s'", wiz->active_step, wiz->current_action->instruction_text);

    lv_label_set_text(wiz->instruction_label, wiz->current_action->instruction_text);
    if(wiz->canvas) lv_obj_invalidate(wiz->canvas);

    switch (wiz->current_action->type) {
        case ACTION_AWAIT_START:
            wiz->wizard_state = WIZARD_STATE_CONFIG;
            break;
        case ACTION_COMPLETE:
            LOGV(TAG, "Reached COMPLETE step. Changing state to COMPLETE.");
            wiz->wizard_state = WIZARD_STATE_COMPLETE;
            break;
        default:
            wiz->wizard_state = WIZARD_STATE_PROBING;
            break;
    }
    
    update_setup_points_display(wiz);
    update_ui_state(obj);

    // Handle step-specific interactions and commands.
    switch (wiz->current_action->type) {
        case ACTION_AWAIT_START:
            // Clear result labels for the initial config screen
            lv_label_set_text(wiz->result_label_x, "X:   - - -");
            lv_label_set_text(wiz->result_label_y, "Y:   - - -");
            lv_label_set_text(wiz->result_label_z, "Z:   - - -");
            break;

        case ACTION_SELECT_CORNER:
            lv_obj_add_state(wiz->next_btn, LV_STATE_DISABLED); // Must click canvas
            lv_obj_add_event_cb(wiz->canvas, canvas_click_event_cb, LV_EVENT_CLICKED, obj);
            break;
        
        case ACTION_PROBE_POINT:
        case ACTION_PROBE_Z_TOP: {
            bool can_execute = true;
            if (wiz->current_action->type == ACTION_PROBE_POINT) {
                /* Ensure required setup points and Z-top have been captured before
                 * invoking the machine handler. This prevents sending probe macros
                 * without J/K/L/Z parameters which cause the MOS macros to abort.
                 */
                lv_probing_wizard_mode_t mode = lv_probing_wizard_get_mode(obj);
                if (mode == LV_PROBING_WIZARD_MODE_RECTANGLE || mode == LV_PROBING_WIZARD_MODE_CIRCLE) {
                    if (!wiz->setup_points[0].is_set || !wiz->setup_points[1].is_set) {
                        LOGW(TAG, "Cannot execute probe: required setup points not set.");
                        can_execute = false;
                    }
                } else if (mode == LV_PROBING_WIZARD_MODE_CORNER) {
                    if (!wiz->setup_points[0].is_set || wiz->corner_type == LV_PROBING_CORNER_NONE) {
                        LOGW(TAG, "Cannot execute corner probe: start position or corner not set.");
                        can_execute = false;
                    }
                }
                if (!wiz->z_top_is_set) {
                    LOGW(TAG, "Cannot execute probe: Z-top not measured.");
                    can_execute = false;
                }
            }

            if (!can_execute) {
                /* Keep user in the current step; inform via log and UI label update. */
                lv_label_set_text(wiz->instruction_label, "Setup incomplete: confirm jog/probe steps first.");
                lv_obj_invalidate(wiz->canvas);
                break;
            }

            if (wiz->exec_probe_cb) {
                LOGV(TAG, "Executing probe callback for action type %d", wiz->current_action->type);
                wiz->exec_probe_cb(obj, wiz->current_action);
            } else {
                LOGW(TAG, "Probe callback is NULL, cannot proceed.");
            }
        } break;
        
        case ACTION_COMPLETE:
            update_progress_panel(wiz);
            lv_obj_add_state(wiz->next_btn, LV_STATE_DISABLED);
            if (wiz->result_valid) {
                snprintf(wiz->result_label_x_text, sizeof(wiz->result_label_x_text), "X:   %.3f", wiz->result.x);
                snprintf(wiz->result_label_y_text, sizeof(wiz->result_label_y_text), "Y:   %.3f", wiz->result.y);
                lv_label_set_text(wiz->result_label_x, wiz->result_label_x_text);
                lv_label_set_text(wiz->result_label_y, wiz->result_label_y_text);
                if (wiz->z_top_is_set) {
                    char z_buf[32];
                    snprintf(z_buf, sizeof(z_buf), "Z:   %.3f", wiz->z_top);
                    lv_label_set_text(wiz->result_label_z, z_buf);
                }
            }
            break;
        
        default:
            break;
    }
}

static void set_active_step(lv_obj_t * obj, int8_t step_index, bool defer_ui_update) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    if (step_index < 0 || step_index >= probe_routine_sizes[wiz->mode]) return;

    // Always remove the event cb, whether deferred or not
    lv_obj_remove_event_cb(wiz->canvas, canvas_click_event_cb);

    // --- State Change --- (Safe to do from any context)
    wiz->active_step = step_index;
    wiz->current_action = &probe_routines[wiz->mode][step_index];
    LOGV(TAG, "Setting active step to %d: '%s' (deferred: %d)", step_index, wiz->current_action->instruction_text, defer_ui_update);

    // --- UI Update --- (Potentially deferred)
    if (defer_ui_update) {
        wiz->deferred_action = DEFERRED_ACTION_UPDATE_UI;
        schedule_deferred_update(wiz);
    } else {
        deferred_update_ui(obj);
    }
}

static void start_btn_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) {
        LOGE(TAG, "Start button clicked, but wizard user data is NULL!");
        return;
    }

    if (wiz->install_probe_cb) {
        wiz->install_probe_cb(obj);
    } else {
        lv_probing_wizard_probe_intalled(obj);
    }

    LOGV(TAG, "Start button clicked. Changing state to PROBING.");
}

void lv_probing_wizard_probe_intalled(lv_obj_t *obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    wiz->wizard_state = WIZARD_STATE_PROBING;
    update_ui_state(obj);
    update_progress_panel(wiz);
    lv_probing_wizard_advance_step(obj);
}

static void apply_btn_event_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz || !wiz->result_valid || !wiz->set_wcs_cb) return;
    
    bool apply_z = (bool)lv_obj_get_user_data(btn);
    LOGV(TAG, "Apply button clicked. apply_z: %d", apply_z);
    if (apply_z && !wiz->z_top_is_set) {
        LOGW(TAG, "Cannot apply Z, not probed.");
        return;
    }

    // WCS index for G10 P... is 1-based, with G54=1, G55=2, etc.
    // The btnmatrix index is 0-based for G55. So we add 2.
    uint8_t wcs_p_val = lv_btnmatrix_get_selected_btn(wiz->wcs_btnm) + 2;
    
    wiz->set_wcs_cb(obj, wcs_p_val, wiz->result.x, wiz->result.y, wiz->z_top, apply_z);
    
    // Save the new result as the "last probe result" for future use
    last_probe_result.x = wiz->result.x;
    last_probe_result.y = wiz->result.y;
    if (apply_z) {
        last_probe_z = wiz->z_top;
    }
    last_probe_result.is_set = true;
    LOGV(TAG, "Saved last probe result: X=%.3f Y=%.3f Z=%.3f", last_probe_result.x, last_probe_result.y, last_probe_z);
    
    reset_and_start_routine(obj);
}

static void apply_last_result_btn_event_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz || !last_probe_result.is_set || !wiz->set_wcs_cb) return;
    
    bool apply_z = (bool)lv_obj_get_user_data(btn);
    LOGV(TAG, "Apply last result button clicked. apply_z: %d", apply_z);

    uint8_t wcs_p_val = lv_btnmatrix_get_selected_btn(wiz->wcs_btnm) + 2;
    
    wiz->set_wcs_cb(obj, wcs_p_val, last_probe_result.x, last_probe_result.y, last_probe_z, apply_z);
    
    // Applying the old result doesn't change it, so no save. Just reset.
    reset_and_start_routine(obj);
}

static void next_btn_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz || !wiz->current_action) return;

    LOGV(TAG, "Next button clicked.");
    if (wiz->current_action->type == ACTION_JOG_AND_CONFIRM) {
        if (wiz->get_pos_cb) {
            uint8_t index = wiz->current_action->param.setup_point_index;
            if (index < MAX_SETUP_POINTS) {
                lv_probing_wizard_point_float_t pos = wiz->get_pos_cb();
                wiz->setup_points[index].x = pos.x;
                wiz->setup_points[index].y = pos.y;
                wiz->setup_points[index].is_set = true;
                LOGV(TAG, "Jog position for setup point %d confirmed: X=%.3f, Y=%.3f", index, pos.x, pos.y);
            }
        }
    }
    lv_probing_wizard_advance_step(obj);
}

static void cancel_btn_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_user_data(e);
    LOGV(TAG, "Cancel button clicked.");
    // First notify the machine handler (if any) to cancel any in-flight probe
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (wiz && wiz->cancel_probe_cb) {
        wiz->cancel_probe_cb(obj);
    }

    // Then reset the wizard UI state to a clean configuration screen
    reset_and_start_routine(obj);
}

static void wizard_destructor(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (wiz) {
        if (wiz->deferred_update_timer) {
            lv_timer_del(wiz->deferred_update_timer);
            wiz->deferred_update_timer = NULL;
        }
        LOGV(TAG, "Wizard destroyed.");
        lv_free(wiz);
    }
}

static void calculate_result(lv_obj_t* obj) {
    // This function is no longer used to calculate the final result, as that comes
    // directly from the machine handler. It's kept in case intermediate calculations
    // for display purposes are ever needed.
}

/**
 * @brief Schedules a deferred UI update to be run in the LVGL task context.
 */
static void schedule_deferred_update(lv_probing_wizard_t* wiz) {
    if (wiz && wiz->deferred_update_timer) {
        // To make a paused, non-repeating timer run again, we must:
        // 1. Reset its repeat count (as it will be 0 after running once).
        // 2. Resume it.
        lv_timer_set_repeat_count(wiz->deferred_update_timer, 1);
        lv_timer_resume(wiz->deferred_update_timer);
    }
}

void lv_probing_wizard_set_z_top_deferred(lv_obj_t * obj, float z_top) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;
    wiz->deferred_z_top = z_top;
    wiz->deferred_action = DEFERRED_ACTION_SET_Z_AND_ADVANCE;
    schedule_deferred_update(wiz);
}

void lv_probing_wizard_report_final_result_deferred(lv_obj_t * obj, float x, float y) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;
    wiz->deferred_final_result.x = x;
    wiz->deferred_final_result.y = y;
    // The handler also knows which step to jump to.
    uint8_t num_steps = probe_routine_sizes[wiz->mode];
    wiz->deferred_next_step = num_steps - 1;
    wiz->deferred_action = DEFERRED_ACTION_SET_FINAL_AND_ADVANCE;
    schedule_deferred_update(wiz);
}

void lv_probing_wizard_advance_step_deferred(lv_obj_t * obj) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;
    wiz->deferred_next_step = wiz->active_step + 1;
    wiz->deferred_action = DEFERRED_ACTION_ADVANCE_STEP;
    schedule_deferred_update(wiz);
}

void lv_probing_wizard_report_full_result_deferred(lv_obj_t * obj, float x, float y, const lv_probing_wizard_details_t * details) {
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;
    wiz->deferred_final_result.x = x;
    wiz->deferred_final_result.y = y;
    if (details) {
        // Copy details into volatile storage (safe for cross-task)
        lv_probing_wizard_details_t * dst = (lv_probing_wizard_details_t *)&wiz->deferred_details;
        *dst = *details;
    }
    uint8_t num_steps = probe_routine_sizes[wiz->mode];
    wiz->deferred_next_step = num_steps - 1;
    wiz->deferred_action = DEFERRED_ACTION_SET_FULL_RESULT_AND_ADVANCE;
    schedule_deferred_update(wiz);
}

void lv_probing_wizard_set_active_step_deferred(lv_obj_t * obj, int8_t step_index) {
    set_active_step(obj, step_index, true);
}

/**
 * @brief The timer callback that executes the deferred UI updates safely.
 */
static void deferred_update_timer_cb(lv_timer_t * timer) {
    lv_obj_t* obj = lv_timer_get_user_data(timer);
    lv_probing_wizard_t * wiz = lv_obj_get_user_data(obj);
    if (!wiz) return;

    deferred_action_t action = wiz->deferred_action;
    wiz->deferred_action = DEFERRED_ACTION_NONE; // Consume the action

    switch (action) {
        case DEFERRED_ACTION_UPDATE_UI:
            deferred_update_ui(obj);
            break;
        case DEFERRED_ACTION_SET_Z_AND_ADVANCE:
            lv_probing_wizard_set_z_top(obj, wiz->deferred_z_top);
            lv_probing_wizard_advance_step(obj);
            break;
        case DEFERRED_ACTION_SET_FINAL_AND_ADVANCE:
            lv_probing_wizard_report_final_result(obj, wiz->deferred_final_result.x, wiz->deferred_final_result.y);
            lv_probing_wizard_set_active_step(obj, wiz->deferred_next_step, false);
            break;
        case DEFERRED_ACTION_ADVANCE_STEP:
            if (wiz->deferred_next_step >= 0 && wiz->deferred_next_step < probe_routine_sizes[wiz->mode]) {
                set_active_step(obj, wiz->deferred_next_step, false);
            }
            break;
        case DEFERRED_ACTION_SET_FULL_RESULT_AND_ADVANCE: {
            lv_probing_wizard_details_t details = *(lv_probing_wizard_details_t *)&wiz->deferred_details;
            lv_probing_wizard_report_details(obj, &details);
            lv_probing_wizard_report_final_result(obj, wiz->deferred_final_result.x, wiz->deferred_final_result.y);
            lv_probing_wizard_set_active_step(obj, wiz->deferred_next_step, false);
            break;
        }
        case DEFERRED_ACTION_NONE:
        default:
            break;
    }

    // Pause the timer after it has run, so it's ready for the next schedule.
    lv_timer_pause(timer);
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
    if(min_dim <= 0) return;   // Widget not yet laid out — skip, avoids degenerate draw areas
    int32_t pad = (min_dim * (PROBING_WIZARD_DASH_LINE_LEN_PCT + PROBING_WIZARD_PAD_PCT)) / 100;
    lv_area_t draw_area = canvas_area;
    lv_area_increase(&draw_area, -pad, -pad);
    if(lv_area_get_width(&draw_area) <= 0 || lv_area_get_height(&draw_area) <= 0) return;

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
    LOGD(TAG, "--- Start draw_rectangle_probe ---");
    if (!wiz) {
        LOGE(TAG, "wiz is NULL!");
        return;
    }

    lv_color_t color_active = lv_theme_get_color_primary(wiz->canvas);
    lv_color_t color_done = lv_color_hex(0x888888);
    lv_color_t color_pending = lv_color_white();
    lv_color_t color_result = lv_color_hex(0x007AFF);
    uint32_t mask = wiz->current_action ? wiz->current_action->highlight_mask : HIGHLIGHT_NONE;
    LOGD(TAG, "Highlight mask: 0x%X", mask);

    lv_area_t square_area;
    lv_coord_t side = LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area));
    if(side < 10) return;   // Too small: border_width=2 + radius=5 needs side>=10 to avoid inverted inner_area crash in lv_draw_sw_border
    lv_area_set_width(&square_area, side);
    lv_area_set_height(&square_area, side);
    lv_area_align(draw_area, &square_area, LV_ALIGN_CENTER, 0, 0);
    LOGD(TAG, "Square area: (%d, %d) -> (%d, %d)", square_area.x1, square_area.y1, square_area.x2, square_area.y2);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.border_width = 2;
    rect_dsc.border_color = (mask & HIGHLIGHT_OUTLINE) ? color_active : color_pending;
    rect_dsc.bg_opa = LV_OPA_TRANSP;
    rect_dsc.radius = 5;
    lv_draw_rect(layer, &rect_dsc, &square_area);

    if (wiz->result_valid || (mask & HIGHLIGHT_CENTER)) {
         lv_point_t center = {(square_area.x1 + square_area.x2) / 2, (square_area.y1 + square_area.y2) / 2};
         LOGD(TAG, "Drawing center crosshair at (%d, %d)", center.x, center.y);
         draw_result_crosshair(layer, center, wiz->result_valid ? color_result : color_active);
    }
    
    // Draw corner highlight points
    lv_point_t corners[] = { {square_area.x1, square_area.y1}, {square_area.x2, square_area.y1}, {square_area.x1, square_area.y2}, {square_area.x2, square_area.y2} };
    uint32_t corner_masks[] = { HIGHLIGHT_CORNER_BL, HIGHLIGHT_CORNER_BR, HIGHLIGHT_CORNER_FL, HIGHLIGHT_CORNER_FR };
    for(int i=0; i<4; i++) {
        if(mask & corner_masks[i]) {
            rect_dsc.bg_color = color_active;
            rect_dsc.bg_opa = LV_OPA_COVER;
            rect_dsc.border_width = 0;
            rect_dsc.radius = LV_RADIUS_CIRCLE;
            lv_area_t point_area = {corners[i].x - 4, corners[i].y - 4, corners[i].x + 4, corners[i].y + 4};
            lv_draw_rect(layer, &rect_dsc, &point_area);
        }
    }
    
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = PROBING_WIZARD_DASH_LINE_WIDTH;
    line_dsc.dash_width = 6;
    line_dsc.dash_gap = 4;

    lv_point_precise_t points[4] = {
        {square_area.x1, (square_area.y1 + square_area.y2) / 2}, {square_area.x2, (square_area.y1 + square_area.y2) / 2},
        {(square_area.x1 + square_area.x2) / 2, square_area.y2}, {(square_area.x1 + square_area.x2) / 2, square_area.y1},
    };
    int path_len = (side * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;
    lv_point_precise_t path_starts[4] = {
        {points[0].x + (wiz->is_inside ? path_len : -path_len), points[0].y}, {points[1].x + (wiz->is_inside ? -path_len : path_len), points[1].y},
        {points[2].x, points[2].y + (wiz->is_inside ? -path_len : path_len)}, {points[3].x, points[3].y + (wiz->is_inside ? path_len : -path_len)},
    };
    uint32_t highlight_flags[] = {HIGHLIGHT_PROBE_POINT_0, HIGHLIGHT_PROBE_POINT_1, HIGHLIGHT_PROBE_POINT_2, HIGHLIGHT_PROBE_POINT_3};

    LOGD(TAG, "Checking condition to draw dimensions: result_valid=%d, dimensions.x=%.2f", wiz->result_valid, wiz->result_details.dimensions.x);
    if (wiz->result_valid && !isnan(wiz->result_details.dimensions.x)) {
        LOGD(TAG, "Condition MET. Drawing dimensions.");
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = color_result;
        label_dsc.font = lv_theme_get_font_small(wiz->canvas);
        if (!label_dsc.font) {
            LOGE(TAG, "FATAL: Font is NULL, cannot draw labels!");
            return;
        }
        LOGD(TAG, "Font pointer is valid: %p", label_dsc.font);
        lv_point_t txt_size;

        // Draw width
        snprintf(wiz->result_dim_w_text, sizeof(wiz->result_dim_w_text), "%.1f", wiz->result_details.dimensions.x);
        LOGD(TAG, "Width text buffer: '%s'", wiz->result_dim_w_text);
        lv_text_get_size(&txt_size, wiz->result_dim_w_text, label_dsc.font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        LOGD(TAG, "Width text size: %d x %d", txt_size.x, txt_size.y);
        lv_area_t width_area = {
            .x1 = (square_area.x1 + square_area.x2) / 2 - txt_size.x / 2,
            .y1 = square_area.y2 + 5,
            .x2 = (square_area.x1 + square_area.x2) / 2 + txt_size.x / 2,
            .y2 = square_area.y2 + 5 + txt_size.y
        };
        LOGD(TAG, "Width draw area: (%d, %d) -> (%d, %d)", width_area.x1, width_area.y1, width_area.x2, width_area.y2);
        label_dsc.text = wiz->result_dim_w_text;
        lv_draw_label(layer, &label_dsc, &width_area);

        // Draw height
        snprintf(wiz->result_dim_h_text, sizeof(wiz->result_dim_h_text), "%.1f", wiz->result_details.dimensions.y);
        LOGD(TAG, "Height text buffer: '%s'", wiz->result_dim_h_text);
        lv_text_get_size(&txt_size, wiz->result_dim_h_text, label_dsc.font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        LOGD(TAG, "Height text size: %d x %d", txt_size.x, txt_size.y);
        lv_area_t height_area = {
            .x1 = square_area.x1 - txt_size.x - 5,
            .y1 = (square_area.y1 + square_area.y2) / 2 - txt_size.y / 2,
            .x2 = square_area.x1 - 5,
            .y2 = (square_area.y1 + square_area.y2) / 2 + txt_size.y / 2,
        };
        LOGD(TAG, "Height draw area: (%d, %d) -> (%d, %d)", height_area.x1, height_area.y1, height_area.x2, height_area.y2);
        label_dsc.text = wiz->result_dim_h_text;
        lv_draw_label(layer, &label_dsc, &height_area);
    }

    for(int i=0; i < 4; i++) {
        lv_color_t current_color = wiz->result_valid ? color_done : ((mask & highlight_flags[i]) ? color_active : color_pending);
        line_dsc.color = current_color;
        line_dsc.p1 = path_starts[i];
        line_dsc.p2 = points[i];
        lv_draw_line(layer, &line_dsc);
        
        rect_dsc.bg_color = current_color;
        rect_dsc.bg_opa = LV_OPA_COVER;
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        rect_dsc.border_width = 0;
        lv_area_t point_area = {points[i].x - 7, points[i].y - 7, points[i].x + 7, points[i].y + 7};
        lv_draw_rect(layer, &rect_dsc, &point_area);
    }
}

static void draw_circle_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area) {
    lv_color_t color_active = lv_theme_get_color_primary(wiz->canvas);
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
    if(radius <= 0) return;   // Degenerate draw_area
    arc_dsc.radius = radius;
    arc_dsc.start_angle = 0; arc_dsc.end_angle = 360;
    lv_draw_arc(layer, &arc_dsc);

    if (wiz->result_valid && !isnan(wiz->result_details.radius)) {
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = color_result;
        label_dsc.font = lv_theme_get_font_small(wiz->canvas);
        
        snprintf(wiz->result_rad_text, sizeof(wiz->result_rad_text), "R %.1f", wiz->result_details.radius);
        lv_point_t txt_size;
        lv_text_get_size(&txt_size, wiz->result_rad_text, label_dsc.font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        // Position it at 45 degrees outside the circle
        float angle = -45.0f * (M_PI / 180.0f);
        lv_area_t label_area;
        label_area.x1 = center.x + (radius + 5) * cosf(angle);
        label_area.y1 = center.y + (radius + 5) * sinf(angle) - txt_size.y / 2;
        label_area.x2 = label_area.x1 + txt_size.x;
        label_area.y2 = label_area.y1 + txt_size.y;
        label_dsc.text = wiz->result_rad_text;
        lv_draw_label(layer, &label_dsc, &label_area);
    }

    if (wiz->result_valid || (mask & HIGHLIGHT_CENTER)) {
         draw_result_crosshair(layer, center, wiz->result_valid ? color_result : color_active);
    }
    
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = PROBING_WIZARD_DASH_LINE_WIDTH;
#if CIRCULAR_PROBE_DASHED_LINES
    line_dsc.dash_width = 6;
    line_dsc.dash_gap = 4;
#endif
    
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    
    float angles[] = { -90, 150, 30 };
    lv_point_precise_t points[3];
    int path_len = (radius * 2 * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;
    uint32_t highlight_flags[] = {HIGHLIGHT_PROBE_POINT_0, HIGHLIGHT_PROBE_POINT_1, HIGHLIGHT_PROBE_POINT_2};

    for(int i=0; i<3; i++) {
        float rad = angles[i] * (M_PI / 180.0f);
        points[i].x = center.x + radius * cosf(rad);
        points[i].y = center.y + radius * sinf(rad);
        
        int path_radius = radius + (wiz->is_inside ? -path_len : path_len);
        lv_point_precise_t path_start = {center.x + path_radius * cosf(rad), center.y + path_radius * sinf(rad)};
        
        lv_color_t current_color = wiz->result_valid ? color_done : ((mask & highlight_flags[i]) ? color_active : color_pending);
        line_dsc.color = current_color;

#if CIRCULAR_PROBE_DASHED_LINES
        if (i == 0) {
            line_dsc.p1 = path_start;
            line_dsc.p2 = points[i];
            lv_draw_line(layer, &line_dsc);
        } else {
            // Emulate dashed line since LVGL can't draw them at an angle
            // Calculate vector from high-precision float coordinates before they are truncated to integers.
            float start_f_x = center.x + path_radius * cosf(rad);
            float start_f_y = center.y + path_radius * sinf(rad);
            float end_f_x = center.x + radius * cosf(rad);
            float end_f_y = center.y + radius * sinf(rad);
            float vec_x = end_f_x - start_f_x;
            float vec_y = end_f_y - start_f_y;
            float total_len = sqrtf(vec_x * vec_x + vec_y * vec_y);

            if (total_len > 0.01f) {
                float norm_x = vec_x / total_len;
                float norm_y = vec_y / total_len;

                float dash_len = line_dsc.dash_width;
                float gap_len = line_dsc.dash_gap;
                float pattern_len = dash_len + gap_len;

                lv_draw_line_dsc_t solid_line_dsc = line_dsc;
                solid_line_dsc.dash_width = 0;
                solid_line_dsc.dash_gap = 0;

                float current_pos = 0;
                while (current_pos < total_len) {
                    solid_line_dsc.p1.x = path_start.x + norm_x * current_pos;
                    solid_line_dsc.p1.y = path_start.y + norm_y * current_pos;

                    float end_pos = current_pos + dash_len;
                    if (end_pos > total_len) {
                        end_pos = total_len;
                    }

                    solid_line_dsc.p2.x = path_start.x + norm_x * end_pos;
                    solid_line_dsc.p2.y = path_start.y + norm_y * end_pos;
                    
                    lv_draw_line(layer, &solid_line_dsc);

                    current_pos += pattern_len;
                }
            }
        }
#else
        // Original behavior: let LVGL try to draw it (will be solid)
        line_dsc.p1 = path_start;
        line_dsc.p2 = points[i];
        lv_draw_line(layer, &line_dsc);
#endif
        
        rect_dsc.bg_color = current_color;
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        rect_dsc.border_width = 0;
        lv_area_t point_area = {points[i].x - 4, points[i].y - 4, points[i].x + 4, points[i].y + 4};
        lv_draw_rect(layer, &rect_dsc, &point_area);
    }
}

static void draw_corner_probe(lv_probing_wizard_t * wiz, lv_layer_t * layer, lv_area_t * draw_area) {
    lv_color_t color_active = lv_theme_get_color_primary(wiz->canvas);
    lv_color_t color_done = lv_color_hex(0x888888);
    lv_color_t color_pending = lv_color_white();
    lv_color_t color_result = lv_color_hex(0x007AFF);
    uint32_t mask = wiz->current_action ? wiz->current_action->highlight_mask : HIGHLIGHT_NONE;

    if (wiz->corner_type == LV_PROBING_CORNER_NONE) {
        lv_coord_t min_dim = LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area));
        if(min_dim < 10) return;  // Too small: border_width=2 + radius=5 needs min_dim>=10 to avoid inverted inner_area crash
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.border_width = 2;
        rect_dsc.border_color = (mask & HIGHLIGHT_OUTLINE) ? color_active : color_pending;
        rect_dsc.bg_opa = LV_OPA_TRANSP;
        rect_dsc.radius = 5;
        lv_draw_rect(layer, &rect_dsc, draw_area);

        lv_point_t corners[] = { {draw_area->x1, draw_area->y1}, {draw_area->x2, draw_area->y1}, {draw_area->x1, draw_area->y2}, {draw_area->x2, draw_area->y2} };
        uint32_t corner_masks[] = { HIGHLIGHT_CORNER_BL, HIGHLIGHT_CORNER_BR, HIGHLIGHT_CORNER_FL, HIGHLIGHT_CORNER_FR };

        for(int i=0; i<4; i++) {
            if(mask & corner_masks[i]) {
                rect_dsc.bg_color = color_active;
                rect_dsc.bg_opa = LV_OPA_COVER;
                rect_dsc.border_width = 0;
                rect_dsc.radius = LV_RADIUS_CIRCLE;
                lv_area_t corner_area = {corners[i].x-5, corners[i].y-5, corners[i].x+5, corners[i].y+5};
                lv_draw_rect(layer, &rect_dsc, &corner_area);
            }
        }
        return;
    }

    lv_point_t corner_pt;
    lv_point_precise_t h_end, v_end;
    switch (wiz->corner_type) {
        case LV_PROBING_CORNER_FRONT_LEFT: corner_pt = (lv_point_t){draw_area->x1, draw_area->y2}; h_end = (lv_point_precise_t){draw_area->x2, draw_area->y2}; v_end = (lv_point_precise_t){draw_area->x1, draw_area->y1}; break;
        case LV_PROBING_CORNER_FRONT_RIGHT: corner_pt = (lv_point_t){draw_area->x2, draw_area->y2}; h_end = (lv_point_precise_t){draw_area->x1, draw_area->y2}; v_end = (lv_point_precise_t){draw_area->x2, draw_area->y1}; break;
        case LV_PROBING_CORNER_BACK_LEFT: corner_pt = (lv_point_t){draw_area->x1, draw_area->y1}; h_end = (lv_point_precise_t){draw_area->x2, draw_area->y1}; v_end = (lv_point_precise_t){draw_area->x1, draw_area->y2}; break;
        case LV_PROBING_CORNER_BACK_RIGHT: default: corner_pt = (lv_point_t){draw_area->x2, draw_area->y1}; h_end = (lv_point_precise_t){draw_area->x1, draw_area->y1}; v_end = (lv_point_precise_t){draw_area->x2, draw_area->y2}; break;
    }

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = (mask & HIGHLIGHT_OUTLINE) ? color_active : color_pending;
    line_dsc.width = 2;
    line_dsc.p1 = (lv_point_precise_t) { corner_pt.x, corner_pt.y }; line_dsc.p2 = h_end;
    lv_draw_line(layer, &line_dsc);
    line_dsc.p2 = v_end;
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
    lv_point_t points[2] = {{corner_pt.x, center_y}, {center_x, corner_pt.y}};
    int path_len = (LV_MIN(lv_area_get_width(draw_area), lv_area_get_height(draw_area)) * PROBING_WIZARD_DASH_LINE_LEN_PCT) / 100;

    lv_point_precise_t path_starts[2];
    int8_t dir_mult = wiz->is_inside ? 1 : -1;
    path_starts[0] = (lv_point_precise_t){points[0].x + (points[0].x < center_x ? path_len : -path_len) * dir_mult, points[0].y};
    path_starts[1] = (lv_point_precise_t){points[1].x, points[1].y + (points[1].y < center_y ? path_len : -path_len) * dir_mult};

    uint32_t highlight_flags[] = {HIGHLIGHT_PROBE_POINT_0, HIGHLIGHT_PROBE_POINT_1};

    for(int i=0; i<2; i++) {
        lv_color_t current_color = wiz->result_valid ? color_done : ((mask & highlight_flags[i]) ? color_active : color_pending);
        line_dsc.color = current_color;
        line_dsc.p1 = path_starts[i];
        line_dsc.p2 = (lv_point_precise_t) { points[i].x, points[i].y };
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

    // Use a larger touch area for better usability
    lv_coord_t touch_area_size = 50; 
    
    lv_area_t draw_area = canvas_area;
    int32_t min_dim = LV_MIN(lv_area_get_width(&canvas_area), lv_area_get_height(&canvas_area));
    int32_t pad = (min_dim * (PROBING_WIZARD_DASH_LINE_LEN_PCT + PROBING_WIZARD_PAD_PCT)) / 100;
    lv_area_increase(&draw_area, -pad, -pad);

    lv_area_t bl = {draw_area.x1 - touch_area_size/2, draw_area.y1 - touch_area_size/2, draw_area.x1 + touch_area_size/2, draw_area.y1 + touch_area_size/2};
    lv_area_t br = {draw_area.x2 - touch_area_size/2, draw_area.y1 - touch_area_size/2, draw_area.x2 + touch_area_size/2, draw_area.y1 + touch_area_size/2};
    lv_area_t fl = {draw_area.x1 - touch_area_size/2, draw_area.y2 - touch_area_size/2, draw_area.x1 + touch_area_size/2, draw_area.y2 + touch_area_size/2};
    lv_area_t fr = {draw_area.x2 - touch_area_size/2, draw_area.y2 - touch_area_size/2, draw_area.x2 + touch_area_size/2, draw_area.y2 + touch_area_size/2};

    lv_probing_wizard_corner_t corner = wiz->corner_type;
    if(lv_area_is_point_on(&bl, &p, 0)) corner = LV_PROBING_CORNER_BACK_LEFT;
    else if(lv_area_is_point_on(&br, &p, 0)) corner = LV_PROBING_CORNER_BACK_RIGHT;
    else if(lv_area_is_point_on(&fl, &p, 0)) corner = LV_PROBING_CORNER_FRONT_LEFT;
    else if(lv_area_is_point_on(&fr, &p, 0)) corner = LV_PROBING_CORNER_FRONT_RIGHT;
    else return;

    LOGV(TAG, "Canvas clicked, selected corner %d", corner);
    wiz->corner_type = corner;
    lv_probing_wizard_advance_step(obj);
}