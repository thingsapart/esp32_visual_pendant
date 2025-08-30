#ifdef WT32_SC01_PLUS

// Already defined in ini.
// #define LGFX_USE_V1

#include <LovyanGFX.hpp>

// SETUP LGFX PARAMETERS FOR WT32-SC01-PLUS
class LGFX_WT32SC01PLUS : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel_instance;
  lgfx::Bus_Parallel8 _bus_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_FT5x06 _touch_instance;  // FT5206, FT5306, FT5406, FT6206,
                                       // FT6236, FT6336, FT6436

 public:
  LGFX_WT32SC01PLUS(void) {
    {
      auto cfg = _bus_instance.config();

      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.pin_wr = 47;  // pin number connecting WR
      cfg.pin_rd = -1;  // pin number connecting RD
      cfg.pin_rs = 0;   // Pin number connecting RS(D/C)
      cfg.pin_d0 = 9;   // pin number connecting D0
      cfg.pin_d1 = 46;  // pin number connecting D1
      cfg.pin_d2 = 3;   // pin number connecting D2
      cfg.pin_d3 = 8;   // pin number connecting D3
      cfg.pin_d4 = 18;  // pin number connecting D4
      cfg.pin_d5 = 17;  // pin number connecting D5
      cfg.pin_d6 = 16;  // pin number connecting D6
      cfg.pin_d7 = 15;  // pin number connecting D7
      // cfg.i2s_port = I2S_NUM_0; // (I2S_NUM_0 or I2S_NUM_1)

      _bus_instance.config(cfg);               // Apply the settings to the bus.
      _panel_instance.setBus(&_bus_instance);  // Sets the bus to the panel.
    }

    {  // Set display panel control.
      auto cfg =
          _panel_instance
              .config();  // Get the structure for display panel settings.

      cfg.pin_cs = -1;  // Pin number to which CS is connected (-1 = disable)
      cfg.pin_rst = 4;  // pin number where RST is connected (-1 = disable)
      cfg.pin_busy =
          -1;  // pin number to which BUSY is connected (-1 = disable)

      // * The following setting values ​​are set to general default values
      // ​​for each panel, and the pin number (-1 = disable) to which BUSY
      // is connected, so please try commenting out any unknown items.

      cfg.memory_width = 320;   // Maximum width supported by driver IC
      cfg.memory_height = 480;  // Maximum height supported by driver IC
      cfg.panel_width = 320;    // actual displayable width
      cfg.panel_height = 480;   // actual displayable height
      cfg.offset_x = 0;         // Panel offset in X direction
      cfg.offset_y = 0;         // Panel offset in Y direction
      cfg.offset_rotation = 0;  // was 2
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;  // was false
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;  // was false something to do with SD?

      _panel_instance.config(cfg);
    }

    {  // Set backlight control. (delete if not necessary)
      auto cfg =
          _light_instance
              .config();  // Get the structure for backlight configuration.

      cfg.pin_bl = 45;      // pin number to which the backlight is connected
      cfg.invert = false;   // true to invert backlight brightness
      cfg.freq = 44100;     // backlight PWM frequency
      cfg.pwm_channel = 0;  // PWM channel number to use (7??)

      _light_instance.config(cfg);
      _panel_instance.setLight(
          &_light_instance);  // Sets the backlight to the panel.
    }

    //*
    {  // Configure settings for touch screen control. (delete if not necessary)
      auto cfg = _touch_instance.config();

      cfg.x_min =
          0;  // Minimum X value (raw value) obtained from the touchscreen
      cfg.x_max =
          319;  // Maximum X value (raw value) obtained from the touchscreen
      cfg.y_min = 0;  // Minimum Y value obtained from touchscreen (raw value)
      cfg.y_max =
          479;  // Maximum Y value (raw value) obtained from the touchscreen
      cfg.pin_int = 7;  // pin number to which INT is connected
      cfg.bus_shared =
          false;  // set true if you are using the same bus as the screen
      cfg.offset_rotation = 0;

      // For I2C connection
      cfg.i2c_port = 0;     // Select I2C to use (0 or 1)
      cfg.i2c_addr = 0x38;  // I2C device address number
      cfg.pin_sda = 6;      // pin number where SDA is connected
      cfg.pin_scl = 5;      // pin number to which SCL is connected
      cfg.freq = 400000;    // set I2C clock

      _touch_instance.config(cfg);
      _panel_instance.setTouch(
          &_touch_instance);  // Set the touchscreen to the panel.
    }
    //*/
    setPanel(&_panel_instance);  // Sets the panel to use.
  }
};

#include <lvgl.h>

LGFX_WT32SC01PLUS tft;

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
static uint8_t buf1[TFT_WIDTH * TFT_HEIGHT / 5 * BYTES_PER_PIXEL];

/* Display flushing */
void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((lgfx::rgb565_t *)px_map, w * h);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

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
  tft.begin();
  tft.setRotation(1);
  tft.setBrightness(255);

#if LV_USE_LOG != 0
  lv_log_register_print_cb(
      lvgl_log); /* register print function for debugging */
#endif

  /* Set display buffer for display. */
  lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, display_flush);

  /*Initialize the input device driver*/
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_indev_read);
}

#endif
