#ifdef WT32_SC01_PLUS

static const char *TAG = "WT32_SC01_PLUS";

#if defined(ESP32_LVGL_ESP_DISP)

// ESP32_Display_Panel configuration to enable only necessary drivers
#define ESP_PANEL_DRIVERS_BUS_USE_I80 (1)
#define ESP_PANEL_DRIVERS_BUS_USE_I2C (1)
#define ESP_PANEL_DRIVERS_LCD_USE_ST7796 (1)
#define ESP_PANEL_DRIVERS_TOUCH_USE_FT5x06 (1)

#include <Arduino.h>
#include <lvgl.h>

#include "esp_idf_version.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 4, 0)
#error "This driver requires ESP-IDF v4.4 or later for I80 bus support. Please update your PlatformIO platform version."
#endif

// Required IDF headers for I80 bus and panel creation
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "drivers/lcd/port/esp_lcd_st7796.h"

#include "esp_display_panel.hpp"
#include "ui/touch_calib/touch_calib.h"
#include "esp_heap_caps.h"
#include "debug.h"

// Hardware configuration from platformio.ini
// Display: ST7796
// Touch: FT6336 (FT5x06 family)
// Resolution: 480x320 (after rotation)
// Bus: 8-bit parallel (I80)


#if LV_USE_LOG != 0
/* Serial debugging */
void lvgl_log(const char *buf) {
  Serial.printf(buf);
  Serial.flush();
}
#endif

// LVGL buffer (allocated in PSRAM)
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define DRAW_BUF_SIZE (TFT_WIDTH * TFT_HEIGHT / 5 * BYTES_PER_PIXEL)

static uint8_t *buf1 = NULL;

// Global driver objects
static esp_lcd_panel_handle_t panel_handle = NULL;
static std::shared_ptr<esp_panel::drivers::Touch> touch = nullptr;

// LVGL callbacks
static void display_flush(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  // The underlying esp-idf driver uses exclusive end coordinates
  esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
  lv_disp_flush_ready(disp);
}

