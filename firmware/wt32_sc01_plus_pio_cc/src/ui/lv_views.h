#ifndef LV_VIEW_DEFINE_MACROS_H
#define LV_VIEW_DEFINE_MACROS_H

#include "lvgl.h"
#include "lv_vfl.h" // Need the view map definition

// --- Configuration ---
#ifndef LV_DV_MAX_WIDGETS
#define LV_DV_MAX_WIDGETS 32
#endif
#ifndef LV_DV_MAX_ARGS_PER_ITEM // Max args for container, widget, or selector block
#define LV_DV_MAX_ARGS_PER_ITEM 64
#endif

// --- Internal Context Variables ---
// (Defined within LV_VIEW scope)
// static lv_obj_t* _dv_parent_obj = NULL;
// static lv_vfl_view_map_t* _dv_view_map_ptr = NULL;
// static size_t* _dv_view_map_count_ptr = NULL;
// static lv_obj_t* _dv_current_widget = NULL;
// static lv_style_selector_t _dv_current_selector = LV_PART_MAIN | LV_STATE_DEFAULT;

// --- Internal Argument Processing Macros (Recursive) ---
#define _LV_DV_PROCESS_ARGS0()
#define _LV_DV_PROCESS_ARGS1(A) A
#define _LV_DV_PROCESS_ARGS2(A, ...) A _LV_DV_PROCESS_ARGS1(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS3(A, ...) A _LV_DV_PROCESS_ARGS2(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS4(A, ...) A _LV_DV_PROCESS_ARGS3(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS5(A, ...) A _LV_DV_PROCESS_ARGS4(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS6(A, ...) A _LV_DV_PROCESS_ARGS5(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS7(A, ...) A _LV_DV_PROCESS_ARGS6(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS8(A, ...) A _LV_DV_PROCESS_ARGS7(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS9(A, ...) A _LV_DV_PROCESS_ARGS8(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS10(A, ...) A _LV_DV_PROCESS_ARGS9(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS11(A, ...) A _LV_DV_PROCESS_ARGS10(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS12(A, ...) A _LV_DV_PROCESS_ARGS11(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS13(A, ...) A _LV_DV_PROCESS_ARGS12(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS14(A, ...) A _LV_DV_PROCESS_ARGS13(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS15(A, ...) A _LV_DV_PROCESS_ARGS14(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS16(A, ...) A _LV_DV_PROCESS_ARGS15(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS17(A, ...) A _LV_DV_PROCESS_ARGS16(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS18(A, ...) A _LV_DV_PROCESS_ARGS17(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS19(A, ...) A _LV_DV_PROCESS_ARGS18(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS20(A, ...) A _LV_DV_PROCESS_ARGS19(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS21(A, ...) A _LV_DV_PROCESS_ARGS20(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS22(A, ...) A _LV_DV_PROCESS_ARGS21(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS23(A, ...) A _LV_DV_PROCESS_ARGS22(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS24(A, ...) A _LV_DV_PROCESS_ARGS23(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS25(A, ...) A _LV_DV_PROCESS_ARGS24(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS26(A, ...) A _LV_DV_PROCESS_ARGS25(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS27(A, ...) A _LV_DV_PROCESS_ARGS26(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS28(A, ...) A _LV_DV_PROCESS_ARGS27(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS29(A, ...) A _LV_DV_PROCESS_ARGS28(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS30(A, ...) A _LV_DV_PROCESS_ARGS29(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS31(A, ...) A _LV_DV_PROCESS_ARGS30(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS32(A, ...) A _LV_DV_PROCESS_ARGS31(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS33(A, ...) A _LV_DV_PROCESS_ARGS32(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS34(A, ...) A _LV_DV_PROCESS_ARGS33(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS35(A, ...) A _LV_DV_PROCESS_ARGS34(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS36(A, ...) A _LV_DV_PROCESS_ARGS35(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS37(A, ...) A _LV_DV_PROCESS_ARGS36(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS38(A, ...) A _LV_DV_PROCESS_ARGS37(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS39(A, ...) A _LV_DV_PROCESS_ARGS38(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS40(A, ...) A _LV_DV_PROCESS_ARGS39(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS41(A, ...) A _LV_DV_PROCESS_ARGS40(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS42(A, ...) A _LV_DV_PROCESS_ARGS41(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS43(A, ...) A _LV_DV_PROCESS_ARGS42(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS44(A, ...) A _LV_DV_PROCESS_ARGS43(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS45(A, ...) A _LV_DV_PROCESS_ARGS44(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS46(A, ...) A _LV_DV_PROCESS_ARGS45(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS47(A, ...) A _LV_DV_PROCESS_ARGS46(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS48(A, ...) A _LV_DV_PROCESS_ARGS47(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS49(A, ...) A _LV_DV_PROCESS_ARGS48(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS50(A, ...) A _LV_DV_PROCESS_ARGS49(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS51(A, ...) A _LV_DV_PROCESS_ARGS50(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS52(A, ...) A _LV_DV_PROCESS_ARGS51(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS53(A, ...) A _LV_DV_PROCESS_ARGS52(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS54(A, ...) A _LV_DV_PROCESS_ARGS53(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS55(A, ...) A _LV_DV_PROCESS_ARGS54(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS56(A, ...) A _LV_DV_PROCESS_ARGS55(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS57(A, ...) A _LV_DV_PROCESS_ARGS56(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS58(A, ...) A _LV_DV_PROCESS_ARGS57(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS59(A, ...) A _LV_DV_PROCESS_ARGS58(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS60(A, ...) A _LV_DV_PROCESS_ARGS59(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS61(A, ...) A _LV_DV_PROCESS_ARGS60(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS62(A, ...) A _LV_DV_PROCESS_ARGS61(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS63(A, ...) A _LV_DV_PROCESS_ARGS62(__VA_ARGS__)
#define _LV_DV_PROCESS_ARGS64(A, ...) A _LV_DV_PROCESS_ARGS63(__VA_ARGS__)

