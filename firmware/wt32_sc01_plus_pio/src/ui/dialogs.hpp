// dialogs.h
#ifndef DIALOGS_H
#define DIALOGS_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <functional>

lv_obj_t* button_dialog(const char* title, const char* text, bool add_close_btn,
                       const std::vector<const char*>& btns,
                       const std::vector<std::function<void(lv_obj_t*)>>& btn_cbs);

#endif