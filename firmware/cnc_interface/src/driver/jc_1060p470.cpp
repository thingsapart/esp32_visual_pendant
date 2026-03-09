#ifdef JC1060P470

#include <Arduino.h>

#include "driver/esp_lvgl/esp_lvgl_adapter.h"
#include "esp_lcd_mipi_dsi.h"
#include "jc1060p470/lcd/jd9165_lcd.h"
#include "gt911_touch/gt911_touch.h"
#include "lvgl.h"

#define UI_DEBUG_LOCAL_LEVEL D_WARN
#include "debug.h"

#define LCD_RST 27

#define TP_I2C_SDA 7
#define TP_I2C_SCL 8
#define TP_RST -1
#define TP_INT -1

#ifndef TFT_WIDTH
#define TFT_WIDTH 1024
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 600
#endif

// Hardware driver instances
static jd9165_lcd lcd(LCD_RST);
static gt911_touch touch(TP_I2C_SDA, TP_I2C_SCL, TP_RST, TP_INT);

// ESP-IDF driver handles (exposed from the driver files)
extern esp_lcd_panel_handle_t panel_handle;  // from jd9165_lcd.cpp
extern esp_lcd_touch_handle_t tp;            // from gt911_touch.cpp

static const char *TAG = "JC1060P470";

void display_setup(lv_display_t *disp, lv_indev_t *indev) {
  // 1. Initialize hardware
  LOGI(TAG, "Initializing JC1060P470 hardware...");
  lcd.begin();
  touch.begin();
  LOGI(TAG, "Hardware initialization complete.");

  // 2. Configure LVGL Display using the adapter
  LOGI(TAG, "Configuring LVGL display driver...");
  const lvgl_port_display_cfg_t disp_cfg = {
      .panel_handle = panel_handle,
      .buffer_size = TFT_WIDTH * TFT_HEIGHT *
                     (LV_COLOR_DEPTH / 8),  // Full buffer for direct mode
      .double_buffer = true,
      .hres = TFT_WIDTH,
      .vres = TFT_HEIGHT,
      .monochrome = false,
      .rotation =
          {
              .swap_xy = false,
              .mirror_x = false,
              .mirror_y = false,
          },
      .flags = {
          .buff_dma = true,
          .buff_spiram = true,
          .full_refresh = false,
          .direct_mode = true,
      }};

  const lvgl_port_display_dsi_cfg_t dsi_cfg = {.flags = {
                                                   .avoid_tearing = true,
                                               }};

  if (esp_lvgl_adapter_init_display(disp, &disp_cfg, &dsi_cfg) != ESP_OK) {
    LOGE(TAG, "Failed to initialize LVGL display adapter!");
    return;
  }
  LOGI(TAG, "LVGL display configured in DIRECT MODE with tearing avoidance.");

  // 3. Configure LVGL Touch Input using the adapter
  LOGI(TAG, "Configuring LVGL touch input driver...");
  const lvgl_port_touch_cfg_t touch_cfg = {
      .disp = disp,
      .handle = tp,
  };

  if (esp_lvgl_adapter_init_touch(indev, &touch_cfg) != ESP_OK) {
    LOGE(TAG, "Failed to initialize LVGL touch adapter!");
    return;
  }
  LOGI(TAG, "LVGL touch input configured.");
}

#endif
