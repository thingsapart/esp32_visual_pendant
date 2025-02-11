#include "ui/handwheel_slider.hpp"

#include "ui/event_handler.hpp"
#include "machine/encoder.hpp"

HandwheelSlider::HandwheelSlider(lv_obj_t *parent,  int step_size, int ticks, bool show_label) {
    slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 15, LV_PCT(100));
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_LAYOUT_1);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_ADV_HITTEST);

    if (label) {
        this->label = lv_label_create(this->parent);
        lv_obj_remove_style_all(label);
        lv_obj_add_flag(label, LV_OBJ_FLAG_IGNORE_LAYOUT);

        lv_obj_add_event_cb(slider, [](lv_event_t *e) {
            lv_obj_t *lbl = evt_data_obj(e);
            lv_obj_t *slider = evt_target_obj(e);
            lv_label_set_text_fmt(lbl, "%d", lv_slider_get_value(slider));
        }, LV_EVENT_VALUE_CHANGED, label);

        lv_style_init(&style_knob);
        lv_style_set_bg_opa(&style_knob, LV_OPA_COVER);
        lv_style_set_bg_color(&style_knob, lv_palette_main(LV_PALETTE_YELLOW));
        lv_style_set_border_color(&style_knob, lv_palette_darken(LV_PALETTE_YELLOW, 3));
        lv_style_set_border_width(&style_knob, 2);
        lv_style_set_radius(&style_knob, LV_RADIUS_CIRCLE);
        lv_style_set_pad_all(&style_knob, 6); // Makes the knob larger
        lv_obj_add_style(slider, &style_knob, LV_PART_KNOB | LV_STATE_FOCUSED);

        lv_obj_set_style_text_color(label, lv_color_make(150, 150, 150), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_width(this->label, LV_SIZE_CONTENT);
    }

    lv_obj_add_event_cb(slider, [](lv_event_t *e) {
        // HandwheelSlider *self = (HandwheelSlider *)lv_event_get_user_data(e);
        encoder.setUiMode();
    }, LV_EVENT_FOCUSED, this);

    lv_obj_add_event_cb(slider, [](lv_event_t *e) {
        // HandwheelSlider *self = (HandwheelSlider *)lv_event_get_user_data(e);
        encoder.setEncoderMode();
    }, LV_EVENT_DEFOCUSED, this);
}

HandwheelSlider::~HandwheelSlider() {
    lv_obj_del_async(slider);
}