// --- Argument Counting Macro ---
// --- (Keep the _LV_DV_NARGS_SEQ and _LV_DV_NARGS definitions matching the highest N) ---
#define _LV_DV_NARGS_SEQ( \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, \
    _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, \
    _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, \
    _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, \
    _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, \
    _61, _62, _63, _64, N, ...) N
#define _LV_DV_NARGS(...) _LV_DV_NARGS_SEQ(__VA_ARGS__, \
    64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, \
    49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, \
    34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, \
    19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

// --- Dispatcher ---
#define _LV_DV_PROCESS_VARGS(N, ...) _LV_DV_PROCESS_ARGS##N(__VA_ARGS__)
#define _LV_DV_PROCESS_DISPATCHER(N, ...) _LV_DV_PROCESS_VARGS(N, __VA_ARGS__)
#define _LV_DV_PROCESS_ARGS(...) _LV_DV_PROCESS_DISPATCHER(_LV_DV_NARGS(__VA_ARGS__), __VA_ARGS__)


// --- Parent Container Property Macros ---
// These operate on _dv_parent_obj
#define PARENT_SIZE(w, h)         lv_obj_set_size(_dv_parent_obj, w, h);
#define PARENT_ALIGN(align, x, y) lv_obj_align(_dv_parent_obj, align, x, y);
#define PARENT_STYLE_BORDER_WIDTH(w)    lv_obj_set_style_border_width(_dv_parent_obj, w, LV_PART_MAIN);
#define PARENT_STYLE_BORDER_COLOR(c)    lv_obj_set_style_border_color(_dv_parent_obj, c, LV_PART_MAIN);
#define PARENT_STYLE_PAD_ALL(p)         lv_obj_set_style_pad_all(_dv_parent_obj, p, LV_PART_MAIN);
#define PARENT_STYLE_PAD_VER(p)         lv_obj_set_style_pad_ver(_dv_parent_obj, p, LV_PART_MAIN);
#define PARENT_STYLE_PAD_HOR(p)         lv_obj_set_style_pad_hor(_dv_parent_obj, p, LV_PART_MAIN);
// Add more PARENT_STYLE_xxx as needed, mirroring the Widget Style Macros but using _dv_parent_obj and LV_PART_MAIN

