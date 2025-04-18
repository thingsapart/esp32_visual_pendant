#ifndef VIEW_DEFINE_MACROS_H
#define VIEW_DEFINE_MACROS_H

#include "lvgl.h"
#include "lv_vfl.h" // Need the view map definition

// --- Configuration ---
#ifndef dv_max_args_per_item // Max args for container, widget, or selector block
#define dv_max_args_per_item 64
#endif

// --- Internal Context Variables ---
// (Defined within view scope)
// static lv_obj_t* _dv_parent_obj = NULL;
// static lv_vfl_view_map_t* _dv_view_map_ptr = NULL;
// static size_t* _dv_view_map_count_ptr = NULL;
// static lv_obj_t* _dv_current_widget = NULL;
// static lv_style_selector_t _dv_current_selector = LV_PART_MAIN | LV_STATE_DEFAULT;

// --- Internal Argument Processing Macros (Recursive) ---
#define _process_args0()
#define _process_args1(A) A
#define _process_args2(A, ...) A _process_args1(__VA_ARGS__)
#define _process_args3(A, ...) A _process_args2(__VA_ARGS__)
#define _process_args4(A, ...) A _process_args3(__VA_ARGS__)
#define _process_args5(A, ...) A _process_args4(__VA_ARGS__)
#define _process_args6(A, ...) A _process_args5(__VA_ARGS__)
#define _process_args7(A, ...) A _process_args6(__VA_ARGS__)
#define _process_args8(A, ...) A _process_args7(__VA_ARGS__)
#define _process_args9(A, ...) A _process_args8(__VA_ARGS__)
#define _process_args10(A, ...) A _process_args9(__VA_ARGS__)
#define _process_args11(A, ...) A _process_args10(__VA_ARGS__)
#define _process_args12(A, ...) A _process_args11(__VA_ARGS__)
#define _process_args13(A, ...) A _process_args12(__VA_ARGS__)
#define _process_args14(A, ...) A _process_args13(__VA_ARGS__)
#define _process_args15(A, ...) A _process_args14(__VA_ARGS__)
#define _process_args16(A, ...) A _process_args15(__VA_ARGS__)
#define _process_args17(A, ...) A _process_args16(__VA_ARGS__)
#define _process_args18(A, ...) A _process_args17(__VA_ARGS__)
#define _process_args19(A, ...) A _process_args18(__VA_ARGS__)
#define _process_args20(A, ...) A _process_args19(__VA_ARGS__)
#define _process_args21(A, ...) A _process_args20(__VA_ARGS__)
#define _process_args22(A, ...) A _process_args21(__VA_ARGS__)
#define _process_args23(A, ...) A _process_args22(__VA_ARGS__)
#define _process_args24(A, ...) A _process_args23(__VA_ARGS__)
#define _process_args25(A, ...) A _process_args24(__VA_ARGS__)
#define _process_args26(A, ...) A _process_args25(__VA_ARGS__)
#define _process_args27(A, ...) A _process_args26(__VA_ARGS__)
#define _process_args28(A, ...) A _process_args27(__VA_ARGS__)
#define _process_args29(A, ...) A _process_args28(__VA_ARGS__)
#define _process_args30(A, ...) A _process_args29(__VA_ARGS__)
#define _process_args31(A, ...) A _process_args30(__VA_ARGS__)
#define _process_args32(A, ...) A _process_args31(__VA_ARGS__)
#define _process_args33(A, ...) A _process_args32(__VA_ARGS__)
#define _process_args34(A, ...) A _process_args33(__VA_ARGS__)
#define _process_args35(A, ...) A _process_args34(__VA_ARGS__)
#define _process_args36(A, ...) A _process_args35(__VA_ARGS__)
#define _process_args37(A, ...) A _process_args36(__VA_ARGS__)
#define _process_args38(A, ...) A _process_args37(__VA_ARGS__)
#define _process_args39(A, ...) A _process_args38(__VA_ARGS__)
#define _process_args40(A, ...) A _process_args39(__VA_ARGS__)
#define _process_args41(A, ...) A _process_args40(__VA_ARGS__)
#define _process_args42(A, ...) A _process_args41(__VA_ARGS__)
#define _process_args43(A, ...) A _process_args42(__VA_ARGS__)
#define _process_args44(A, ...) A _process_args43(__VA_ARGS__)
#define _process_args45(A, ...) A _process_args44(__VA_ARGS__)
#define _process_args46(A, ...) A _process_args45(__VA_ARGS__)
#define _process_args47(A, ...) A _process_args46(__VA_ARGS__)
#define _process_args48(A, ...) A _process_args47(__VA_ARGS__)
#define _process_args49(A, ...) A _process_args48(__VA_ARGS__)
#define _process_args50(A, ...) A _process_args49(__VA_ARGS__)
#define _process_args51(A, ...) A _process_args50(__VA_ARGS__)
#define _process_args52(A, ...) A _process_args51(__VA_ARGS__)
#define _process_args53(A, ...) A _process_args52(__VA_ARGS__)
#define _process_args54(A, ...) A _process_args53(__VA_ARGS__)
#define _process_args55(A, ...) A _process_args54(__VA_ARGS__)
#define _process_args56(A, ...) A _process_args55(__VA_ARGS__)
#define _process_args57(A, ...) A _process_args56(__VA_ARGS__)
#define _process_args58(A, ...) A _process_args57(__VA_ARGS__)
#define _process_args59(A, ...) A _process_args58(__VA_ARGS__)
#define _process_args60(A, ...) A _process_args59(__VA_ARGS__)
#define _process_args61(A, ...) A _process_args60(__VA_ARGS__)
#define _process_args62(A, ...) A _process_args61(__VA_ARGS__)
#define _process_args63(A, ...) A _process_args62(__VA_ARGS__)
#define _process_args64(A, ...) A _process_args63(__VA_ARGS__)

// --- Argument Counting Macro ---
// --- (Keep the _nargs_seq and _nargs definitions matching the highest N) ---
#define _nargs_seq( \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, \
    _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, \
    _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, \
    _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, \
    _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, \
    _61, _62, _63, _64, N, ...) N
