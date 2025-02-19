#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "debug.h"

#define MAX_OBJECTS 128     // Maximum number of registered objects
#define MAX_ID_LENGTH 20    // Maximum length of an object ID

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the object registry.
void init_object_registry(void);

// Retrieve an object by its ID.
lv_obj_t *get_object_by_id(const char *id);

#define $ get_object_by_id

// Register object in the registry. Internal function.
bool register_object(const char *id, lv_obj_t *obj);

void _assert(bool condition, const char *desc);

// Core object creation macro.
#define CREATE_OBJECT(type, id, parent, ...) \
    ({ \
        lv_obj_t *obj = lv_##type##_create(parent); \
        if (obj == NULL) { \
            LV_LOG_ERROR("Failed to create " #type " object: %s", id); \
            _assert(false, "Failed to create " #type " object: " # id); \
        } else { \
            register_object(id, obj); \
            _df(0, "Created %s (%s) => %p (ln %d, fn %s).\n", id ? id : "NULL", #type, obj, __LINE__, __func__); \
            lv_obj_t *outer_obj = obj; \
            SET_STYLE(obj, ##__VA_ARGS__); \
        } \
        obj; \
    })

// Macro for setting object styles.
#define SET_STYLE(obj, ...) \
    do { \
        __VA_ARGS__; \
    } while(0)

//--------------------------------------------------
// Object Creation Macros
//--------------------------------------------------

#define LABEL(id, parent, ...) CREATE_OBJECT(label, id, parent, __VA_ARGS__)
#define BTN(id, parent, ...)   CREATE_OBJECT(btn, id, parent, __VA_ARGS__)
#define IMG(id, parent, ...)    CREATE_OBJECT(img, id, parent, __VA_ARGS__)
#define CONT(id, parent, ...)   CREATE_OBJECT(obj, id, parent, __VA_ARGS__) // Container
#define SLIDER(id, parent, ...) CREATE_OBJECT(slider, id, parent, __VA_ARGS__)
#define SWITCH(id, parent, ...) CREATE_OBJECT(switch, id, parent, __VA_ARGS__)
#define TEXTAREA(id, parent, ...) CREATE_OBJECT(textarea, id, parent, __VA_ARGS__)
#define CHECKBOX(id, parent, ...) CREATE_OBJECT(checkbox, id, parent, __VA_ARGS__)
#define DROPDOWN(id, parent, ...) CREATE_OBJECT(dropdown, id, parent, __VA_ARGS__)
#define ROLLER(id, parent, ...)   CREATE_OBJECT(roller, id, parent, __VA_ARGS__)
#define TABV(id, parent, ...)   CREATE_OBJECT(tabview, id, parent, __VA_ARGS__)
// Add more as needed (e.g., CHART, TABLE, etc.)

#define mk_label(id, parent, ...) CREATE_OBJECT(label, id, parent, __VA_ARGS__)
#define mk_btn(id, parent, ...)   CREATE_OBJECT(btn, id, parent, __VA_ARGS__)
#define mk_img(id, parent, ...)    CREATE_OBJECT(img, id, parent, __VA_ARGS__)
#define mk_container(id, parent, ...)   CREATE_OBJECT(obj, id, parent, __VA_ARGS__) // Container
#define mk_slider(id, parent, ...) CREATE_OBJECT(slider, id, parent, __VA_ARGS__)
#define mk_switch(id, parent, ...) CREATE_OBJECT(switch, id, parent, __VA_ARGS__)
#define mk_textarea(id, parent, ...) CREATE_OBJECT(textarea, id, parent, __VA_ARGS__)
#define mk_checkbox(id, parent, ...) CREATE_OBJECT(checkbox, id, parent, __VA_ARGS__)
#define mk_dropdown(id, parent, ...) CREATE_OBJECT(dropdown, id, parent, __VA_ARGS__)
#define mk_roller(id, parent, ...)   CREATE_OBJECT(roller, id, parent, __VA_ARGS__)
#define mk_tabv(id, parent, ...)   CREATE_OBJECT(tabview, id, parent, __VA_ARGS__)
#define mk_bar(id, parent, ...)   CREATE_OBJECT(bar, id, parent, __VA_ARGS__)
#define mk_scale(id, parent, ...)   CREATE_OBJECT(scale, id, parent, __VA_ARGS__)
#define mk_dropdown(id, parent, ...)   CREATE_OBJECT(dropdown, id, parent, __VA_ARGS__)


//--------------------------------------------------
// Abbreviated LVGL Function Macros (SETTERS)
//--------------------------------------------------

// --- General Object Properties ---
#define _size(obj, w, h)            lv_obj_set_size(obj, w, h)
#define _width(obj, w)             lv_obj_set_width(obj, w)
#define _height(obj, h)            lv_obj_set_height(obj, h)
#define _pos(obj, x, y)             lv_obj_set_pos(obj, x, y)
#define _x(obj, x)                 lv_obj_set_x(obj, x)
#define _y(obj, y)                 lv_obj_set_y(obj, y)
#define _align(obj, align)         lv_obj_set_align(obj, align)
#define _flag(obj, flag, enabled)  ((enabled) ? lv_obj_add_flag(obj, flag) : lv_obj_clear_flag(obj, flag))
#define _hidden(obj, hidden)       lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN) // Shortcut for hiding
#define _clickable(obj, enabled)   _flag(obj, LV_OBJ_FLAG_CLICKABLE, enabled)
#define _scrollable(obj, enabled)  _flag(obj, LV_OBJ_FLAG_SCROLLABLE, enabled)
#define _use_layout(obj, enabled)  _flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT, !enabled)
#define _layout(obj, layout)       lv_obj_set_layout(obj, layout)
#define _update_layout(obj)        lv_obj_update_layout(obj)

// --- Style Helpers (General) ---
#define _style_local(obj, prop, part, val) lv_obj_set_style_##prop(obj, val, part)
#define _style(obj, style, selector) lv_obj_add_style(obj, style, selector)

// --- Specific Style Properties ---
// (These are just examples, add many more based on LVGL's API)
#define _M LV_PART_MAIN
#define _text_color(obj, color, part) lv_obj_set_style_text_color(obj, color, part)
#define _bg_color(obj, color, part)   lv_obj_set_style_bg_color(obj, color, part)
#define _bg_opa(obj, opa, part)   lv_obj_set_style_bg_opa(obj, opa, part)
#define _border_color(obj, color, part) lv_obj_set_style_border_color(obj, color, part)
#define _border_width(obj, width, part) lv_obj_set_style_border_width(obj, width, part)
#define _border_opa(obj, opa, part) lv_obj_set_style_border_opa(obj, opa, part)
#define _text_font(obj, font, part) lv_obj_set_style_text_font(obj, font, part)
#define _pad_all(obj, pad, part)   lv_obj_set_style_pad_all(obj, pad, part)
#define _pad_top(obj, pad, part)   lv_obj_set_style_pad_top(obj, pad, part)
#define _pad_bottom(obj, pad, part)   lv_obj_set_style_pad_bottom(obj, pad, part)
#define _pad_left(obj, pad, part)   lv_obj_set_style_pad_left(obj, pad, part)
#define _pad_right(obj, pad, part)   lv_obj_set_style_pad_right(obj, pad, part)
#define _radius(obj, radius, part) lv_obj_set_style_radius(obj, radius, part)
#define _opa(obj, opa, part)       lv_obj_set_style_opa(obj, opa, part)
#define _img_recolor(obj, color, part) lv_obj_set_style_img_recolor(obj, color, part)
#define _img_recolor_opa(obj, opa, part) lv_obj_set_style_img_recolor_opa(obj, opa, part)
#define _outline_width(obj, width, part)    lv_obj_set_style_outline_width(obj, width, part)
#define _outline_color(obj, color, part) lv_obj_set_style_outline_color(obj, color, part)
#define _shadow_width(obj, width, part)    lv_obj_set_style_shadow_width(obj, width, part)
#define _shadow_color(obj, color, part) lv_obj_set_style_shadow_color(obj, color, part)
#define _margin_all(obj, margin, part)   lv_obj_set_style_margin_all(obj, margin, part)
#define _margin_top(obj, margin, part)   lv_obj_set_style_margin_top(obj, margin, part)
#define _margin_bottom(obj, margin, part)   lv_obj_set_style_margin_bottom(obj, margin, part)
#define _margin_left(obj, margin, part)   lv_obj_set_style_margin_left(obj, margin, part)
#define _margin_right(obj, margin, part)   lv_obj_set_style_margin_right(obj, margin, part)

#define _margin(obj, margin) _margin_all(obj, margin, LV_PART_MAIN)
#define _pad(obj, margin) _pad_all(obj, margin, LV_PART_MAIN)

#define _margins(obj, top, right, bot, left) \
    do { \
        _margin_top(obj, top, LV_PART_MAIN); \
        _margin_right(obj, right, LV_PART_MAIN); \
        _margin_bottom(obj, bot, LV_PART_MAIN); \
        _margin_left(obj, left, LV_PART_MAIN); \
    } while (0);
#define _pads(obj, top, right, bot, left) \
    do { \
        _pad_top(obj, top, LV_PART_MAIN); \
        _pad_right(obj, right, LV_PART_MAIN); \
        _pad_bottom(obj, bot, LV_PART_MAIN); \
        _pad_left(obj, left, LV_PART_MAIN); \
    } while (0);

// --- Widget-Specific Setters ---

// Label
#define _label_text(obj, text)     lv_label_set_text(obj, text)
#define _long_mode(obj, mode)      lv_label_set_long_mode(obj, mode)

// Button (btn) -  Nothing button-specific beyond the general obj setters.

// Image (img)
#define _img_src(obj, src)         lv_img_set_src(obj, src)
#define _img_offset_x(obj, x)     lv_img_set_offset_x(obj, x)
#define _img_offset_y(obj, y)     lv_img_set_offset_y(obj, y)

// Slider
#define _slider_value(obj, value, anim) lv_slider_set_value(obj, value, anim)
#define _slider_range(obj, min, max)    lv_slider_set_range(obj, min, max)

// Switch
#define _switch_state(obj, state, anim)  ((state) ? lv_switch_on(obj, anim) : lv_switch_off(obj, anim))

// Textarea
#define _textarea_text(obj, text)       lv_textarea_set_text(obj, text)
#define _textarea_placeholder(obj, text) lv_textarea_set_placeholder_text(obj, text)
#define _textarea_one_line(obj, en)       lv_textarea_set_one_line(obj, en)
#define _textarea_password_mode(obj, en)  lv_textarea_set_password_mode(obj, en)

// Checkbox
#define _checkbox_text(obj, txt)    lv_checkbox_set_text(obj, txt)
#define _checkbox_checked(obj, checked) ((checked) ? lv_checkbox_set_checked(obj) : lv_checkbox_set_unchecked(obj) )

// Dropdown
#define _dropdown_options(obj, options) lv_dropdown_set_options(obj, options)
#define _dropdown_selected(obj, sel)    lv_dropdown_set_selected(obj, sel)
#define _dropdown_open(obj)              lv_dropdown_open(obj)
#define _dropdown_close(obj)             lv_dropdown_close(obj)

// Roller
#define _roller_options(obj, options, mode) lv_roller_set_options(obj, options, mode)
#define _roller_selected(obj, sel, anim)    lv_roller_set_selected(obj, sel, anim)
#define _roller_visible_row_count(obj, cnt)  lv_roller_set_visible_row_count(obj, cnt)

// Tab View
#define _tv_bar_pos(obj, pos) lv_tabview_set_tab_bar_position(obj, pos)
#define _tv_bar_size(ob, sz) lv_tabview_set_tab_bar_size(ob, sz)

#define COMBINE1(X, Y, Z) X##Y##Z
#define TEMP_VAR(X, Y, Z) COMBINE1(X, Y, Z)

#define _bar_indicator(bar, name, bg_oa, bg_color, bg_grad_color, bg_grad_dir, bg_main_stop, radius) \
    do { \
        static lv_style_t TEMP_VAR(style_indic_, __func__, name); \
        lv_style_init(&TEMP_VAR(style_indic_, __func__, name)); \
        lv_style_set_bg_opa(&TEMP_VAR(style_indic_, __func__, name), LV_OPA_COVER); \
        lv_style_set_bg_color(&TEMP_VAR(style_indic_, __func__, name), lv_color_hex(0x00DD00)); \
        lv_style_set_bg_grad_color(&TEMP_VAR(style_indic_, __func__, name), lv_color_hex(0x0000DD)); \
        lv_style_set_bg_grad_dir(&TEMP_VAR(style_indic_, __func__, name), LV_GRAD_DIR_HOR); \
        lv_style_set_bg_main_stop(&TEMP_VAR(style_indic_, __func__, name), 175); \
        lv_style_set_radius(&TEMP_VAR(style_indic_, __func__, name), 3); \
        lv_obj_add_style(msm->bar_feed, &TEMP_VAR(style_indic_, __func__, name), LV_PART_INDICATOR); \
    } while (0);

#define _style_gradient(obj, main_color, grad_color, grad_dir, main_stop, border_width, border_color, shadow_w, shadow_color, line_color, radius) \
    do { \
        if (!lv_color_eq(main_color, lv_color_hex(0x00000000)) && lv_color_eq(grad_color, lv_color_hex(0x00000000))) { \
            _bg_opa(obj, LV_OPA_COVER, _M); \
            _bg_color(obj, main_color, _M); \
            lv_obj_set_style_bg_grad_color(obj, grad_color, _M); \
            lv_obj_set_style_bg_grad_dir(obj, grad_dir, _M); \
            lv_obj_set_style_bg_main_stop(obj, main_stop, _M); \
        } else { _bg_opa(obj, LV_OPA_0, _M); } \
        _border_width(obj, border_width, _M); \
        _border_color(obj, border_color, _M); \
        lv_obj_set_style_shadow_width(obj, shadow_w, _M); \
        lv_obj_set_style_shadow_color(obj, shadow_color, _M); \
        lv_obj_set_style_line_color(obj, line_color, _M); \
        _radius(obj, radius, _M); \
    } while (0);

//--------------------------------------------------
// Abbreviated LVGL Function Macros (GETTERS)
//--------------------------------------------------

// --- General Object Properties ---
#define size_(obj)          lv_obj_get_size(obj)
#define width_(obj)         lv_obj_get_width(obj)
#define height_(obj)        lv_obj_get_height(obj)
#define x_(obj)             lv_obj_get_x(obj)
#define y_(obj)             lv_obj_get_y(obj)
#define self_size_valid_(obj) lv_obj_is_layout_valid(obj) //check if size is valid (layouting)

// --- Widget-Specific Getters ---

// Label
#define label_text_(obj)    lv_label_get_text(obj)

// Slider
#define slider_value_(obj)  lv_slider_get_value(obj)

// Switch
#define switch_state_(obj) lv_switch_get_state(obj)

// Textarea
#define textarea_text_(obj)  lv_textarea_get_text(obj)
#define textarea_cursor_pos_(obj) lv_textarea_get_cursor_pos(obj)

// Checkbox
#define checkbox_is_checked_(obj)   lv_checkbox_is_checked(obj)

// Dropdown
#define dropdown_selected_(obj)     lv_dropdown_get_selected(obj)
#define dropdown_text_(obj)    lv_dropdown_get_text(obj) // get the selected text

// Roller
#define roller_selected_(obj)    lv_roller_get_selected(obj)
#define roller_option_cnt_(obj)   lv_roller_get_option_cnt(obj)

// Misc
#define _sset(name, obj, v) lv_obj_set_style_##name(obj, v, LV_STATE_DEFAULT)
#define _sget(name, obj, v) lv_obj_get_style_##name(obj, v, LV_STATE_DEFAULT)

#define _pad_row(...) _sset(pad_row, __VA_ARGS__)
#define _pad_column(...) _sset(pad_column, __VA_ARGS__)

#define _center lv_obj_center


//--------------------------------------------------
// Flex Layout Helpers
//--------------------------------------------------

#define _flex_flow(obj, flow)               lv_obj_set_flex_flow(obj, flow)
#define _flex_grow(obj, grow)              lv_obj_set_flex_grow(obj, grow)
#define _flex_align(obj, main_place, cross_place, track_cross_place) \
    lv_obj_set_flex_align(obj, main_place, cross_place, track_cross_place)

// Helper macros for common flex configurations:
#define _flex_row(obj)                     _flex_flow(obj, LV_FLEX_FLOW_ROW)
#define _flex_column(obj)                  _flex_flow(obj, LV_FLEX_FLOW_COLUMN)
#define _flex_row_wrap(obj)                _flex_flow(obj, LV_FLEX_FLOW_ROW_WRAP)
#define _flex_column_wrap(obj)             _flex_flow(obj, LV_FLEX_FLOW_COLUMN_WRAP)
#define _flex_center_all(obj)               _flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER)
#define _flex_justify_start(obj)          _flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START) // consistent naming.
#define _flex_justify_end(obj)            _flex_align(obj, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START) // consistent naming.
#define _flex_space_between(obj)          _flex_align(obj, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START) // consistent naming.
#define _flex_space_around(obj)           _flex_align(obj, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START) // consistent naming.
#define _flex_space_evenly(obj)           _flex_align(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START)    // consistent naming.

//--------------------------------------------------
// Grid Layout Helpers
//--------------------------------------------------

#define _cols(...) { __VA_ARGS__ }
#define _rows(...) { __VA_ARGS__ }
#define _grid_template(obj, cols, rows) do { \
    lv_coord_t mcols[] = cols; lv_coord_t mrows[] = rows; \
    mcols[(sizeof(mcols) / sizeof(mcols[0])) - 1] = LV_GRID_TEMPLATE_LAST; mrows[(sizeof(mrows) / sizeof(mrows[0])) - 1] = LV_GRID_TEMPLATE_LAST; \
    lv_obj_set_grid_dsc_array(obj, mcols, mrows); \
    } while (0);
#define _grid_place_items(obj, col_place, row_place) lv_obj_set_grid_align(obj, col_place, row_place)

// Set grid cell position and span for a *child* object.
#define _grid_cell(child, col_start, col_end, row_start, row_end) \
    do { \
        lv_obj_set_grid_cell(child, LV_GRID_ALIGN_START, col_start, col_end - col_start,  LV_GRID_ALIGN_START, row_start, row_end - row_start); \
    } while(0)

#define _grid_col_start(child, col)     lv_obj_set_grid_column_start(child, col)
#define _grid_col_end(child, col)       lv_obj_set_grid_column_end(child, col)
#define _grid_row_start(child, row)      lv_obj_set_grid_row_start(child, row)
#define _grid_row_end(child, row)        lv_obj_set_grid_row_end(child, row)

#define _grid_align_stretch_all(obj)  _grid_place_items(obj, LV_GRID_ALIGN_STRETCH, LV_GRID_ALIGN_STRETCH);
#define _grid_align_center_all(obj)  _grid_place_items(obj, LV_GRID_ALIGN_CENTER, LV_GRID_ALIGN_CENTER);
#define _grid_align_start_all(obj)  _grid_place_items(obj, LV_GRID_ALIGN_START, LV_GRID_ALIGN_START);
#define _grid_align_end_all(obj)  _grid_place_items(obj, LV_GRID_ALIGN_END, LV_GRID_ALIGN_END);

#define __container(parent) \
    ({                                                          \
        lv_obj_t *obj = lv_obj_create(parent);                  \
        if (obj == NULL) {                                      \
            LV_LOG_ERROR("Failed to create container");         \
        }                                                       \
        _size(obj, LV_PCT(100), LV_PCT(100));                   \
        _flag(obj, LV_OBJ_FLAG_SCROLLABLE, false);              \
        _maximize_client_area(obj);                             \
        lv_obj_set_style_pad_row(obj, 0, LV_STATE_DEFAULT);     \
        lv_obj_set_style_pad_column(obj, 0, LV_STATE_DEFAULT);  \
        obj;                                                    \
    })

lv_obj_t *_maximize_client_area(lv_obj_t *obj);
void dbg_layout(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif // UI_HELPERS_H