// --- Widget Definition Macros (No "WIDGET_" Prefix) ---
// These create a widget, add it to the map, set _dv_current_widget, reset _dv_current_selector,
// and process the remaining arguments (__VA_ARGS__) which include direct properties and styles.
#define _SETUP_WIDGET(WidgetType, CreateFunc, VarName, ...) \
    lv_obj_t* VarName = CreateFunc(_dv_parent_obj); /* Declare variable */ \
    if (*_dv_view_map_count_ptr < LV_DV_MAX_WIDGETS) { /* Add to map */ \
        _dv_view_map_ptr[*_dv_view_map_count_ptr].obj = VarName; \
        _dv_view_map_ptr[*_dv_view_map_count_ptr].name = #VarName; \
        (*_dv_view_map_count_ptr)++; \
    } else { /* Handle error */ \
        LV_LOG_ERROR("Exceeded LV_DV_MAX_WIDGETS when adding %s", #VarName); \
    } \
    _dv_current_widget = VarName; \
    _dv_current_selector = LV_PART_MAIN | LV_STATE_DEFAULT; /* Reset selector for this widget */ \
    _LV_DV_PROCESS_ARGS(__VA_ARGS__) /* Process properties/styles */

#define OBJ(VarName, ...)     _SETUP_WIDGET(lv_obj_t, lv_obj_create, VarName, __VA_ARGS__)
#define DIV(VarName, ...)     _SETUP_WIDGET(lv_obj_t, lv_obj_create, VarName, __VA_ARGS__)
#define LABEL(VarName, Text, ...)   _SETUP_WIDGET(lv_obj_t, lv_label_create, VarName, LABEL_SET_TEXT(Text), __VA_ARGS__)
#define BUTTON(VarName, ...)  _SETUP_WIDGET(lv_obj_t, lv_btn_create, VarName, __VA_ARGS__)
#define TEXTAREA(VarName, ...) _SETUP_WIDGET(lv_obj_t, lv_textarea_create, VarName, __VA_ARGS__)
// ... Add other widget creation macros (SWITCH, SLIDER, IMAGE, etc.) following the same pattern.
// Example:
// #define SWITCH(VarName, ...) _SETUP_WIDGET(lv_obj_t, lv_switch_create, VarName, __VA_ARGS__)
// #define IMAGE(VarName, Src, ...) _SETUP_WIDGET(lv_obj_t, lv_image_create, VarName, IMAGE_SET_SRC(Src), __VA_ARGS__)


// --- Widget Direct Property/Function Macros ---
// These call specific lv_obj_... or lv_<widget>_... functions using _dv_current_widget.
// They are typically used for non-style properties or actions.
#define OBJ_SIZE(w, h)              lv_obj_set_size(_dv_current_widget, w, h);
#define OBJ_WIDTH(val)              lv_obj_set_width(_dv_current_widget, val);
#define OBJ_HEIGHT(val)             lv_obj_set_height(_dv_current_widget, val);
#define OBJ_ALIGN(align, x, y)      lv_obj_align(_dv_current_widget, align, x, y);
#define OBJ_CENTER()                lv_obj_center(_dv_current_widget);
#define OBJ_ADD_FLAG(f)             lv_obj_add_flag(_dv_current_widget, f);
#define OBJ_CLEAR_FLAG(f)           lv_obj_clear_flag(_dv_current_widget, f);
#define OBJ_ADD_STATE(s)            lv_obj_add_state(_dv_current_widget, s);
#define OBJ_CLEAR_STATE(s)          lv_obj_clear_state(_dv_current_widget, s);
#define OBJ_SCROLL_TO_VIEW()        lv_obj_scroll_to_view(_dv_current_widget, LV_ANIM_ON); // Example with animation
#define OBJ_ADD_EVENT_CB(cb, filter, data) lv_obj_add_event_cb(_dv_current_widget, cb, filter, data);

// Widget-specific direct properties
#define LABEL_SET_TEXT(t)           lv_label_set_text(_dv_current_widget, t);
#define TEXTAREA_SET_TEXT(t)        lv_textarea_set_text(_dv_current_widget, t);
#define TEXTAREA_SET_PLACEHOLDER(t) lv_textarea_set_placeholder_text(_dv_current_widget, t);
#define TEXTAREA_SET_ONE_LINE(b)    lv_textarea_set_one_line(_dv_current_widget, b);
// Button often uses a child label, so direct text setting isn't common, but could be:
// #define BUTTON_SET_LABEL(t)       do { lv_obj_t* lbl = lv_obj_get_child(_dv_current_widget, 0); if(lbl) lv_label_set_text(lbl, t); } while(0)
// #define IMAGE_SET_SRC(s)            lv_image_set_src(_dv_current_widget, s);

// --- Widget Style Macros ---
// These call lv_obj_set_style_... functions using _dv_current_widget and the current _dv_current_selector.
// They are processed directly within widget definitions or within SELECTOR blocks.

// Size & Position Styles
#define WIDTH(value)                    lv_obj_set_style_width(_dv_current_widget, value, _dv_current_selector);
#define MIN_WIDTH(value)                lv_obj_set_style_min_width(_dv_current_widget, value, _dv_current_selector);
#define MAX_WIDTH(value)                lv_obj_set_style_max_width(_dv_current_widget, value, _dv_current_selector);
#define HEIGHT(value)                   lv_obj_set_style_height(_dv_current_widget, value, _dv_current_selector);
#define MIN_HEIGHT(value)               lv_obj_set_style_min_height(_dv_current_widget, value, _dv_current_selector);
#define MAX_HEIGHT(value)               lv_obj_set_style_max_height(_dv_current_widget, value, _dv_current_selector);
#define LENGTH(value)                   lv_obj_set_style_length(_dv_current_widget, value, _dv_current_selector); /* (Arc/Line/?) Check usage */
#define X(value)                        lv_obj_set_style_x(_dv_current_widget, value, _dv_current_selector);
#define Y(value)                        lv_obj_set_style_y(_dv_current_widget, value, _dv_current_selector);
#define ALIGN(value)                    lv_obj_set_style_align(_dv_current_widget, value, _dv_current_selector);

// Transform Styles
#define TRANSFORM_WIDTH(value)          lv_obj_set_style_transform_width(_dv_current_widget, value, _dv_current_selector);
#define TRANSFORM_HEIGHT(value)         lv_obj_set_style_transform_height(_dv_current_widget, value, _dv_current_selector);
#define TRANSLATE_X(value)              lv_obj_set_style_translate_x(_dv_current_widget, value, _dv_current_selector);
#define TRANSLATE_Y(value)              lv_obj_set_style_translate_y(_dv_current_widget, value, _dv_current_selector);
#define TRANSLATE_RADIAL(value)         lv_obj_set_style_translate_radial(_dv_current_widget, value, _dv_current_selector);
#define TRANSFORM_SCALE_X(value)        lv_obj_set_style_transform_scale_x(_dv_current_widget, value, _dv_current_selector);
#define TRANSFORM_SCALE_Y(value)        lv_obj_set_style_transform_scale_y(_dv_current_widget, value, _dv_current_selector);
#define TRANSFORM_ROTATION(value)       lv_obj_set_style_transform_rotation(_dv_current_widget, value, _dv_current_selector);
#define TRANSFORM_PIVOT_X(value)        lv_obj_set_style_transform_pivot_x(_dv_current_widget, value, _dv_current_selector);
#define TRANSFORM_PIVOT_Y(value)        lv_obj_set_style_transform_pivot_y(_dv_current_widget, value, _dv_current_selector);
#define TRANSFORM_SKEW_X(value)         lv_obj_set_style_transform_skew_x(_dv_current_widget, value, _dv_current_selector);
#define TRANSFORM_SKEW_Y(value)         lv_obj_set_style_transform_skew_y(_dv_current_widget, value, _dv_current_selector);

// Padding Styles
#define PAD_TOP(value)                  lv_obj_set_style_pad_top(_dv_current_widget, value, _dv_current_selector);
#define PAD_BOTTOM(value)               lv_obj_set_style_pad_bottom(_dv_current_widget, value, _dv_current_selector);
#define PAD_LEFT(value)                 lv_obj_set_style_pad_left(_dv_current_widget, value, _dv_current_selector);
#define PAD_RIGHT(value)                lv_obj_set_style_pad_right(_dv_current_widget, value, _dv_current_selector);
#define PAD_ROW(value)                  lv_obj_set_style_pad_row(_dv_current_widget, value, _dv_current_selector);
#define PAD_COLUMN(value)               lv_obj_set_style_pad_column(_dv_current_widget, value, _dv_current_selector);
#define PAD_RADIAL(value)               lv_obj_set_style_pad_radial(_dv_current_widget, value, _dv_current_selector);
// Convenience Padding (Order matters if used with individual sides)
#define PAD_ALL(value)                  lv_obj_set_style_pad_all(_dv_current_widget, value, _dv_current_selector);
#define PAD_HOR(value)                  lv_obj_set_style_pad_hor(_dv_current_widget, value, _dv_current_selector);
#define PAD_VER(value)                  lv_obj_set_style_pad_ver(_dv_current_widget, value, _dv_current_selector);

// Margin Styles
#define MARGIN_TOP(value)               lv_obj_set_style_margin_top(_dv_current_widget, value, _dv_current_selector);
#define MARGIN_BOTTOM(value)            lv_obj_set_style_margin_bottom(_dv_current_widget, value, _dv_current_selector);
#define MARGIN_LEFT(value)              lv_obj_set_style_margin_left(_dv_current_widget, value, _dv_current_selector);
#define MARGIN_RIGHT(value)             lv_obj_set_style_margin_right(_dv_current_widget, value, _dv_current_selector);

// Background Styles
#define BG_COLOR(value)                 lv_obj_set_style_bg_color(_dv_current_widget, value, _dv_current_selector);
#define BG_OPA(value)                   lv_obj_set_style_bg_opa(_dv_current_widget, value, _dv_current_selector);
#define BG_GRAD_COLOR(value)            lv_obj_set_style_bg_grad_color(_dv_current_widget, value, _dv_current_selector);
#define BG_GRAD_DIR(value)              lv_obj_set_style_bg_grad_dir(_dv_current_widget, value, _dv_current_selector);
#define BG_MAIN_STOP(value)             lv_obj_set_style_bg_main_stop(_dv_current_widget, value, _dv_current_selector);
#define BG_GRAD_STOP(value)             lv_obj_set_style_bg_grad_stop(_dv_current_widget, value, _dv_current_selector);
#define BG_MAIN_OPA(value)              lv_obj_set_style_bg_main_opa(_dv_current_widget, value, _dv_current_selector); /* LVGL 9 specific */
#define BG_GRAD_OPA(value)              lv_obj_set_style_bg_grad_opa(_dv_current_widget, value, _dv_current_selector); /* LVGL 9 specific */
#define BG_GRAD(value)                  lv_obj_set_style_bg_grad(_dv_current_widget, value, _dv_current_selector);
#define BG_IMAGE_SRC(value)             lv_obj_set_style_bg_image_src(_dv_current_widget, value, _dv_current_selector);
#define BG_IMAGE_OPA(value)             lv_obj_set_style_bg_image_opa(_dv_current_widget, value, _dv_current_selector);
#define BG_IMAGE_RECOLOR(value)         lv_obj_set_style_bg_image_recolor(_dv_current_widget, value, _dv_current_selector);
#define BG_IMAGE_RECOLOR_OPA(value)     lv_obj_set_style_bg_image_recolor_opa(_dv_current_widget, value, _dv_current_selector);
#define BG_IMAGE_TILED(value)           lv_obj_set_style_bg_image_tiled(_dv_current_widget, value, _dv_current_selector);

// Border Styles
#define BORDER_COLOR(value)             lv_obj_set_style_border_color(_dv_current_widget, value, _dv_current_selector);
#define BORDER_OPA(value)               lv_obj_set_style_border_opa(_dv_current_widget, value, _dv_current_selector);
#define BORDER_WIDTH(value)             lv_obj_set_style_border_width(_dv_current_widget, value, _dv_current_selector);
#define BORDER_SIDE(value)              lv_obj_set_style_border_side(_dv_current_widget, value, _dv_current_selector);
#define BORDER_POST(value)              lv_obj_set_style_border_post(_dv_current_widget, value, _dv_current_selector);

// Outline Styles
#define OUTLINE_WIDTH(value)            lv_obj_set_style_outline_width(_dv_current_widget, value, _dv_current_selector);
#define OUTLINE_COLOR(value)            lv_obj_set_style_outline_color(_dv_current_widget, value, _dv_current_selector);
#define OUTLINE_OPA(value)              lv_obj_set_style_outline_opa(_dv_current_widget, value, _dv_current_selector);
#define OUTLINE_PAD(value)              lv_obj_set_style_outline_pad(_dv_current_widget, value, _dv_current_selector);

// Shadow Styles
#define SHADOW_WIDTH(value)             lv_obj_set_style_shadow_width(_dv_current_widget, value, _dv_current_selector);
#define SHADOW_OFFSET_X(value)          lv_obj_set_style_shadow_offset_x(_dv_current_widget, value, _dv_current_selector);
#define SHADOW_OFFSET_Y(value)          lv_obj_set_style_shadow_offset_y(_dv_current_widget, value, _dv_current_selector);
#define SHADOW_SPREAD(value)            lv_obj_set_style_shadow_spread(_dv_current_widget, value, _dv_current_selector);
#define SHADOW_COLOR(value)             lv_obj_set_style_shadow_color(_dv_current_widget, value, _dv_current_selector);
#define SHADOW_OPA(value)               lv_obj_set_style_shadow_opa(_dv_current_widget, value, _dv_current_selector);

// Image Styles (for Image Widgets)
#define IMAGE_OPA(value)                lv_obj_set_style_image_opa(_dv_current_widget, value, _dv_current_selector);
#define IMAGE_RECOLOR(value)            lv_obj_set_style_image_recolor(_dv_current_widget, value, _dv_current_selector);
#define IMAGE_RECOLOR_OPA(value)        lv_obj_set_style_image_recolor_opa(_dv_current_widget, value, _dv_current_selector);

// Line Styles (for Line Widgets)
#define LINE_WIDTH(value)               lv_obj_set_style_line_width(_dv_current_widget, value, _dv_current_selector);
#define LINE_DASH_WIDTH(value)          lv_obj_set_style_line_dash_width(_dv_current_widget, value, _dv_current_selector);
#define LINE_DASH_GAP(value)            lv_obj_set_style_line_dash_gap(_dv_current_widget, value, _dv_current_selector);
#define LINE_ROUNDED(value)             lv_obj_set_style_line_rounded(_dv_current_widget, value, _dv_current_selector);
#define LINE_COLOR(value)               lv_obj_set_style_line_color(_dv_current_widget, value, _dv_current_selector);
#define LINE_OPA(value)                 lv_obj_set_style_line_opa(_dv_current_widget, value, _dv_current_selector);

// Arc Styles (for Arc Widgets)
#define ARC_WIDTH(value)                lv_obj_set_style_arc_width(_dv_current_widget, value, _dv_current_selector);
#define ARC_ROUNDED(value)              lv_obj_set_style_arc_rounded(_dv_current_widget, value, _dv_current_selector);
#define ARC_COLOR(value)                lv_obj_set_style_arc_color(_dv_current_widget, value, _dv_current_selector);
#define ARC_OPA(value)                  lv_obj_set_style_arc_opa(_dv_current_widget, value, _dv_current_selector);
#define ARC_IMAGE_SRC(value)            lv_obj_set_style_arc_image_src(_dv_current_widget, value, _dv_current_selector);

// Text Styles
#define TEXT_COLOR(value)               lv_obj_set_style_text_color(_dv_current_widget, value, _dv_current_selector);
#define TEXT_OPA(value)                 lv_obj_set_style_text_opa(_dv_current_widget, value, _dv_current_selector);
#define TEXT_FONT(value)                lv_obj_set_style_text_font(_dv_current_widget, value, _dv_current_selector);
#define TEXT_LETTER_SPACE(value)        lv_obj_set_style_text_letter_space(_dv_current_widget, value, _dv_current_selector);
#define TEXT_LINE_SPACE(value)          lv_obj_set_style_text_line_space(_dv_current_widget, value, _dv_current_selector);
#define TEXT_DECOR(value)               lv_obj_set_style_text_decor(_dv_current_widget, value, _dv_current_selector);
#define TEXT_ALIGN(value)               lv_obj_set_style_text_align(_dv_current_widget, value, _dv_current_selector);
// Text Outline/Stroke (if enabled in LVGL config, check lv_conf.h)
#if LV_USE_FONT_SUBPX // Often depends on subpixel rendering, check LVGL config
#define TEXT_OUTLINE_STROKE_COLOR(value) lv_obj_set_style_text_outline_stroke_color(_dv_current_widget, value, _dv_current_selector);
#define TEXT_OUTLINE_STROKE_WIDTH(value) lv_obj_set_style_text_outline_stroke_width(_dv_current_widget, value, _dv_current_selector);
#define TEXT_OUTLINE_STROKE_OPA(value)  lv_obj_set_style_text_outline_stroke_opa(_dv_current_widget, value, _dv_current_selector);
#endif // LV_USE_FONT_SUBPX

// Miscellaneous Styles
#define RADIUS(value)                   lv_obj_set_style_radius(_dv_current_widget, value, _dv_current_selector);
#define RADIAL_OFFSET(value)            lv_obj_set_style_radial_offset(_dv_current_widget, value, _dv_current_selector); /* Arc specific? Check usage */
#define CLIP_CORNER(value)              lv_obj_set_style_clip_corner(_dv_current_widget, value, _dv_current_selector);
#define OPA(value)                      lv_obj_set_style_opa(_dv_current_widget, value, _dv_current_selector);
#define OPA_LAYERED(value)              lv_obj_set_style_opa_layered(_dv_current_widget, value, _dv_current_selector);
#define COLOR_FILTER_DSC(value)         lv_obj_set_style_color_filter_dsc(_dv_current_widget, value, _dv_current_selector);
#define COLOR_FILTER_OPA(value)         lv_obj_set_style_color_filter_opa(_dv_current_widget, value, _dv_current_selector);
// #define RECOLOR(value)                  lv_obj_set_style_recolor(_dv_current_widget, value, _dv_current_selector); /* Deprecated? Check LVGL 9 docs */
// #define RECOLOR_OPA(value)              lv_obj_set_style_recolor_opa(_dv_current_widget, value, _dv_current_selector); /* Deprecated? Check LVGL 9 docs */
#define ANIM(value)                     lv_obj_set_style_anim(_dv_current_widget, value, _dv_current_selector);
#define ANIM_DURATION(value)            lv_obj_set_style_anim_duration(_dv_current_widget, value, _dv_current_selector); /* Replaced anim_time in v9 */
#define TRANSITION(value)               lv_obj_set_style_transition(_dv_current_widget, value, _dv_current_selector);
#define BLEND_MODE(value)               lv_obj_set_style_blend_mode(_dv_current_widget, value, _dv_current_selector);
#define LAYOUT(value)                   lv_obj_set_style_layout(_dv_current_widget, value, _dv_current_selector);
#define BASE_DIR(value)                 lv_obj_set_style_base_dir(_dv_current_widget, value, _dv_current_selector);
#define BITMAP_MASK_SRC(value)          lv_obj_set_style_bitmap_mask_src(_dv_current_widget, value, _dv_current_selector); /* New in v9? Check usage */
#define ROTARY_SENSITIVITY(value)       lv_obj_set_style_rotary_sensitivity(_dv_current_widget, value, _dv_current_selector); /* New in v9? Check usage */

// Flexbox Styles (Apply to container with LAYOUT(lv_layout_flex_create()))
#define FLEX_FLOW(value)                lv_obj_set_style_flex_flow(_dv_current_widget, value, _dv_current_selector);
#define FLEX_MAIN_PLACE(value)          lv_obj_set_style_flex_main_place(_dv_current_widget, value, _dv_current_selector);
#define FLEX_CROSS_PLACE(value)         lv_obj_set_style_flex_cross_place(_dv_current_widget, value, _dv_current_selector);
#define FLEX_TRACK_PLACE(value)         lv_obj_set_style_flex_track_place(_dv_current_widget, value, _dv_current_selector);
#define FLEX_GROW(value)                lv_obj_set_style_flex_grow(_dv_current_widget, value, _dv_current_selector); // Apply to flex items

// Grid Styles (Apply relevant ones to container with LAYOUT(lv_layout_grid_create()), others to items)
#define GRID_COLUMN_DSC_ARRAY(value)    lv_obj_set_style_grid_column_dsc_array(_dv_current_widget, value, _dv_current_selector); // Container
#define GRID_COLUMN_ALIGN(value)        lv_obj_set_style_grid_column_align(_dv_current_widget, value, _dv_current_selector); // Container
#define GRID_ROW_DSC_ARRAY(value)       lv_obj_set_style_grid_row_dsc_array(_dv_current_widget, value, _dv_current_selector); // Container
#define GRID_ROW_ALIGN(value)           lv_obj_set_style_grid_row_align(_dv_current_widget, value, _dv_current_selector); // Container
#define GRID_CELL_COLUMN_POS(value)     lv_obj_set_style_grid_cell_column_pos(_dv_current_widget, value, _dv_current_selector); // Item
#define GRID_CELL_X_ALIGN(value)        lv_obj_set_style_grid_cell_x_align(_dv_current_widget, value, _dv_current_selector); // Item
#define GRID_CELL_COLUMN_SPAN(value)    lv_obj_set_style_grid_cell_column_span(_dv_current_widget, value, _dv_current_selector); // Item
#define GRID_CELL_ROW_POS(value)        lv_obj_set_style_grid_cell_row_pos(_dv_current_widget, value, _dv_current_selector); // Item
#define GRID_CELL_Y_ALIGN(value)        lv_obj_set_style_grid_cell_y_align(_dv_current_widget, value, _dv_current_selector); // Item
#define GRID_CELL_ROW_SPAN(value)       lv_obj_set_style_grid_cell_row_span(_dv_current_widget, value, _dv_current_selector); // Item
// Grid Gap styles (Usually on container)
#define GRID_COLUMN_GAP(value)          lv_obj_set_style_grid_column_gap(_dv_current_widget, value, _dv_current_selector); // Container
#define GRID_ROW_GAP(value)             lv_obj_set_style_grid_row_gap(_dv_current_widget, value, _dv_current_selector); // Container

// --- Selector Block Macro ---
/**
 * @brief Creates a scope where subsequent style macros use the specified selector.
 *
 * @param selector_value The lv_style_selector_t value (e.g., LV_STATE_PRESSED, LV_PART_INDICATOR | LV_STATE_CHECKED).
 * @param ... Comma-separated list of style macros (e.g., BG_COLOR(...), WIDTH(...)) to apply using this selector.
 */
#define SELECTOR(selector_value, ...) \
    do { \
        /* Save selector from the outer scope (widget default or previous SELECTOR) */ \
        lv_style_selector_t _dv_outer_selector = _dv_current_selector; \
        /* Set the new selector specifically for this block */ \
        _dv_current_selector = (selector_value); \
        /* Process the arguments passed to SELECTOR (...) using the new selector */ \
        _LV_DV_PROCESS_ARGS(__VA_ARGS__) \
        /* Restore the outer selector once this block's arguments are processed */ \
        _dv_current_selector = _dv_outer_selector; \
    } while(0)

// --- Main Definition Macro ---
/**
 * @brief Defines LVGL objects, styles (potentially scoped by SELECTOR), and populates a view map.
 *
 * Usage:
 *   LV_VIEW(map_array, map_count, container,
 *       PARENT_STYLE_PAD_ALL(5), // Apply to container
 *       LAYOUT(lv_layout_flex_create()), // Set container layout
 *       FLEX_FLOW(LV_FLEX_FLOW_ROW),    // Apply flex style to container
 *
 *       LABEL(my_label, "Text",          // Define a label
 *           WIDTH(100),                 // Style for default state
 *           TEXT_COLOR(lv_color_black()), // Style for default state
 *           SELECTOR(LV_STATE_DISABLED, // Start disabled state block
 *              TEXT_COLOR(lv_color_grey())
 *           ) // End selector block
 *       ), // End LABEL block
 *
 *       BUTTON(my_button,               // Define a button
 *           OBJ_ADD_FLAG(LV_OBJ_FLAG_CHECKABLE), // Add a flag
 *           WIDTH(LV_SIZE_CONTENT),     // Default state width
 *           HEIGHT(30),                 // Default state height
 *           RADIUS(4),                  // Default state radius
 *           SELECTOR(LV_PART_MAIN | LV_STATE_PRESSED, // Start pressed state block
 *              BG_COLOR(lv_palette_darken(LV_PALETTE_BLUE, 2)),
 *              BORDER_WIDTH(3)
 *           ), // End selector block
 *           SELECTOR(LV_PART_MAIN | LV_STATE_CHECKED, // Start checked state block
 *              BG_COLOR(lv_palette_main(LV_PALETTE_GREEN))
 *           ), // End selector block
 *           // Add a label *inside* the button (Button macro doesn't create one automatically)
 *           LABEL(btn_label, "Click Me", // This label's parent is my_button
 *               OBJ_CENTER(),          // Center label in button
 *               TEXT_COLOR(lv_color_white()), // Default label color
 *               SELECTOR(LV_STATE_PRESSED, // Style for label when button is pressed
 *                  TEXT_COLOR(lv_color_lighten(lv_color_white(), 50))
 *               )
 *           ) // End button label
 *       ) // End BUTTON block
 *   );
 *
 * @param MapArrayVariableName The name of the lv_vfl_view_map_t array.
 * @param MapCountVariableName The name of the size_t variable holding the count.
 * @param ParentObj The lv_obj_t* container object where widgets defined at the top level will be created.
 * @param ... Comma-separated list of PARENT_xxx(), Layout Styles (LAYOUT, FLEX_FLOW, etc.),
 *            Widget Creation (LABEL, BUTTON, etc.), which can contain OBJ_xxx(), Style (WIDTH, BG_COLOR, etc.),
 *            SELECTOR(...) blocks, and even nested widget definitions.
 */
#define LV_VIEW(MapArrayVariableName, MapCountVariableName, ParentObj, ...) \
    do { \
        lv_obj_t* _dv_parent_obj = (ParentObj); \
        lv_vfl_view_map_t* _dv_view_map_ptr = MapArrayVariableName; \
        size_t* _dv_view_map_count_ptr = &(MapCountVariableName); \
        lv_obj_t* _dv_current_widget = NULL; /* Track the widget being configured */ \
        lv_style_selector_t _dv_current_selector = LV_PART_MAIN | LV_STATE_DEFAULT; /* Track the selector being used */ \
        /* Need a way to handle nested widgets; parent needs updating within widget blocks */ \
        /* Let's store the parent context for nested calls. */ \
        /* This simple model might need refinement for deep nesting if parent styles needed. */ \
        /* The current _SETUP_WIDGET always uses the top-level _dv_parent_obj. */ \
        /* A stack or temporary variable could manage parent context if needed. */ \
        /* For now, assume top-level PARENT_xxx macros apply only to the main ParentObj */ \
        /* and widget properties/styles apply to _dv_current_widget. */ \
        /* The _SETUP_WIDGET sets the parent correctly for immediate children. */ \
        \
        /* Set the initial widget context to the parent for PARENT_xxx/Layout macros */ \
        _dv_current_widget = _dv_parent_obj; \
        _dv_current_selector = LV_PART_MAIN | LV_STATE_DEFAULT; \
        \
        /* Process all top-level arguments */ \
        /* PARENT_xxx macros use _dv_parent_obj directly */ \
        /* Layout macros (LAYOUT, FLEX_FLOW etc.) use _dv_current_widget which is the parent here */ \
        /* Widget macros (_SETUP_WIDGET based) will set _dv_current_widget internally */ \
        _LV_DV_PROCESS_ARGS(__VA_ARGS__) \
    } while(0)


#endif // LV_VIEW_DEFINE_MACROS_H