#define _nargs(...) _nargs_seq(__VA_ARGS__, \
    64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, \
    49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, \
    34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, \
    19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

// --- Dispatcher ---
#define _process_vargs(N, ...) _process_args##N(__VA_ARGS__)
#define _process_dispatcher(N, ...) _process_vargs(N, __VA_ARGS__)
#define _process_args(...) _process_dispatcher(_nargs(__VA_ARGS__), __VA_ARGS__)


// --- Parent Container Property Macros ---
// These operate on _dv_parent_obj
// Setters (prefixed with _)
#define _parent_size(w, h)              lv_obj_set_size(_dv_parent_obj, w, h);
#define _parent_align(align, x, y)      lv_obj_align(_dv_parent_obj, align, x, y);
#define _parent_style_border_width(w)   lv_obj_set_style_border_width(_dv_parent_obj, w, LV_PART_MAIN);
#define _parent_style_border_color(c)   lv_obj_set_style_border_color(_dv_parent_obj, c, LV_PART_MAIN);
#define _parent_style_pad_all(p)        lv_obj_set_style_pad_all(_dv_parent_obj, p, LV_PART_MAIN);
#define _parent_style_pad_ver(p)        lv_obj_set_style_pad_ver(_dv_parent_obj, p, LV_PART_MAIN);
#define _parent_style_pad_hor(p)        lv_obj_set_style_pad_hor(_dv_parent_obj, p, LV_PART_MAIN);
// Add more _parent_style_xxx as needed

// --- Widget Definition Macros ---
// These create a widget, add it to the map, set _dv_current_widget, reset _dv_current_selector,
// and process the remaining arguments (__VA_ARGS__) which include direct properties and styles.
#define _setup_widget(WidgetType, CreateFunc, VarName, ...) \
    VarName = CreateFunc(_dv_parent_obj); \
    _dv_current_widget = VarName; \
    _dv_current_selector = LV_PART_MAIN | LV_STATE_DEFAULT; /* Reset selector for this widget */ \
    _process_args(__VA_ARGS__) /* Process properties/styles */

#define obj(VarName, ...)     _setup_widget(lv_obj_t, lv_obj_create, VarName, __VA_ARGS__)
#define div(VarName, ...)     _setup_widget(lv_obj_t, lv_obj_create, VarName, __VA_ARGS__)
#define label(VarName, Text, ...)   _setup_widget(lv_obj_t, lv_label_create, VarName, _text(Text), __VA_ARGS__)
#define button(VarName, ...)  _setup_widget(lv_obj_t, lv_btn_create, VarName, __VA_ARGS__)
#define list(VarName, ...)  _setup_widget(lv_obj_t, lv_list_create, VarName, __VA_ARGS__)
#define textarea(VarName, ...) _setup_widget(lv_obj_t, lv_textarea_create, VarName, __VA_ARGS__)
// ... Add other widget creation macros (switch, slider, image, etc.) following the same pattern.
// Example:
// #define switch(VarName, ...) _setup_widget(lv_obj_t, lv_switch_create, VarName, __VA_ARGS__)
// #define image(VarName, Src, ...) _setup_widget(lv_obj_t, lv_image_create, VarName, _src(Src), __VA_ARGS__)

// --- Widget Direct Property/Function Setter Macros (prefixed with _) ---
#define _size(w, h)                 lv_obj_set_size(_dv_current_widget, w, h);
#define _width(val)                 lv_obj_set_width(_dv_current_widget, val);
#define _height(val)                lv_obj_set_height(_dv_current_widget, val);
#define _align(align, x, y)         lv_obj_align(_dv_current_widget, align, x, y);
#define _add_flag(f)                lv_obj_add_flag(_dv_current_widget, f);
#define _clear_flag(f)              lv_obj_clear_flag(_dv_current_widget, f);
#define _add_state(s)               lv_obj_add_state(_dv_current_widget, s);
#define _clear_state(s)             lv_obj_clear_state(_dv_current_widget, s);

// Widget-specific direct property setters
#define _text(t)                    lv_label_set_text(_dv_current_widget, t); // Use specific setter if widget type known, otherwise generic
#define _label_text(t)              lv_label_set_text(_dv_current_widget, t);
#define _textarea_text(t)           lv_textarea_set_text(_dv_current_widget, t);
#define _placeholder_text(t)        lv_textarea_set_placeholder_text(_dv_current_widget, t);
#define _one_line(b)                lv_textarea_set_one_line(_dv_current_widget, b);
#define _image_src(s)               lv_image_set_src(_dv_current_widget, s);
// #define _button_label_text(t)    do { lv_obj_t* lbl = lv_obj_get_child(_dv_current_widget, 0); if(lbl && lv_obj_check_type(lbl, &lv_label_class)) lv_label_set_text(lbl, t); } while(0)


// --- Widget Direct Property/Function Getter Macros (suffixed with _) ---
// Note: Getters typically don't take arguments in this context, they return a value.
// They operate on _dv_current_widget.
#define size_()                     lv_obj_get_size(_dv_current_widget) // Returns lv_point_t
#define width_()                    lv_obj_get_width(_dv_current_widget)
#define height_()                   lv_obj_get_height(_dv_current_widget)
#define x_()                        lv_obj_get_x(_dv_current_widget)
#define y_()                        lv_obj_get_y(_dv_current_widget)
#define content_width_()            lv_obj_get_content_width(_dv_current_widget)
#define content_height_()           lv_obj_get_content_height(_dv_current_widget)
#define self_width_()               lv_obj_get_self_width(_dv_current_widget)
#define self_height_()              lv_obj_get_self_height(_dv_current_widget)
#define has_flag_(f)                lv_obj_has_flag(_dv_current_widget, f)
#define has_state_(s)               lv_obj_has_state(_dv_current_widget, s)
#define state_()                    lv_obj_get_state(_dv_current_widget)

