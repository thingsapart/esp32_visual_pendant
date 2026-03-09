#ifdef JC8012P4A1

#include <Arduino.h>

#include "driver/esp_lvgl/esp_lvgl_adapter.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "jc8012p4a1/lcd/jd9365_lcd.h"
#include "jc8012p4a1/touch/gsl3680_touch.h"
#include "lvgl.h"

#define UI_DEBUG_LOCAL_LEVEL D_WARN
#include "debug.h"

#define TP_I2C_SDA 7
#define TP_I2C_SCL 8
#define TP_RST 22
#define TP_INT 21

#define LCD_RST 27
#define LCD_LED 23

#ifndef TFT_WIDTH
#define TFT_WIDTH 800
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 1280
#endif

// Hardware driver instances (new JD9365 + GSL3680 implementations)
static jd9365_lcd lcd(LCD_RST);
static gsl3680_touch touch(TP_I2C_SDA, TP_I2C_SCL, TP_RST, TP_INT);

// ESP-IDF driver handles (defined in the new driver files)
extern esp_lcd_panel_handle_t panel_handle;  // from jd9365_lcd.cpp
extern esp_lcd_touch_handle_t tp;            // from gsl3680_touch.cpp

static const char *TAG = "JC8012P4A1";

void display_alloc() {
  LOGI(TAG, "display_alloc: no-op for JC8012P4A1 (adapter handles buffers)");
}
void display_setup(lv_display_t *disp, lv_indev_t *indev) {
  LOGI(TAG, "Initializing JC8012P4A1 hardware...");
  lcd.begin();
  touch.begin();
  LOGI(TAG, "Hardware initialization complete.");

  LOGI(TAG, "Configuring LVGL display driver...");
    const lvgl_port_display_cfg_t disp_cfg = {
      .panel_handle = panel_handle,
      .buffer_size = TFT_WIDTH * TFT_HEIGHT * (LV_COLOR_DEPTH / 8),
      .double_buffer = true,
      .hres = TFT_WIDTH,
      .vres = TFT_HEIGHT,
      .monochrome = false,
      .rotation = {.swap_xy = false, .mirror_x = false, .mirror_y = false},
      .flags = {.buff_dma = true, .buff_spiram = true, .full_refresh = false, .direct_mode = true}};

  const lvgl_port_display_dsi_cfg_t dsi_cfg = {.flags = {.avoid_tearing = true}};

  if (esp_lvgl_adapter_init_display(disp, &disp_cfg, &dsi_cfg) != ESP_OK) {
    LOGE(TAG, "Failed to initialize LVGL display adapter!");
    return;
  }
  LOGI(TAG, "LVGL display configured in DIRECT MODE with tearing avoidance.");

  LOGI(TAG, "Configuring LVGL touch input driver...");
  const lvgl_port_touch_cfg_t touch_cfg = {.disp = disp, .handle = tp};

  if (esp_lvgl_adapter_init_touch(indev, &touch_cfg) != ESP_OK) {
    LOGE(TAG, "Failed to initialize LVGL touch adapter!");
    return;
  }
  LOGI(TAG, "LVGL touch input configured.");
}

#endif
