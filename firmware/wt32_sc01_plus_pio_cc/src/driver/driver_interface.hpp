#ifndef __DRIVER_DRIVER_INTERFACE_HPP__
#define __DRIVER_DRIVER_INTERFACE_HPP__

// Called to initialize the MCU and built-in peripherals.
void mcu_setup();
// Called after UI is up and running, can be used to report
// machine info, for example.
void mcu_startup();

#ifdef TFT_WIDTH

#include "lvgl.h"

// Called to initialize the display and main input device.
void display_setup(lv_display_t *disp, lv_indev_t *indev);

#endif

#endif // __DRIVER_DRIVER_INTERFACE_HPP__
