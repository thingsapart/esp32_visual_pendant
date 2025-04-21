// lv_axis_position_display.c
#include "lv_axis_position_display.h"

#include <stdio.h> // For snprintf

#include "ui/lv_vfl.h"
#include "ui/lv_views.h"

#include "../../lvgl/src/core/lv_obj_class_private.h"

#define AX_DISP_CLASS &lv_axis_position_display_class
#define INITIAL_AXIS_COLOR lv_color_hex(0x33ff44) // Green
#define SELECTED_AXIS_COLOR lv_color_hex(0x9C27B0) // Purple (Material Design Purple 500)
#define WCS_PREFIX "G"

/** Private */

static void lv_axis_position_display_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_axis_position_display_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void axis_label_event_cb(lv_event_t * e);
static void update_label_text(lv_obj_t * label, const char * symbol, float value, const char* prefix);
static lv_obj_t * lv_axis_position_display_get_child(lv_obj_t * obj, lv_part_t part);
static void lv_axis_position_display_event(const lv_obj_class_t * class_p, lv_event_t * e);

const lv_obj_class_t lv_axis_position_display_class = {
    .constructor_cb = lv_axis_position_display_constructor,
    .destructor_cb = lv_axis_position_display_destructor,
    .event_cb = lv_axis_position_display_event,

    .width_def = LV_SIZE_CONTENT,
    .height_def = LV_SIZE_CONTENT,
    .instance_size = sizeof(lv_axis_position_display_t),
    .base_class = &lv_obj_class // Inherit from base object
};

/** Private */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"

static void lv_axis_position_display_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_axis_position_display_t * disp = (lv_axis_position_display_t *)obj;

    // Initialize basic state
    disp->selected = false;

    // Temporary pointers to capture the created objects within the view macro scope
    lv_obj_t * _axis, *_pos, *_dpos, *_apos, *_adpos_cont;

    view(
        obj,
        __text_color(lv_color_white()),
        __pad_row(5),
        __pad_column(5),

        label(_axis, "Axis", // Placeholder text, will be set in create()
            //__max_client_area(),
            __text_font(&lv_font_montserrat_24), // Replace with desired font
            __text_color(INITIAL_AXIS_COLOR), // Initial color
            __text_align(LV_TEXT_ALIGN_LEFT)
        ),
        label(_pos, "0.00",
            //__max_client_area(),
            __text_font(&lv_font_montserrat_24), // Replace with desired font
            __text_color(lv_color_hex(0x33ff44)), // Keep WCS color same for now
            __text_align(LV_TEXT_ALIGN_RIGHT)
        ),

        obj( // This creates the container _adpos_cont
            _adpos_cont,
            __expand_client_area(), // Apply to the div itself

            label(_dpos, LV_SYMBOL_DOWNLOAD "   0.00", // Alt: Δ
                //__max_client_area(),
                __text_font(&lv_font_montserrat_14)
                __text_align(LV_TEXT_ALIGN_LEFT)
            ),
            label(_apos, LV_SYMBOL_GPS "   0.00", // Alt: ⊙
                //__max_client_area(),
                __text_font(&lv_font_montserrat_14)
                __text_align(LV_TEXT_ALIGN_LEFT)
            )
        )
    );

    lv_color_t text_color = lv_color_white();

    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_AXIS_LABEL);
    lv_obj_set_style_text_color(obj, INITIAL_AXIS_COLOR, LV_PART_AXIS_LABEL);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_AXIS_LABEL);

    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_POSITION);
    lv_obj_set_style_text_color(obj, INITIAL_AXIS_COLOR, LV_PART_POSITION);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_POSITION);

    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_ABS);
    lv_obj_set_style_text_color(obj, text_color, LV_PART_ABS);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_ABS);

    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_TO_GO);
    lv_obj_set_style_text_color(obj, text_color, LV_PART_TO_GO);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_TO_GO);

    // Expand axis click area a bit to make it easier to tap.
    lv_obj_set_ext_click_area(_axis, 10);

    // Assign the created objects to the struct members
    disp->axis_label = _axis;
    disp->pos_label = _pos;
    disp->dpos_label = _dpos;
    disp->apos_label = _apos;
    disp->adpos_cont = _adpos_cont;

    // Apply layout to the _adpos_cont (horizontal flex layout)
    _layout_h(_adpos_cont, LV_FLEX_ALIGN_CENTER,
        _flex(_dpos, 1),
        _flex(_apos, 1)
    );
    lv_obj_set_style_pad_column(_adpos_cont, 5, 0);

    _layout_grid(
        obj,
        _cols(LV_GRID_CONTENT, _fr(1)),
        _rows(LV_GRID_CONTENT, _px(30)),

        _cell(disp->axis_label, 0, 0, _cell_opts(.col_align=LV_GRID_ALIGN_STRETCH, .row_align=LV_GRID_ALIGN_CENTER)),
        _cell(disp->pos_label, 1, 0, _cell_opts(.col_align=LV_GRID_ALIGN_STRETCH, .row_align=LV_GRID_ALIGN_CENTER)),
        _cell(disp->adpos_cont, 0, 1, _cell_opts(.col_span=2, .col_align=LV_GRID_ALIGN_STRETCH, .row_align=LV_GRID_ALIGN_STRETCH)) // Span 2 cols, stretch container
    );

    __flag(disp->axis_label, LV_OBJ_FLAG_CLICKABLE, true);
    __add_event_cb(disp->axis_label, axis_label_event_cb, LV_EVENT_CLICKED, obj); // Pass widget obj as user data

    // Remove background, padding, border.
    __bg_opa(obj, LV_OPA_TRANSP);
    __pad_all(obj, 0);
    __border_width(obj, 0);

    LV_TRACE_OBJ_CREATE("finished");
}