// Widget-specific direct property getters
#define label_text_()               lv_label_get_text(_dv_current_widget)
#define textarea_text_()            lv_textarea_get_text(_dv_current_widget)
#define placeholder_text_()         lv_textarea_get_placeholder_text(_dv_current_widget)
#define one_line_()                 lv_textarea_get_one_line(_dv_current_widget)
#define image_src_()                lv_image_get_src(_dv_current_widget)


// --- Widget Direct Function Macros (no prefix/suffix) ---
// These call actions or setup functions that aren't simple setters/getters
#define center()                    lv_obj_center(_dv_current_widget);
#define scroll_to_view(anim)        lv_obj_scroll_to_view(_dv_current_widget, anim);
#define add_event_cb(cb, filter, data) lv_obj_add_event_cb(_dv_current_widget, cb, filter, data);


// --- Widget Style Setter Macros (prefixed with _) ---
// These call lv_obj_set_style_... functions using _dv_current_widget and the current _dv_current_selector.
// Size & Position Styles
#define _min_width(value)               lv_obj_set_style_min_width(_dv_current_widget, value, _dv_current_selector);
#define _max_width(value)               lv_obj_set_style_max_width(_dv_current_widget, value, _dv_current_selector);
#define _min_height(value)              lv_obj_set_style_min_height(_dv_current_widget, value, _dv_current_selector);
#define _max_height(value)              lv_obj_set_style_max_height(_dv_current_widget, value, _dv_current_selector);
#define _length(value)                  lv_obj_set_style_length(_dv_current_widget, value, _dv_current_selector);
#define _x(value)                       lv_obj_set_style_x(_dv_current_widget, value, _dv_current_selector);
#define _y(value)                       lv_obj_set_style_y(_dv_current_widget, value, _dv_current_selector);