static void touch_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
  esp_panel::drivers::TouchPoint point;
  // Read one point with 0 timeout
  if (touch->readPoints(&point, 1, 0) > 0) {
    data->state = LV_INDEV_STATE_PR;
    float fx = (float)point.x;
    float fy = (float)point.y;
    touch_calib_apply_inplace(&fx, &fy);
    data->point.x = (lv_coord_t)fx;
    data->point.y = (lv_coord_t)fy;
#if DEBUG_TOUCH != 0
    // This can be spammy
    // Serial.printf("Touch: x=%d, y=%d\n", point.x, point.y);
#endif
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void display_alloc() {
  // Allocate LVGL draw buffer from PSRAM
  buf1 = (uint8_t *)heap_caps_malloc(DRAW_BUF_SIZE,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf1) {
    LOGW(TAG,
         "LVGL draw buffer allocation failed in PSRAM, trying internal RAM");
    // Fallback to internal RAM
    buf1 = (uint8_t *)heap_caps_malloc(DRAW_BUF_SIZE,
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  assert(buf1);

}

void display_setup(lv_display_t *disp, lv_indev_t *indev) {
  LOGI(TAG, "DISPLAY SETUP WT32-SC01-PLUS with ESP_Display_Panel");

  // 1. Initialize Backlight
  esp_panel::drivers::BacklightPWM_LEDC::Config backlight_cfg = {
      .ledc_channel = esp_panel::drivers::BacklightPWM_LEDC::LEDC_ChannelPartialConfig{
          .io_num = TFT_BCKL,
          .on_level = 1,
      },
  };
  auto backlight = esp_panel::drivers::BacklightFactory::create(backlight_cfg);
  backlight->begin();
  backlight->on();
  backlight->setBrightness(100);

  // 2. Initialize LCD Panel
  // The C++ wrapper for I80 Bus was removed from the library. We now use the esp-idf functions directly.
  esp_lcd_i80_bus_handle_t i80_bus = NULL;
  esp_lcd_i80_bus_config_t bus_config = {
      .dc_gpio_num = TFT_RS,
      .wr_gpio_num = TFT_WR,
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .data_gpio_nums = {TFT_D0, TFT_D1, TFT_D2, TFT_D3, TFT_D4, TFT_D5,
                         TFT_D6, TFT_D7},
      .bus_width = 8,
      .max_transfer_bytes = DRAW_BUF_SIZE,
  };
  ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_i80_config_t io_config = {
      .cs_gpio_num = TFT_CS,
      .pclk_hz = 15 * 1000 * 1000, // 15MHz
      .trans_queue_depth = 10,
      .on_color_trans_done = NULL,
      .user_ctx = NULL,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .dc_levels = {
          .dc_idle_level = 0,
          .dc_cmd_level = 0,
          .dc_dummy_level = 0,
          .dc_data_level = 1,
      },
      .flags = {
          .swap_color_bytes = (LV_COLOR_16_SWAP != 0),
      },
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = TFT_RST,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));

  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);
  esp_lcd_panel_invert_color(panel_handle, true);
  esp_lcd_panel_swap_xy(panel_handle, true);
  esp_lcd_panel_mirror(panel_handle, false, false);
  esp_lcd_panel_disp_on_off(panel_handle, true);

  // 3. Initialize Touch Panel
  esp_panel::drivers::BusI2C::Config i2c_bus_config = {
      .host_id = (i2c_port_t)I2C_TOUCH_PORT,
      .host = esp_panel::drivers::BusI2C::HostPartialConfig{
          .sda_io_num = TOUCH_SDA,
          .scl_io_num = TOUCH_SCL,
          .sda_pullup_en = true,
          .scl_pullup_en = true,
          .clk_speed = I2C_TOUCH_FREQUENCY,
      },
      .control_panel = ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(FT5x06),
  };
  esp_panel::drivers::BusFactory::Config touch_bus_cfg(i2c_bus_config);

  esp_panel::drivers::Touch::Config touch_cfg;
  touch_cfg.device = esp_panel::drivers::Touch::DevicePartialConfig{
      // Native resolution of the touch panel
      .x_max = 320,
      .y_max = 480,
      .rst_gpio_num = -1,
      .int_gpio_num = 7,  // From old LGFX config
  };

  touch = esp_panel::drivers::TouchFactory::create("FT5x06", touch_bus_cfg,
                                                   touch_cfg);
  touch->init();
  touch->begin();
  touch->swapXY(true);  // Match LCD's swap_xy

  // 4. Setup LVGL
#if LV_USE_LOG != 0
  lv_log_register_print_cb(
      lvgl_log); /* register print function for debugging */
#endif

  // For I80 panels, LVGL needs to know when the flush is done.
  // The `drawBitmap` is synchronous for I80, so we can call
  // `lv_disp_flush_ready` right away.
  lv_display_set_flush_cb(disp, display_flush);
  lv_display_set_buffers(disp, buf1, NULL, DRAW_BUF_SIZE,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_indev_read);

  LOGI(TAG, "DONE.");
}

#else

/* Ensure LovyanGFX internal features are disabled to prevent ODR violations and bloat */
#ifndef LGFX_NO_LFS
#define LGFX_NO_LFS
#endif

#ifndef LGFX_NO_SPIFFS
#define LGFX_NO_SPIFFS
#endif

#ifndef LGFX_NO_SD
#define LGFX_NO_SD
#endif

#ifndef LGFX_NO_HTTP
#define LGFX_NO_HTTP
#endif

#ifndef LGFX_NO_PNG
#define LGFX_NO_PNG
#endif

#ifndef LGFX_NO_JPG
#define LGFX_NO_JPG
#endif

#ifndef LGFX_NO_BMP
#define LGFX_NO_BMP
#endif

#ifndef LGFX_NO_QOI
#define LGFX_NO_QOI
#endif

#include <LovyanGFX.hpp>

#include "debug.h"
#include "ui/touch_calib/touch_calib.h"

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

      //cfg.freq_write = 60000000;
      //cfg.freq_read = 24000000;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      //cfg.freq_write = 16000000;
      //cfg.freq_read = 6250000;
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
      cfg.dummy_read_pixel = 1;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;

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


// #define DEBUG_TOUCH 1
// #define USE_DMA
#define USE_DOUBLE
#define USE_PSRAM

/* Declare buffer for part of screen size; BYTES_PER_PIXEL will be 2 for RGB565. */
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#if !defined(USE_PSRAM) || defined(USE_DMA)
#define DRAW_BUFFER_SIZE (TFT_WIDTH * TFT_HEIGHT / 20 * BYTES_PER_PIXEL)
#else
#define DRAW_BUFFER_SIZE (TFT_WIDTH * TFT_HEIGHT * BYTES_PER_PIXEL)
#endif
static uint8_t *buf1 = NULL;
static uint8_t *buf2 = NULL;
static bool has_dma = false;

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

static int o_x = 0, o_y = 0, o_w = 0, o_h = 0;

void display_flush_dma(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const uint32_t w = (area->x2 - area->x1 + 1);
    const uint32_t h = (area->y2 - area->y1 + 1);
    auto _mm = millis();

    if (tft.getStartCount() == 0) {
      tft.startWrite();
    }

    if (area->x1 != o_x || area->y1 != o_y || w != o_w || h != o_h) {
      tft.setAddrWindow(area->x1, area->y1, w, h);
    }

    tft.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t *) px_map);
    tft.endWrite();

    while (tft.dmaBusy()) {
      tft.waitDMA();
    }
    lv_disp_flush_ready(disp);
    auto _mo = millis();
    //LOGI(TAG, "DMA: %d ms", _mo - _mm);
}

