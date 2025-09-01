#ifndef ESP_LVGL_ADAPTER_H
#define ESP_LVGL_ADAPTER_H

#ifdef ESP32_LVGL_ESP_DISP

#include <esp_display_panel.hpp>
#include "driver/esp_lvgl/include/esp_lvgl_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configures an existing LVGL display object using the esp_lvgl_port
 * logic.
 *
 * This function adapts the display setup logic from esp_lvgl_port to work with
 * an already-created lv_display_t object, making it compatible with existing
 * application structures that manage their own LVGL tasks.
 *
 * @param disp The lv_display_t object to configure.
 * @param disp_cfg Display configuration structure.
 * @param dsi_cfg MIPI-DSI specific display configuration.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t esp_lvgl_adapter_init_display(
    lv_display_t *disp, const lvgl_port_display_cfg_t *disp_cfg,
    const lvgl_port_display_dsi_cfg_t *dsi_cfg);

/**
 * @brief Configures an existing LVGL input device object for touch input.
 *
 * This function adapts the touch setup logic from esp_lvgl_port to work with
 * an already-created lv_indev_t object.
 *
 * @param indev The lv_indev_t object to configure.
 * @param touch_cfg Touch configuration structure.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t esp_lvgl_adapter_init_touch(lv_indev_t *indev,
                                      const lvgl_port_touch_cfg_t *touch_cfg);

#ifdef __cplusplus
}
#endif

#endif  // ESP_LVGL_ADAPTER_H

#endif