// Transform Styles
#define _transform_width(value)         lv_obj_set_style_transform_width(_dv_current_widget, value, _dv_current_selector);
#define _transform_height(value)        lv_obj_set_style_transform_height(_dv_current_widget, value, _dv_current_selector);
#define _translate_x(value)             lv_obj_set_style_translate_x(_dv_current_widget, value, _dv_current_selector);
#define _translate_y(value)             lv_obj_set_style_translate_y(_dv_current_widget, value, _dv_current_selector);
#define _translate_radial(value)        lv_obj_set_style_translate_radial(_dv_current_widget, value, _dv_current_selector);
#define _transform_scale_x(value)       lv_obj_set_style_transform_scale_x(_dv_current_widget, value, _dv_current_selector);
#define _transform_scale_y(value)       lv_obj_set_style_transform_scale_y(_dv_current_widget, value, _dv_current_selector);
#define _transform_rotation(value)      lv_obj_set_style_transform_rotation(_dv_current_widget, value, _dv_current_selector);
#define _transform_pivot_x(value)       lv_obj_set_style_transform_pivot_x(_dv_current_widget, value, _dv_current_selector);
#define _transform_pivot_y(value)       lv_obj_set_style_transform_pivot_y(_dv_current_widget, value, _dv_current_selector);
#define _transform_skew_x(value)        lv_obj_set_style_transform_skew_x(_dv_current_widget, value, _dv_current_selector);
#define _transform_skew_y(value)        lv_obj_set_style_transform_skew_y(_dv_current_widget, value, _dv_current_selector);
// Padding Styles
#define _pad_top(value)                 lv_obj_set_style_pad_top(_dv_current_widget, value, _dv_current_selector);
#define _pad_bottom(value)              lv_obj_set_style_pad_bottom(_dv_current_widget, value, _dv_current_selector);
#define _pad_left(value)                lv_obj_set_style_pad_left(_dv_current_widget, value, _dv_current_selector);
#define _pad_right(value)               lv_obj_set_style_pad_right(_dv_current_widget, value, _dv_current_selector);
#define _pad_row(value)                 lv_obj_set_style_pad_row(_dv_current_widget, value, _dv_current_selector);
#define _pad_column(value)              lv_obj_set_style_pad_column(_dv_current_widget, value, _dv_current_selector);
#define _pad_radial(value)              lv_obj_set_style_pad_radial(_dv_current_widget, value, _dv_current_selector);
#define _pad_all(value)                 lv_obj_set_style_pad_all(_dv_current_widget, value, _dv_current_selector);
#define _pad_hor(value)                 lv_obj_set_style_pad_hor(_dv_current_widget, value, _dv_current_selector);
#define _pad_ver(value)                 lv_obj_set_style_pad_ver(_dv_current_widget, value, _dv_current_selector);
// Margin Styles
#define _margin_top(value)              lv_obj_set_style_margin_top(_dv_current_widget, value, _dv_current_selector);
#define _margin_bottom(value)           lv_obj_set_style_margin_bottom(_dv_current_widget, value, _dv_current_selector);
#define _margin_left(value)             lv_obj_set_style_margin_left(_dv_current_widget, value, _dv_current_selector);
#define _margin_right(value)            lv_obj_set_style_margin_right(_dv_current_widget, value, _dv_current_selector);
// Background Styles
#define _bg_color(value)                lv_obj_set_style_bg_color(_dv_current_widget, value, _dv_current_selector);
#define _bg_opa(value)                  lv_obj_set_style_bg_opa(_dv_current_widget, value, _dv_current_selector);
#define _bg_grad_color(value)           lv_obj_set_style_bg_grad_color(_dv_current_widget, value, _dv_current_selector);
#define _bg_grad_dir(value)             lv_obj_set_style_bg_grad_dir(_dv_current_widget, value, _dv_current_selector);
#define _bg_main_stop(value)            lv_obj_set_style_bg_main_stop(_dv_current_widget, value, _dv_current_selector);
#define _bg_grad_stop(value)            lv_obj_set_style_bg_grad_stop(_dv_current_widget, value, _dv_current_selector);
#define _bg_main_opa(value)             lv_obj_set_style_bg_main_opa(_dv_current_widget, value, _dv_current_selector);
#define _bg_grad_opa(value)             lv_obj_set_style_bg_grad_opa(_dv_current_widget, value, _dv_current_selector);
#define _bg_grad(value)                 lv_obj_set_style_bg_grad(_dv_current_widget, value, _dv_current_selector);
#define _bg_image_src(value)            lv_obj_set_style_bg_image_src(_dv_current_widget, value, _dv_current_selector);
#define _bg_image_opa(value)            lv_obj_set_style_bg_image_opa(_dv_current_widget, value, _dv_current_selector);
#define _bg_image_recolor(value)        lv_obj_set_style_bg_image_recolor(_dv_current_widget, value, _dv_current_selector);
#define _bg_image_recolor_opa(value)    lv_obj_set_style_bg_image_recolor_opa(_dv_current_widget, value, _dv_current_selector);
#define _bg_image_tiled(value)          lv_obj_set_style_bg_image_tiled(_dv_current_widget, value, _dv_current_selector);
// Border Styles
#define _border_color(value)            lv_obj_set_style_border_color(_dv_current_widget, value, _dv_current_selector);
#define _border_opa(value)              lv_obj_set_style_border_opa(_dv_current_widget, value, _dv_current_selector);
#define _border_width(value)            lv_obj_set_style_border_width(_dv_current_widget, value, _dv_current_selector);
#define _border_side(value)             lv_obj_set_style_border_side(_dv_current_widget, value, _dv_current_selector);
#define _border_post(value)             lv_obj_set_style_border_post(_dv_current_widget, value, _dv_current_selector);
// Outline Styles
#define _outline_width(value)           lv_obj_set_style_outline_width(_dv_current_widget, value, _dv_current_selector);
#define _outline_color(value)           lv_obj_set_style_outline_color(_dv_current_widget, value, _dv_current_selector);
#define _outline_opa(value)             lv_obj_set_style_outline_opa(_dv_current_widget, value, _dv_current_selector);
#define _outline_pad(value)             lv_obj_set_style_outline_pad(_dv_current_widget, value, _dv_current_selector);
// Shadow Styles
#define _shadow_width(value)            lv_obj_set_style_shadow_width(_dv_current_widget, value, _dv_current_selector);
#define _shadow_offset_x(value)         lv_obj_set_style_shadow_offset_x(_dv_current_widget, value, _dv_current_selector);
#define _shadow_offset_y(value)         lv_obj_set_style_shadow_offset_y(_dv_current_widget, value, _dv_current_selector);
#define _shadow_spread(value)           lv_obj_set_style_shadow_spread(_dv_current_widget, value, _dv_current_selector);
#define _shadow_color(value)            lv_obj_set_style_shadow_color(_dv_current_widget, value, _dv_current_selector);
#define _shadow_opa(value)              lv_obj_set_style_shadow_opa(_dv_current_widget, value, _dv_current_selector);
// Image Styles
#define _image_opa(value)               lv_obj_set_style_image_opa(_dv_current_widget, value, _dv_current_selector);
#define _image_recolor(value)           lv_obj_set_style_image_recolor(_dv_current_widget, value, _dv_current_selector);
#define _image_recolor_opa(value)       lv_obj_set_style_image_recolor_opa(_dv_current_widget, value, _dv_current_selector);
// Line Styles
#define _line_width(value)              lv_obj_set_style_line_width(_dv_current_widget, value, _dv_current_selector);
#define _line_dash_width(value)         lv_obj_set_style_line_dash_width(_dv_current_widget, value, _dv_current_selector);
#define _line_dash_gap(value)           lv_obj_set_style_line_dash_gap(_dv_current_widget, value, _dv_current_selector);
#define _line_rounded(value)            lv_obj_set_style_line_rounded(_dv_current_widget, value, _dv_current_selector);
#define _line_color(value)              lv_obj_set_style_line_color(_dv_current_widget, value, _dv_current_selector);
#define _line_opa(value)                lv_obj_set_style_line_opa(_dv_current_widget, value, _dv_current_selector);
// Arc Styles
#define _arc_width(value)               lv_obj_set_style_arc_width(_dv_current_widget, value, _dv_current_selector);
#define _arc_rounded(value)             lv_obj_set_style_arc_rounded(_dv_current_widget, value, _dv_current_selector);
#define _arc_color(value)               lv_obj_set_style_arc_color(_dv_current_widget, value, _dv_current_selector);
#define _arc_opa(value)                 lv_obj_set_style_arc_opa(_dv_current_widget, value, _dv_current_selector);
#define _arc_image_src(value)           lv_obj_set_style_arc_image_src(_dv_current_widget, value, _dv_current_selector);
// Text Styles
#define _text_color(value)              lv_obj_set_style_text_color(_dv_current_widget, value, _dv_current_selector);
#define _text_opa(value)                lv_obj_set_style_text_opa(_dv_current_widget, value, _dv_current_selector);
#define _text_font(value)               lv_obj_set_style_text_font(_dv_current_widget, value, _dv_current_selector);
#define _text_letter_space(value)       lv_obj_set_style_text_letter_space(_dv_current_widget, value, _dv_current_selector);
#define _text_line_space(value)         lv_obj_set_style_text_line_space(_dv_current_widget, value, _dv_current_selector);
#define _text_decor(value)              lv_obj_set_style_text_decor(_dv_current_widget, value, _dv_current_selector);
#define _text_align(value)              lv_obj_set_style_text_align(_dv_current_widget, value, _dv_current_selector);
#if LV_USE_FONT_SUBPX // Check LVGL config
#define _text_outline_stroke_color(value) lv_obj_set_style_text_outline_stroke_color(_dv_current_widget, value, _dv_current_selector);
#define _text_outline_stroke_width(value) lv_obj_set_style_text_outline_stroke_width(_dv_current_widget, value, _dv_current_selector);
#define _text_outline_stroke_opa(value)  lv_obj_set_style_text_outline_stroke_opa(_dv_current_widget, value, _dv_current_selector);
#endif // LV_USE_FONT_SUBPX
// Miscellaneous Styles
#define _radius(value)                  lv_obj_set_style_radius(_dv_current_widget, value, _dv_current_selector);
#define _radial_offset(value)           lv_obj_set_style_radial_offset(_dv_current_widget, value, _dv_current_selector);
#define _clip_corner(value)             lv_obj_set_style_clip_corner(_dv_current_widget, value, _dv_current_selector);
#define _opa(value)                     lv_obj_set_style_opa(_dv_current_widget, value, _dv_current_selector);
#define _opa_layered(value)             lv_obj_set_style_opa_layered(_dv_current_widget, value, _dv_current_selector);
#define _color_filter_dsc(value)        lv_obj_set_style_color_filter_dsc(_dv_current_widget, value, _dv_current_selector);
#define _color_filter_opa(value)        lv_obj_set_style_color_filter_opa(_dv_current_widget, value, _dv_current_selector);
#define _anim(value)                    lv_obj_set_style_anim(_dv_current_widget, value, _dv_current_selector);
#define _anim_duration(value)           lv_obj_set_style_anim_duration(_dv_current_widget, value, _dv_current_selector);
#define _transition(value)              lv_obj_set_style_transition(_dv_current_widget, value, _dv_current_selector);
#define _blend_mode(value)              lv_obj_set_style_blend_mode(_dv_current_widget, value, _dv_current_selector);
#define _layout(value)                  lv_obj_set_style_layout(_dv_current_widget, value, _dv_current_selector);
#define _base_dir(value)                lv_obj_set_style_base_dir(_dv_current_widget, value, _dv_current_selector);
#define _bitmap_mask_src(value)         lv_obj_set_style_bitmap_mask_src(_dv_current_widget, value, _dv_current_selector);
#define _rotary_sensitivity(value)      lv_obj_set_style_rotary_sensitivity(_dv_current_widget, value, _dv_current_selector);
// Flexbox Styles
#define _flex_flow(value)               lv_obj_set_style_flex_flow(_dv_current_widget, value, _dv_current_selector);
#define _flex_main_place(value)         lv_obj_set_style_flex_main_place(_dv_current_widget, value, _dv_current_selector);
#define _flex_cross_place(value)        lv_obj_set_style_flex_cross_place(_dv_current_widget, value, _dv_current_selector);
#define _flex_track_place(value)        lv_obj_set_style_flex_track_place(_dv_current_widget, value, _dv_current_selector);
#define _flex_grow(value)               lv_obj_set_style_flex_grow(_dv_current_widget, value, _dv_current_selector);
// Grid Styles
#define _grid_column_dsc_array(value)   lv_obj_set_style_grid_column_dsc_array(_dv_current_widget, value, _dv_current_selector);
#define _grid_column_align(value)       lv_obj_set_style_grid_column_align(_dv_current_widget, value, _dv_current_selector);
#define _grid_row_dsc_array(value)      lv_obj_set_style_grid_row_dsc_array(_dv_current_widget, value, _dv_current_selector);
#define _grid_row_align(value)          lv_obj_set_style_grid_row_align(_dv_current_widget, value, _dv_current_selector);
#define _grid_cell_column_pos(value)    lv_obj_set_style_grid_cell_column_pos(_dv_current_widget, value, _dv_current_selector);
#define _grid_cell_x_align(value)       lv_obj_set_style_grid_cell_x_align(_dv_current_widget, value, _dv_current_selector);
#define _grid_cell_column_span(value)   lv_obj_set_style_grid_cell_column_span(_dv_current_widget, value, _dv_current_selector);
#define _grid_cell_row_pos(value)       lv_obj_set_style_grid_cell_row_pos(_dv_current_widget, value, _dv_current_selector);
#define _grid_cell_y_align(value)       lv_obj_set_style_grid_cell_y_align(_dv_current_widget, value, _dv_current_selector);
#define _grid_cell_row_span(value)      lv_obj_set_style_grid_cell_row_span(_dv_current_widget, value, _dv_current_selector);
#define _grid_column_gap(value)         lv_obj_set_style_grid_column_gap(_dv_current_widget, value, _dv_current_selector);
#define _grid_row_gap(value)            lv_obj_set_style_grid_row_gap(_dv_current_widget, value, _dv_current_selector);


