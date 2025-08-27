#ifdef JC1060P470

#include <Arduino.h>
#include "lvgl.h"
#include "jc1060p470/lcd/jd9165_lcd.h"
#include "jc1060p470/touch/gt911_touch.h"
#include "esp_lcd_mipi_dsi.h"

#define UI_DEBUG_LOCAL_LEVEL D_VERBOSE
#include "debug.h"

#define LCD_RST 27
#define LCD_LED 23

#define TP_I2C_SDA 7
#define TP_I2C_SCL 8
#define TP_RST -1
#define TP_INT -1

#ifndef TFT_WIDTH
#  define LCD_H_RES 1024
#endif
#ifndef TFT_HEIGHT
#  define TFT_HEIGHT 600
#endif

jd9165_lcd lcd = jd9165_lcd(LCD_RST);
gt911_touch touch = gt911_touch(TP_I2C_SDA, TP_I2C_SCL, TP_RST, TP_INT);

static const char *TAG = "JC1060P470";

// static lv_disp_draw_buf_t draw_buf;
static uint32_t *buf;
static uint32_t *buf1;

IRAM_ATTR void display_flush( lv_display_t *disp, const lv_area_t *area, uint8_t * color_map)
{
  const int offsetx1 = area->x1;
  const int offsetx2 = area->x2;
  const int offsety1 = area->y1;
  const int offsety2 = area->y2;
  lcd.lcd_draw_bitmap(offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
  //lv_disp_flush_ready(disp);
}

// No-op in direct mode.
IRAM_ATTR void display_flush_direct( lv_display_t *disp, const lv_area_t *area, uint8_t * color_map) {
}

//IRAM_ATTR bool lcd_draw_finished(void *user_data)
bool lcd_draw_finished(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    lv_display_t *drv = (lv_display_t *)user_ctx;

    lv_disp_flush_ready(drv);

    return false;
}

void touch_indev_read(lv_indev_t *indev_driver, lv_indev_data_t *data)
{
  bool touched;
  uint16_t touchX, touchY;//[1]={0}, touchY[1]={0};

  touched = touch.getTouch(&touchX, &touchY);
  // touched = touch.getTouch(touchX, touchY);

  if (!touched)  {
    data->state = LV_INDEV_STATE_REL;
  }
  else {
    data->state = LV_INDEV_STATE_PR;

    data->point.x = touchX;
    data->point.y = touchY;
    // data->point.x = touchX[0];
    // data->point.y = touchY[0];
    LOGI(TAG, "x=%d,y=%d \r\n",data->point.x,data->point.y);
  }
}

void indev_setup(lv_display_t *disp, lv_indev_t *indev) {
  /*Initialize the input device driver*/
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_indev_read);

  lv_indev_enable(indev, true);
  lv_indev_set_display(indev, disp);
}

extern esp_lcd_panel_handle_t panel_handle;

void display_setup(lv_display_t *disp, lv_indev_t *indev) {
#if LV_USE_LOG != 0
  lv_log_register_print_cb(
      lvgl_log); /* register print function for debugging */
#endif

  LOGI(TAG, "MIPI DSI Init...");
  lcd.begin();
  LOGI(TAG, "MIPI DSI Init Done.");

  LOGI(TAG, "I2C SDA: %d, SCL: %d", TP_I2C_SDA, TP_I2C_SCL);
  touch.begin();
  LOGI(TAG, "I2C Init Done.");

  lv_init();
  uint32_t buffer_size = TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t);

  buf = (uint32_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  assert(buf);

#ifdef JS1060P470C_DOUBLE_BUFFER
  buf1 =(uint32_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  assert(buf1);
#else
  buf1 = NULL;
#endif

  /* Set display buffer for display. */
  lv_display_set_buffers(disp, buf, buf1, buffer_size,
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(disp, display_flush);

  esp_lcd_dpi_panel_event_callbacks_t cbs = {
      .on_refresh_done = lcd_draw_finished,
  };
  esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, disp);

  indev_setup(disp, indev);
}

void display_setup_direct(lv_display_t *disp, lv_indev_t *indev) {
#if LV_USE_LOG != 0
  lv_log_register_print_cb(
      lvgl_log); /* register print function for debugging */
#endif

  LOGI(TAG, "MIPI DSI Init...");
  lcd.begin();
  LOGI(TAG, "MIPI DSI Init Done.");

  LOGI(TAG, "I2C SDA: %d, SCL: %d", TP_I2C_SDA, TP_I2C_SCL);
  touch.begin();
  LOGI(TAG, "I2C Init Done.");

  lv_init();
  uint32_t buffer_size = TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t);

  //buf = (uint32_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  //buf1 =(uint32_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  esp_lcd_dpi_panel_get_frame_buffer(panel_handle, 2, (void**) &buf, (void**) &buf1);

  assert(buf);
  assert(buf1);
  
  /* Set display buffer for display. */
  lv_display_set_buffers(disp, buf, buf1, buffer_size,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, display_flush_direct);

  esp_lcd_dpi_panel_event_callbacks_t cbs = {
      .on_refresh_done = lcd_draw_finished,
  };
  // esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, disp);

  indev_setup(disp, indev);
}

#endif