#pragma clang diagnostic pop // Match the opening pragma


static void lv_axis_position_display_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    // LVGL automatically destroys children created by the view() macro.

    LV_TRACE_OBJ_CREATE("finished");
}

static void axis_label_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    // lv_obj_t * label = lv_event_get_target(e); // Target is the label itself
    lv_obj_t * obj = lv_event_get_user_data(e); // Get the main widget object

    if (code == LV_EVENT_CLICKED && obj) {
        lv_axis_position_display_t * disp = (lv_axis_position_display_t *)obj;
        disp->selected = !disp->selected; // Toggle state

        lv_color_t new_color = disp->selected ? SELECTED_AXIS_COLOR : INITIAL_AXIS_COLOR;

        lv_obj_set_style_text_color(disp->axis_label, new_color, 0);
    }
}

static void refresh_part_styles(lv_obj_t * obj) {
    lv_axis_position_display_t * disp = (lv_axis_position_display_t *)obj;

    if(disp->pos_label) {
        // Get style properties applied to the main obj FOR THIS PART
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_POSITION);
        lv_color_t color = lv_obj_get_style_text_color(obj, LV_PART_POSITION);
        lv_text_align_t align = lv_obj_get_style_text_align(obj, LV_PART_POSITION);
        // Add others as needed (letter_space, line_space, decor, opa)

        // Apply them to the child label's BASE state (selector 0)
        lv_obj_set_style_text_font(disp->pos_label, font, 0);
        lv_obj_set_style_text_color(disp->pos_label, color, 0);
        lv_obj_set_style_text_align(disp->pos_label, align, 0);
    }

     if(disp->axis_label) {
        // Get style properties applied to the main obj FOR THIS PART
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_AXIS_LABEL);
        lv_color_t color = lv_obj_get_style_text_color(obj, LV_PART_AXIS_LABEL);
        lv_text_align_t align = lv_obj_get_style_text_align(obj, LV_PART_AXIS_LABEL);
        // Add others as needed (letter_space, line_space, decor, opa)

        // Apply them to the child label's BASE state (selector 0)
        lv_obj_set_style_text_font(disp->axis_label, font, 0);
        lv_obj_set_style_text_color(disp->axis_label, color, 0);
        lv_obj_set_style_text_align(disp->axis_label, align, 0);
    }

    // --- To-Go Label (LV_PART_TO_GO) ---
    if(disp->dpos_label) {
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_TO_GO);
        lv_color_t color = lv_obj_get_style_text_color(obj, LV_PART_TO_GO);
        lv_text_align_t align = lv_obj_get_style_text_align(obj, LV_PART_TO_GO);

        lv_obj_set_style_text_font(disp->dpos_label, font, 0);
        lv_obj_set_style_text_color(disp->dpos_label, color, 0);
        lv_obj_set_style_text_align(disp->dpos_label, align, 0);
    }

    // --- Absolute Position Label (LV_PART_ABS) ---
    if(disp->apos_label) {
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_ABS);
        lv_color_t color = lv_obj_get_style_text_color(obj, LV_PART_ABS);
        lv_text_align_t align = lv_obj_get_style_text_align(obj, LV_PART_ABS);

        lv_obj_set_style_text_font(disp->apos_label, font, 0);
        lv_obj_set_style_text_color(disp->apos_label, color, 0);
        lv_obj_set_style_text_align(disp->apos_label, align, 0);
    }

    // Important: Invalidate the object to ensure redraw if styles changed visuals
    lv_obj_invalidate(obj);
}

