/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP LVGL port touch
 */

#pragma once

#include "driver/jc1060p470/touch/esp_lcd_touch.h"  // Direct include for our project structure
#include "esp_err.h"
#include "lvgl.h"

#if LVGL_VERSION_MAJOR == 8
#include "esp_lvgl_port_compatibility.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration touch structure
 */
typedef struct {
  lv_display_t
      *disp; /*!< LVGL display handle (returned from lvgl_port_add_disp) */
  esp_lcd_touch_handle_t handle; /*!< LCD touch IO handle */
  struct {
    float x;
    float y;
  } scale; /*!< Touch scale */
} lvgl_port_touch_cfg_t;

#ifdef __cplusplus
}
#endif
