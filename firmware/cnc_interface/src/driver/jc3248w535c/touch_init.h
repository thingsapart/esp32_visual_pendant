// Touch hardware initialisation for the JC3248W535C (AXS15231B, I2C touch).
//
// Provides a uniform interface over three backend implementations:
//   • ESP32_Display_Panel library     (ESP32_LVGL_ESP_DISP defined)
//   • ESP32_Display_Panel touch-only  (JC3248W535C_ESP_DISP_TOUCH defined, no ESP32_LVGL_ESP_DISP)
//   • Raw I2C                         (default)
//
// The implementations live in touch_init.cpp.  The caller (jc3248w535c.cpp)
// selects behaviour purely through build flags; this header is the same for all.

#pragma once

#ifdef JC3248W535C

#include "lvgl.h"

// Initialise touch hardware.
// Must be called after I2C is configured in the IDF-native path (display_setup
// will have already called i2c_driver_install before this if using the raw path).
// Returns true on success, false if the device could not be reached.
bool touch_hw_init();

// LVGL pointer-indev read callback.
// Register with:  lv_indev_set_read_cb(indev, touch_indev_read);
void touch_indev_read(lv_indev_t *indev, lv_indev_data_t *data);

#endif // JC3248W535C