static void lv_axis_position_display_event(const lv_obj_class_t * class_p, lv_event_t * e) {
    LV_UNUSED(class_p);

    /*Call the ancestor's event handler*/
    lv_result_t res = lv_obj_event_base(AX_DISP_CLASS, e);
    if(res != LV_RESULT_OK) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    if(code == LV_EVENT_STYLE_CHANGED || code == LV_EVENT_RESOLUTION_CHANGED) {
        refresh_part_styles(obj);
    }
}

/** Public */

static void update_label_text(lv_obj_t * label, const char * symbol, float value, const char* prefix) {
    if (!label) return;
    char buf[32];
    if (prefix && symbol) {
        snprintf(buf, sizeof(buf), "%s%s %.*f", prefix, symbol, LV_AXIS_DEFAULT_POS_PRECISION, value);
    } else if (symbol) {
        snprintf(buf, sizeof(buf), "%s %.*f", symbol, LV_AXIS_DEFAULT_POS_PRECISION, value);
    } else if (prefix) {
        snprintf(buf, sizeof(buf), "%s%.*f", prefix, LV_AXIS_DEFAULT_POS_PRECISION, value);
    }
     else {
        snprintf(buf, sizeof(buf), "%.*f", LV_AXIS_DEFAULT_POS_PRECISION, value);
    }
    lv_label_set_text(label, buf);
}

lv_obj_t * lv_axis_position_display_create(lv_obj_t * parent, const char * axis_name) {
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(AX_DISP_CLASS, parent);
    if (obj == NULL) {
        LV_LOG_WARN("Object creation failed");
        return NULL;
    }

    lv_obj_class_init_obj(obj);

    lv_axis_position_display_t * disp = (lv_axis_position_display_t *)obj;
    if (axis_name && disp->axis_label) {
        lv_label_set_text(disp->axis_label, axis_name);
    }

    return obj;
}

void lv_axis_position_display_set_abs_pos(lv_obj_t * obj, float pos) {
    LV_ASSERT_OBJ(obj, AX_DISP_CLASS);
    lv_axis_position_display_t * disp = (lv_axis_position_display_t *)obj;
    update_label_text(disp->apos_label, LV_SYMBOL_GPS, pos, NULL);
}

void lv_axis_position_display_set_wcs_pos(lv_obj_t * obj, float pos, int wcs_index) {
    LV_ASSERT_OBJ(obj, AX_DISP_CLASS);
    lv_axis_position_display_t * disp = (lv_axis_position_display_t *)obj;

    char buf[32];
    snprintf(buf, 31, WCS_PREFIX "%d - ", wcs_index);
    update_label_text(disp->pos_label, NULL, pos, buf); 
}


void lv_axis_position_display_set_dist_to_move(lv_obj_t * obj, float delta) {
    LV_ASSERT_OBJ(obj, AX_DISP_CLASS);
    lv_axis_position_display_t * disp = (lv_axis_position_display_t *)obj;
    update_label_text(disp->dpos_label, LV_SYMBOL_DOWNLOAD, delta, NULL);
}

bool lv_axis_position_display_is_selected(lv_obj_t * obj) {
    LV_ASSERT_OBJ(obj, AX_DISP_CLASS);
    lv_axis_position_display_t * disp = (lv_axis_position_display_t *)obj;
    return disp->selected;
}