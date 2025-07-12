#ifdef GUITION_4848S040

// Already defined in ini.
// #define LGFX_USE_V1

#include <LovyanGFX.hpp>

#include <driver/i2c.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

#include <lvgl.h>

class LGFX_GUITION4848S040 : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_ST7701_guition_esp32_4848S040 _panel_instance;
  lgfx::Touch_GT911 _touch_instance;
  lgfx::Light_PWM _light_instance;

  LGFX_GUITION4848S040(void) {
    {
      auto cfg = _panel_instance.config();

      cfg.memory_width = TFT_WIDTH;
      cfg.memory_height = TFT_HEIGHT;
      cfg.panel_width = TFT_WIDTH;
      cfg.panel_height = TFT_HEIGHT;

      cfg.offset_x = 0;
      cfg.offset_y = 0;

      // >>
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true; // was false
      cfg.invert = false;
      // cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true; // was false something to do with SD?
      // <<

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _panel_instance.config_detail();

      cfg.pin_cs = 39;
      cfg.pin_sclk = 48;
      cfg.pin_mosi = 47; // SDA

      _panel_instance.config_detail(cfg);
    }

    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0 = GPIO_NUM_4;   // B0
      cfg.pin_d1 = GPIO_NUM_5;   // B1
      cfg.pin_d2 = GPIO_NUM_6;   // B2
      cfg.pin_d3 = GPIO_NUM_7;   // B3
      cfg.pin_d4 = GPIO_NUM_15;  // B4
      cfg.pin_d5 = GPIO_NUM_8;   // G0
      cfg.pin_d6 = GPIO_NUM_20;  // G1
      cfg.pin_d7 = GPIO_NUM_3;   // G2
      cfg.pin_d8 = GPIO_NUM_46;  // G3
      cfg.pin_d9 = GPIO_NUM_9;   // G4
      cfg.pin_d10 = GPIO_NUM_10; // G5
      cfg.pin_d11 = GPIO_NUM_11; // R0
      cfg.pin_d12 = GPIO_NUM_12; // R1
      cfg.pin_d13 = GPIO_NUM_13; // R2
      cfg.pin_d14 = GPIO_NUM_14; // R3
      cfg.pin_d15 = GPIO_NUM_0;  // R4

      cfg.pin_henable = GPIO_NUM_18;
      cfg.pin_vsync = GPIO_NUM_17;
      cfg.pin_hsync = GPIO_NUM_16;
      cfg.pin_pclk = GPIO_NUM_21;
      cfg.freq_write = 12000000;
      // cfg.freq_write = 20000000;
      // cfg.freq_write  = 2000000;

      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 10;
      cfg.hsync_pulse_width = 8;
      cfg.hsync_back_porch = 50;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 10;
      cfg.vsync_pulse_width = 8;
      cfg.vsync_back_porch = 20;
      cfg.pclk_idle_high = 0;
      cfg.de_idle_high = 1;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = GPIO_NUM_38;
      _light_instance.config(cfg);
    }
    _panel_instance.light(&_light_instance);

    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = 480;
      cfg.y_min = 0;
      cfg.y_max = 480;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;

      cfg.i2c_port = I2C_NUM_1;

      cfg.pin_int = GPIO_NUM_NC;
      cfg.pin_sda = GPIO_NUM_19;
      cfg.pin_scl = GPIO_NUM_45;
      cfg.pin_rst = GPIO_NUM_NC;

      cfg.freq = 100000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    // {
    //   auto cfg = _light_instance.config();
    //   cfg.pin_bl = GPIO_NUM_44;
    //   _light_instance.config(cfg);
    // }
    // _panel_instance.light(&_light_instance);

    setPanel(&_panel_instance);
  }
};

LGFX_GUITION4848S040 tft;

// lv debugging can be set in lv_conf.h
#if LV_USE_LOG != 0
/* Serial debugging */
void lvgl_log(const char *buf) {
  Serial.printf(buf);
  Serial.flush();
}
#endif

/* Declare buffer for 1/10 screen size; BYTES_PER_PIXEL will be 2 for RGB565. */
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define DRAW_BUF_SIZE (TFT_WIDTH * TFT_HEIGHT / 10 * BYTES_PER_PIXEL)
static uint8_t buf1[DRAW_BUF_SIZE]; // IRAM_ATTR;
static uint8_t buf2[DRAW_BUF_SIZE]; // IRAM_ATTR;

/* Display flushing */
#define DISPLAY_DMA

#ifndef DISPLAY_DMA
void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((lgfx::rgb565_t *)px_map, w * h);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}
#else
void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  if (tft.getStartCount() == 0) {
    tft.startWrite();
  }
  tft.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1,
                   area->y2 - area->y1 + 1, (lgfx::rgb565_t *)px_map);
  lv_disp_flush_ready(disp);
}
#endif

#include "debug.h"

void touch_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = tft.getTouch(&touchX, &touchY);
  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;

#if DEBUG_TOUCH != 0
    Serial.print("Data x ");
    Serial.println(touchX);
    Serial.print("Data y ");
    Serial.println(touchY);
#endif
  }
}

void display_setup(lv_display_t *disp, lv_indev_t *indev) {
  _d(0, "DISPLAY SETUP GUITION 4848S040 ST7701");
  tft.begin();
  tft.setRotation(1);
  tft.setBrightness(255);

#if LV_USE_LOG != 0
  lv_log_register_print_cb(
      lvgl_log); /* register print function for debugging */
#endif

  /* Set display buffer for display. */
  lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, display_flush);

  /*Initialize the input device driver*/
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_indev_read);

  _d(0, "DONE.");
}

#endif
