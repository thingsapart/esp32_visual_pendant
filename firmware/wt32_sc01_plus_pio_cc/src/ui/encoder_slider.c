#include "encoder_slider.h"

#include "debug.h"

static lv_color_t knob_color = { 0xFF };
extern lv_group_t *default_group;

void slider_focused(lv_event_t *e) {
    _d(0, "FOCUSED\n");
    encoder_set_ui_mode();

    lv_obj_t *slider = lv_event_get_user_data(e);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFF00FF), LV_PART_KNOB);
    // lv_group_set_editing(default_group, true);
}

void slider_defocused(lv_event_t *e) {
    _d(0, "DEFOCUSED\n");
    encoder_set_encoder_mode();

    lv_obj_t *slider = lv_event_get_user_data(e);
    lv_obj_set_style_bg_color(slider, knob_color, LV_PART_KNOB);
    // lv_group_set_editing(default_group, false);
}

void slider_val(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_user_data(e);
    _df(0, "VAL: %d\n", lv_slider_get_value(slider));
}

lv_obj_t *encoder_slider_create(lv_obj_t *parent) {
    lv_obj_t *res = lv_slider_create(parent);

    lv_obj_add_event_cb(res, slider_focused, LV_EVENT_FOCUSED, res);
    lv_obj_add_event_cb(res, slider_defocused, LV_EVENT_DEFOCUSED, res);
    lv_obj_add_event_cb(res, slider_val, LV_EVENT_VALUE_CHANGED, res);
    //lv_group_add_obj(default_group, res);

    knob_color = lv_obj_get_style_bg_color(res, LV_PART_KNOB);

    return res;
}

