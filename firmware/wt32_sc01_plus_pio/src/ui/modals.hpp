#ifndef MODAL_H
#define MODAL_H

#include <Arduino.h>
#include <lvgl.h>
#include <functional>  // For std::function

// #include "machine/machine_interface.hpp"
#include "ui/interface.hpp"

// Global variable declarations (extern)
extern bool active_modal;

// Function declarations
bool modal_active();
void close_modal(lv_obj_t* m);
lv_obj_t* home_modal(Interface* interface);
lv_obj_t* home_modal_(Interface* interface);

#endif