// --- Widget Style Getter Macros (suffixed with _) ---
// These call lv_obj_get_style_... functions using _dv_current_widget and the current _dv_current_selector.
// Note: Getters typically don't take arguments in this context, they return a value.
// Size & Position Styles
#define min_width_()                    lv_obj_get_style_min_width(_dv_current_widget, _dv_current_selector)
#define max_width_()                    lv_obj_get_style_max_width(_dv_current_widget, _dv_current_selector)
#define min_height_()                   lv_obj_get_style_min_height(_dv_current_widget, _dv_current_selector)
#define max_height_()                   lv_obj_get_style_max_height(_dv_current_widget, _dv_current_selector)
#define length_()                       lv_obj_get_style_length(_dv_current_widget, _dv_current_selector)
// Transform Styles
#define transform_width_()              lv_obj_get_style_transform_width(_dv_current_widget, _dv_current_selector)
#define transform_height_()             lv_obj_get_style_transform_height(_dv_current_widget, _dv_current_selector)
#define translate_x_()                 lv_obj_get_style_translate_x(_dv_current_widget, _dv_current_selector)
#define translate_y_()                 lv_obj_get_style_translate_y(_dv_current_widget, _dv_current_selector)
#define translate_radial_()             lv_obj_get_style_translate_radial(_dv_current_widget, _dv_current_selector)
#define transform_scale_x_()            lv_obj_get_style_transform_scale_x(_dv_current_widget, _dv_current_selector)
#define transform_scale_y_()            lv_obj_get_style_transform_scale_y(_dv_current_widget, _dv_current_selector)
#define transform_rotation_()           lv_obj_get_style_transform_rotation(_dv_current_widget, _dv_current_selector)
#define transform_pivot_x_()            lv_obj_get_style_transform_pivot_x(_dv_current_widget, _dv_current_selector)
#define transform_pivot_y_()            lv_obj_get_style_transform_pivot_y(_dv_current_widget, _dv_current_selector)
#define transform_skew_x_()             lv_obj_get_style_transform_skew_x(_dv_current_widget, _dv_current_selector)
#define transform_skew_y_()             lv_obj_get_style_transform_skew_y(_dv_current_widget, _dv_current_selector)
// Padding Styles
#define pad_top_()                      lv_obj_get_style_pad_top(_dv_current_widget, _dv_current_selector)
#define pad_bottom_()                   lv_obj_get_style_pad_bottom(_dv_current_widget, _dv_current_selector)
#define pad_left_()                     lv_obj_get_style_pad_left(_dv_current_widget, _dv_current_selector)
#define pad_right_()                    lv_obj_get_style_pad_right(_dv_current_widget, _dv_current_selector)
#define pad_row_()                      lv_obj_get_style_pad_row(_dv_current_widget, _dv_current_selector)
#define pad_column_()                   lv_obj_get_style_pad_column(_dv_current_widget, _dv_current_selector)
#define pad_radial_()                   lv_obj_get_style_pad_radial(_dv_current_widget, _dv_current_selector)
// No direct getters for pad_all, pad_hor, pad_ver
// Margin Styles
#define margin_top_()                   lv_obj_get_style_margin_top(_dv_current_widget, _dv_current_selector)
#define margin_bottom_()                lv_obj_get_style_margin_bottom(_dv_current_widget, _dv_current_selector)
#define margin_left_()                  lv_obj_get_style_margin_left(_dv_current_widget, _dv_current_selector)
#define margin_right_()                 lv_obj_get_style_margin_right(_dv_current_widget, _dv_current_selector)
// Background Styles
#define bg_color_()                     lv_obj_get_style_bg_color(_dv_current_widget, _dv_current_selector)
#define bg_opa_()                       lv_obj_get_style_bg_opa(_dv_current_widget, _dv_current_selector)
#define bg_grad_color_()                lv_obj_get_style_bg_grad_color(_dv_current_widget, _dv_current_selector)
#define bg_grad_dir_()                  lv_obj_get_style_bg_grad_dir(_dv_current_widget, _dv_current_selector)
#define bg_main_stop_()                 lv_obj_get_style_bg_main_stop(_dv_current_widget, _dv_current_selector)
#define bg_grad_stop_()                 lv_obj_get_style_bg_grad_stop(_dv_current_widget, _dv_current_selector)
#define bg_main_opa_()                  lv_obj_get_style_bg_main_opa(_dv_current_widget, _dv_current_selector)
#define bg_grad_opa_()                  lv_obj_get_style_bg_grad_opa(_dv_current_widget, _dv_current_selector)
#define bg_grad_()                      lv_obj_get_style_bg_grad(_dv_current_widget, _dv_current_selector) // Returns const lv_grad_dsc_t *
#define bg_image_src_()                 lv_obj_get_style_bg_image_src(_dv_current_widget, _dv_current_selector)
#define bg_image_opa_()                 lv_obj_get_style_bg_image_opa(_dv_current_widget, _dv_current_selector)
#define bg_image_recolor_()             lv_obj_get_style_bg_image_recolor(_dv_current_widget, _dv_current_selector)
#define bg_image_recolor_opa_()         lv_obj_get_style_bg_image_recolor_opa(_dv_current_widget, _dv_current_selector)
#define bg_image_tiled_()               lv_obj_get_style_bg_image_tiled(_dv_current_widget, _dv_current_selector)
// Border Styles
#define border_color_()                 lv_obj_get_style_border_color(_dv_current_widget, _dv_current_selector)
#define border_opa_()                   lv_obj_get_style_border_opa(_dv_current_widget, _dv_current_selector)
#define border_width_()                 lv_obj_get_style_border_width(_dv_current_widget, _dv_current_selector)
#define border_side_()                  lv_obj_get_style_border_side(_dv_current_widget, _dv_current_selector)
#define border_post_()                  lv_obj_get_style_border_post(_dv_current_widget, _dv_current_selector)
// Outline Styles
#define outline_width_()                lv_obj_get_style_outline_width(_dv_current_widget, _dv_current_selector)
#define outline_color_()                lv_obj_get_style_outline_color(_dv_current_widget, _dv_current_selector)
#define outline_opa_()                  lv_obj_get_style_outline_opa(_dv_current_widget, _dv_current_selector)
#define outline_pad_()                  lv_obj_get_style_outline_pad(_dv_current_widget, _dv_current_selector)
// Shadow Styles
#define shadow_width_()                 lv_obj_get_style_shadow_width(_dv_current_widget, _dv_current_selector)
#define shadow_offset_x_()              lv_obj_get_style_shadow_offset_x(_dv_current_widget, _dv_current_selector)
#define shadow_offset_y_()              lv_obj_get_style_shadow_offset_y(_dv_current_widget, _dv_current_selector)
#define shadow_spread_()                lv_obj_get_style_shadow_spread(_dv_current_widget, _dv_current_selector)
#define shadow_color_()                 lv_obj_get_style_shadow_color(_dv_current_widget, _dv_current_selector)
#define shadow_opa_()                   lv_obj_get_style_shadow_opa(_dv_current_widget, _dv_current_selector)
// Image Styles
#define image_opa_()                    lv_obj_get_style_image_opa(_dv_current_widget, _dv_current_selector)
#define image_recolor_()                lv_obj_get_style_image_recolor(_dv_current_widget, _dv_current_selector)
#define image_recolor_opa_()            lv_obj_get_style_image_recolor_opa(_dv_current_widget, _dv_current_selector)
// Line Styles
#define line_width_()                   lv_obj_get_style_line_width(_dv_current_widget, _dv_current_selector)
#define line_dash_width_()              lv_obj_get_style_line_dash_width(_dv_current_widget, _dv_current_selector)
#define line_dash_gap_()                lv_obj_get_style_line_dash_gap(_dv_current_widget, _dv_current_selector)
#define line_rounded_()                 lv_obj_get_style_line_rounded(_dv_current_widget, _dv_current_selector)
#define line_color_()                   lv_obj_get_style_line_color(_dv_current_widget, _dv_current_selector)
#define line_opa_()                     lv_obj_get_style_line_opa(_dv_current_widget, _dv_current_selector)
// Arc Styles
#define arc_width_()                    lv_obj_get_style_arc_width(_dv_current_widget, _dv_current_selector)
#define arc_rounded_()                  lv_obj_get_style_arc_rounded(_dv_current_widget, _dv_current_selector)
#define arc_color_()                    lv_obj_get_style_arc_color(_dv_current_widget, _dv_current_selector)
#define arc_opa_()                      lv_obj_get_style_arc_opa(_dv_current_widget, _dv_current_selector)
#define arc_image_src_()                lv_obj_get_style_arc_image_src(_dv_current_widget, _dv_current_selector)
// Text Styles
#define text_color_()                   lv_obj_get_style_text_color(_dv_current_widget, _dv_current_selector)
#define text_opa_()                     lv_obj_get_style_text_opa(_dv_current_widget, _dv_current_selector)
#define text_font_()                    lv_obj_get_style_text_font(_dv_current_widget, _dv_current_selector)
#define text_letter_space_()            lv_obj_get_style_text_letter_space(_dv_current_widget, _dv_current_selector)
#define text_line_space_()              lv_obj_get_style_text_line_space(_dv_current_widget, _dv_current_selector)
#define text_decor_()                   lv_obj_get_style_text_decor(_dv_current_widget, _dv_current_selector)
#define text_align_()                   lv_obj_get_style_text_align(_dv_current_widget, _dv_current_selector)
#if LV_USE_FONT_SUBPX // Check LVGL config
#define text_outline_stroke_color_()    lv_obj_get_style_text_outline_stroke_color(_dv_current_widget, _dv_current_selector);
#define text_outline_stroke_width_()    lv_obj_get_style_text_outline_stroke_width(_dv_current_widget, _dv_current_selector);
#define text_outline_stroke_opa_()     lv_obj_get_style_text_outline_stroke_opa(_dv_current_widget, _dv_current_selector);
#endif // LV_USE_FONT_SUBPX
// Miscellaneous Styles
#define radius_()                       lv_obj_get_style_radius(_dv_current_widget, _dv_current_selector)
#define radial_offset_()                lv_obj_get_style_radial_offset(_dv_current_widget, _dv_current_selector); // Added getter manually
#define clip_corner_()                  lv_obj_get_style_clip_corner(_dv_current_widget, _dv_current_selector)
#define opa_()                          lv_obj_get_style_opa(_dv_current_widget, _dv_current_selector)
#define opa_layered_()                  lv_obj_get_style_opa_layered(_dv_current_widget, _dv_current_selector)
#define color_filter_dsc_()             lv_obj_get_style_color_filter_dsc(_dv_current_widget, _dv_current_selector)
#define color_filter_opa_()             lv_obj_get_style_color_filter_opa(_dv_current_widget, _dv_current_selector)
#define anim_()                         lv_obj_get_style_anim(_dv_current_widget, _dv_current_selector)
#define anim_duration_()                lv_obj_get_style_anim_duration(_dv_current_widget, _dv_current_selector)
#define transition_()                   lv_obj_get_style_transition(_dv_current_widget, _dv_current_selector)
#define blend_mode_()                   lv_obj_get_style_blend_mode(_dv_current_widget, _dv_current_selector)
#define layout_()                       lv_obj_get_style_layout(_dv_current_widget, _dv_current_selector)
#define base_dir_()                     lv_obj_get_style_base_dir(_dv_current_widget, _dv_current_selector)
#define bitmap_mask_src_()              lv_obj_get_style_bitmap_mask_src(_dv_current_widget, _dv_current_selector)
#define rotary_sensitivity_()           lv_obj_get_style_rotary_sensitivity(_dv_current_widget, _dv_current_selector)
// Flexbox Styles
#define flex_flow_()                    lv_obj_get_style_flex_flow(_dv_current_widget, _dv_current_selector)
#define flex_main_place_()              lv_obj_get_style_flex_main_place(_dv_current_widget, _dv_current_selector)
#define flex_cross_place_()             lv_obj_get_style_flex_cross_place(_dv_current_widget, _dv_current_selector)
#define flex_track_place_()             lv_obj_get_style_flex_track_place(_dv_current_widget, _dv_current_selector)
#define flex_grow_()                    lv_obj_get_style_flex_grow(_dv_current_widget, _dv_current_selector)
// Grid Styles
#define grid_column_dsc_array_()        lv_obj_get_style_grid_column_dsc_array(_dv_current_widget, _dv_current_selector)
#define grid_column_align_()            lv_obj_get_style_grid_column_align(_dv_current_widget, _dv_current_selector)
#define grid_row_dsc_array_()           lv_obj_get_style_grid_row_dsc_array(_dv_current_widget, _dv_current_selector)
#define grid_row_align_()               lv_obj_get_style_grid_row_align(_dv_current_widget, _dv_current_selector)
#define grid_cell_column_pos_()         lv_obj_get_style_grid_cell_column_pos(_dv_current_widget, _dv_current_selector)
#define grid_cell_x_align_()            lv_obj_get_style_grid_cell_x_align(_dv_current_widget, _dv_current_selector)
#define grid_cell_column_span_()        lv_obj_get_style_grid_cell_column_span(_dv_current_widget, _dv_current_selector)
#define grid_cell_row_pos_()            lv_obj_get_style_grid_cell_row_pos(_dv_current_widget, _dv_current_selector)
#define grid_cell_y_align_()            lv_obj_get_style_grid_cell_y_align(_dv_current_widget, _dv_current_selector)
#define grid_cell_row_span_()           lv_obj_get_style_grid_cell_row_span(_dv_current_widget, _dv_current_selector)
#define grid_column_gap_()              lv_obj_get_style_grid_column_gap(_dv_current_widget, _dv_current_selector)
#define grid_row_gap_()                 lv_obj_get_style_grid_row_gap(_dv_current_widget, _dv_current_selector)


