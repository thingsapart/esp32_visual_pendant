// dialogs.h
#ifndef DIALOGS_H
#define DIALOGS_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <functional>
#include <string>

#include "ui/event_handler.hpp"

lv_obj_t* button_dialog(const char* title, const char* text, bool add_close_btn,
                       const std::vector<std::string>& btns,
                       const std::vector<evt_handler_t>& btn_cbs);

#endif