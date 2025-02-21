#ifndef __UI_ENCODER_SLIDER_H__
#define __UI_ENCODER_SLIDER_H__

#include "lvgl.h"

extern void encoder_set_ui_mode();
extern void encoder_set_encoder_mode();

lv_obj_t *encoder_slider_create(lv_obj_t *parent);

#endif // __UI_ENCODER_SLIDER_H__