// --- Selector Block Macro ---
/**
 * @brief Creates a scope where subsequent style macros use the specified selector.
 *
 * @param selector_value The lv_style_selector_t value (e.g., LV_STATE_PRESSED, LV_PART_INDICATOR | LV_STATE_CHECKED).
 * @param ... Comma-separated list of style setter macros (e.g., _bg_color(...), _width(...)) to apply using this selector.
 */
#define selector(selector_value, ...) \
    do { \
        /* Save selector from the outer scope (widget default or previous selector) */ \
        lv_style_selector_t _dv_outer_selector = _dv_current_selector; \
        /* Set the new selector specifically for this block */ \
        _dv_current_selector = (selector_value); \
        /* Process the arguments passed to selector (...) using the new selector */ \
        _process_args(__VA_ARGS__) \
        /* Restore the outer selector once this block's arguments are processed */ \
        _dv_current_selector = _dv_outer_selector; \
    } while(0)

// --- Main Definition Macro ---
/**
 * @brief Defines LVGL objects, properties, styles (potentially scoped by selector), and populates a view map.
 *
 * Naming Convention:
 * - Macros are lower-case.
 * - Macros calling lv_obj_set_*(...) or lv_obj_set_style_*(...) are prefixed with '_'. (e.g., _width(100))
 * - Macros calling lv_obj_get_*(...) or lv_obj_get_style_*(...) are suffixed with '_'. (e.g., width_())
 * - Macros for actions or widget creation have no prefix/suffix. (e.g., center(), label(...))
 * - Parent-specific setters are prefixed with '_parent_'. (e.g., _parent_size(100, 50))
 *
 * Usage Example:
 *   view(map_array, map_count, container,
 *       _parent_style_pad_all(5),         // Apply style setter to container
 *       _layout(lv_layout_flex_create()), // Apply layout style setter to container
 *       _flex_flow(LV_FLEX_FLOW_ROW),     // Apply flex style setter to container
 *
 *       label(my_label, "Text",           // Define a label widget named my_label
 *           _width(100),                  // Style setter for default state
 *           _text_color(lv_color_black()),// Style setter for default state
 *           selector(LV_STATE_DISABLED,   // Start disabled state selector block
 *              _text_color(lv_color_grey()) // Style setter for disabled state
 *           ) // End selector block
 *       ), // End label block
 *
 *       button(my_button,                 // Define a button widget named my_button
 *           _add_flag(LV_OBJ_FLAG_CHECKABLE), // Direct property setter
 *           _width(LV_SIZE_CONTENT),      // Style setter for default state
 *           _height(30),                  // Style setter for default state
 *           _radius(4),                   // Style setter for default state
 *           selector(LV_PART_MAIN | LV_STATE_PRESSED, // Start pressed state selector block
 *              _bg_color(lv_palette_darken(LV_PALETTE_BLUE, 2)),
 *              _border_width(3)
 *           ), // End selector block
 *           selector(LV_PART_MAIN | LV_STATE_CHECKED, // Start checked state selector block
 *              _bg_color(lv_palette_main(LV_PALETTE_GREEN))
 *           ), // End selector block
 *
 *           // Add a label *inside* the button
 *           label(btn_label, "Click Me",   // Define nested label widget
 *               center(),                   // Action macro: center label in button
 *               _text_color(lv_color_white()), // Style setter for label's default state
 *               selector(LV_STATE_PRESSED,   // Selector block for label style when button is pressed
 *                  _text_color(lv_color_lighten(lv_color_white(), 50))
 *               ) // End selector block
 *           ) // End nested label block
 *       ) // End button block
 *   );
 *
 * @param ParentObj The lv_obj_t* container object where widgets defined at the top level will be created.
 * @param ... Comma-separated list of parent setters (_parent_...), layout setters (_layout, _flex_flow, etc.),
 *            widget creation (label, button, etc.), which can contain direct setters (_width, _add_flag),
 *            actions (center), style setters (_width, _bg_color, etc.), selector(...) blocks,
 *            and nested widget definitions. Getter macros (e.g., width_()) are generally not used
 *            within the view definition itself but might be used elsewhere to query properties.
 */
