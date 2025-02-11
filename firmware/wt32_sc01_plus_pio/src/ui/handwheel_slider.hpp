#ifndef __UI_HANDWHEEL_SLIDER_HPP__
#define __UI_HANDWHEEL_SLIDER_HPP__

#include "lvgl.h"

class HandwheelSlider {
public:
    HandwheelSlider(lv_obj_t *parent, int step_size = 100, int ticks = 50, bool show_label = true);
    ~HandwheelSlider();

    lv_obj_t *slider;
    lv_obj_t *label;
private:
    lv_obj_t *parent;
    lv_style_t style_knob;
};

#endif // __UI_HANDWHEEL_SLIDER_HPP__