void touch_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = tft.getTouch(&touchX, &touchY);

  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;
    float fx = (float)touchX;
    float fy = (float)touchY;
    #if DEBUG_TOUCH != 0
      LOGI(TAG, "TOUCHED RAW: %f, %f", fx, fy);
    #endif

    touch_calib_apply_inplace(&fx, &fy);
    data->point.x = (lv_coord_t)fx;
    data->point.y = (lv_coord_t)fy;
    #if DEBUG_TOUCH != 0
      LOGI(TAG, "TOUCHED CALIB: %f, %f", fx, fy);
    #endif
  }
}

extern void ram_usage();

void display_alloc() {
  ram_usage();

  const size_t buffer_size = DRAW_BUFFER_SIZE;
  LOGI(TAG, "PRE: Allocating %d buffer (%d x %d @ %d bytes)", buffer_size, TFT_WIDTH, TFT_HEIGHT, BYTES_PER_PIXEL);

  #ifdef USE_DMA
  buf1 = (uint8_t *) heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  #ifdef USE_DOUBLE
  buf2 = (uint8_t *) heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  #endif
  has_dma = true;
  #else
  #ifdef USE_PSRAM
  # define VDO_BUF_CAPS (MALLOC_CAP_SPIRAM)
  #else
  # define VDO_BUF_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
  #endif

  buf1 = (uint8_t *) heap_caps_malloc(buffer_size, VDO_BUF_CAPS);
  #ifdef USE_DOUBLE
  buf2 = (uint8_t *) heap_caps_malloc(buffer_size, VDO_BUF_CAPS);
  #endif
  has_dma = false;
  #endif

  if (!buf1 || !buf2) {
    LOGE(TAG, "FAILED: Alloc DMA.");
    if (!buf1) { buf1 = (uint8_t *) heap_caps_malloc(buffer_size, MALLOC_CAP_8BIT); }
    #ifdef USE_DOUBLE
    if (!buf2) { buf2 = (uint8_t *) heap_caps_malloc(buffer_size, MALLOC_CAP_8BIT); }
    #endif
    has_dma = false;
  } else if (!has_dma) {
    has_dma = false;
  } else {
    LOGE(TAG, "DONE: Alloc DMA %p (%d).", buf1, sizeof(buf1));
    has_dma = true;
  }

  ram_usage();
}

void display_setup(lv_display_t *disp, lv_indev_t *indev) {
  LOGI(TAG, "DISPLAY SETUP WT32-SC01-PLUS with LGFX");

  tft.init();
  tft.initDMA();

  tft.begin();
  tft.setRotation(1);
  tft.setBrightness(255);

  vTaskDelay(pdMS_TO_TICKS(1));

#if LV_USE_LOG != 0
  lv_log_register_print_cb(
      lvgl_log); /* register print function for debugging */
#endif

  LOGI(TAG, "DISPLAY SETUP WT32-SC01-PLUS with LGFX");

  /* Set display buffer for display. */
  if (!has_dma) {
    LOGE(TAG, "FAILED: Alloc DMA.");
    lv_display_set_buffers(disp, buf1, buf2, DRAW_BUFFER_SIZE,
                          LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, display_flush);
  } else {
    lv_display_set_buffers(disp, buf1, buf2, DRAW_BUFFER_SIZE,
                          LV_DISPLAY_RENDER_MODE_PARTIAL);
    LOGE(TAG, "DONE: Set DMA Buffers.");
    lv_display_set_flush_cb(disp, display_flush_dma);
    LOGE(TAG, "DONE: Set DMA Flush.");
  }

  /*Initialize the input device driver*/
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_indev_read);

  LOGI(TAG, "DONE: DISPLAY SETUP WT32-SC01-PLUS with LGFX");
}

#endif

#endif