#define view(ParentObj, ...) \
    do { \
        lv_obj_t* _dv_parent_obj = (ParentObj); \
        lv_obj_t* _dv_current_widget = NULL; /* Track the widget being configured */ \
        lv_style_selector_t _dv_current_selector = LV_PART_MAIN | LV_STATE_DEFAULT; /* Track the selector */ \
        \
        /* Set the initial widget context to the parent for top-level setters */ \
        _dv_current_widget = _dv_parent_obj; \
        _dv_current_selector = LV_PART_MAIN | LV_STATE_DEFAULT; \
        \
        /* Process all top-level arguments */ \
        /* _parent_... macros use _dv_parent_obj directly */ \
        /* _layout, _flex_*, _grid_* etc. use _dv_current_widget (initially parent) */ \
        /* Widget macros (obj, label...) set _dv_current_widget internally and update _dv_parent_obj for nesting */ \
        _process_args(__VA_ARGS__) \
        \
    } while(0)


lv_obj_t *_maximize_client_area(lv_obj_t *obj);
#define __flag(obj, flag, enabled)                                              \
  ((enabled) ? lv_obj_add_flag(obj, flag) : lv_obj_clear_flag(obj, flag))
#define __hide(obj, hidden)                                                   \
  lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN) // Shortcut for hiding
#define __clickable(obj, enabled) __flag(obj, LV_OBJ_FLAG_CLICKABLE, enabled)
#define __scrollable(obj, enabled) __flag(obj, LV_OBJ_FLAG_SCROLLABLE, enabled)
#define __use_layout(obj, enabled)                                              \
  __flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT, !enabled)
#define __layout(obj, layout) lv_obj_set_layout(obj, layout)
#define __update_layout(obj) lv_obj_update_layout(obj)


#endif // VIEW_DEFINE_MACROS_H