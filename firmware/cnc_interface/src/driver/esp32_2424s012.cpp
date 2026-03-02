// Driver for the Waveshare / Sunton ESP32-2424S012C
//   MCU    : ESP32-C3-MINI-1U (RISC-V, single core, 4 MB flash, no PSRAM)
//   Display: 1.28" round IPS, 240×240, GC9A01 via SPI2
//   Touch  : CST816S capacitive via I2C0
//
// Pin mapping (from rzeldent/platformio-espressif32-sunton board definition):
//   GC9A01 SPI  : MOSI=7, SCLK=6, CS=10, DC=2, RST=NC, BL=3
//   CST816S I2C : SDA=4, SCL=5, RST=1, INT=0
//
// Activated by the build flag  -D ESP32_2424S012=1

#ifdef ESP32_2424S012

/* Disable LovyanGFX optional subsystems we don't use to keep code size down */
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

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lvgl.h>

#include "debug.h"
#include "ui/touch_calib/touch_calib.h"

static const char *TAG = "ESP32_2424S012";

// ─── LovyanGFX class ─────────────────────────────────────────────────────────

class LGFX_ESP32_2424S012 : public lgfx::LGFX_Device {
 public:
  lgfx::Panel_GC9A01  _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Touch_CST816S _touch_instance;
  lgfx::Light_PWM     _light_instance;

  LGFX_ESP32_2424S012(void) {

    // ── SPI bus ──────────────────────────────────────────────────────────────
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI2_HOST;      // FSPI on C3
      cfg.spi_mode    = 0;
      cfg.freq_write  = 80000000;       // GC9A01 supports 80 MHz SPI writes
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = TFT_SCLK;      // GPIO 6
      cfg.pin_mosi    = TFT_MOSI;      // GPIO 7
      cfg.pin_miso    = -1;            // Not connected
      cfg.pin_dc      = TFT_DC;        // GPIO 2
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    // ── Panel ─────────────────────────────────────────────────────────────────
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs      = TFT_CS;        // GPIO 10
      cfg.pin_rst     = TFT_RST;       // -1 (NC)
      cfg.pin_busy    = -1;

      cfg.memory_width  = TFT_WIDTH;   // 240
      cfg.memory_height = TFT_HEIGHT;  // 240
      cfg.panel_width   = TFT_WIDTH;
      cfg.panel_height  = TFT_HEIGHT;

      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;    // GC9A01 write-only SPI
      cfg.invert           = true;     // IPS panel — colours inverted by default
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;

      _panel_instance.config(cfg);
    }

    // ── Backlight (PWM on GPIO 3) ─────────────────────────────────────────────
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl    = TFT_BCKL;        // GPIO 3
      cfg.invert    = false;
      cfg.freq      = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    // ── Capacitive touch (CST816S on I2C0) ───────────────────────────────────
    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = TFT_WIDTH  - 1;     // 239
      cfg.y_min = 0;
      cfg.y_max = TFT_HEIGHT - 1;     // 239

      cfg.pin_int  = TOUCH_INT;        // GPIO 0
      cfg.pin_rst  = TOUCH_RST;        // GPIO 1
      cfg.bus_shared      = false;
      cfg.offset_rotation = 0;

      cfg.i2c_port = I2C_TOUCH_PORT;   // I2C_NUM_0
      cfg.i2c_addr = I2C_TOUCH_ADDRESS; // 0x15
      cfg.pin_sda  = TOUCH_SDA;        // GPIO 4
      cfg.pin_scl  = TOUCH_SCL;        // GPIO 5
      cfg.freq     = (uint32_t)I2C_TOUCH_FREQUENCY; // 400 kHz

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

static LGFX_ESP32_2424S012 tft;

// ─── LVGL log bridge ─────────────────────────────────────────────────────────

#if LV_USE_LOG != 0
static void lvgl_log(const char *buf) {
  Serial.print(buf);
  Serial.flush();
}
#endif

// ─── Draw buffers (static — no PSRAM on C3) ──────────────────────────────────
//
// 1/10 of the screen: 240×240 / 10 × 2 bytes = 11 520 bytes each (~11 KB).
// Two buffers allow LVGL to render the next tile while the previous is flushing.

#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define DRAW_BUF_SIZE   (TFT_WIDTH * TFT_HEIGHT / 10 * BYTES_PER_PIXEL)

DRAM_ATTR static uint8_t buf1[DRAW_BUF_SIZE];
DRAM_ATTR static uint8_t buf2[DRAW_BUF_SIZE];

// ─── LVGL flush callback ─────────────────────────────────────────────────────

static void display_flush(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  if (tft.getStartCount() == 0) {
    tft.startWrite();
  }
  tft.pushImageDMA(area->x1, area->y1,
                   area->x2 - area->x1 + 1,
                   area->y2 - area->y1 + 1,
                   (lgfx::rgb565_t *)px_map);
  lv_disp_flush_ready(disp);
}

// ─── LVGL touch callback ──────────────────────────────────────────────────────

static void touch_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t touchX = 0, touchY = 0;
  bool touched = tft.getTouch(&touchX, &touchY);
  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;
    float fx = (float)touchX;
    float fy = (float)touchY;
    touch_calib_apply_inplace(&fx, &fy);
    data->point.x = (lv_coord_t)fx;
    data->point.y = (lv_coord_t)fy;
#if DEBUG_TOUCH != 0
    LOGI(TAG, "Touch: x=%d y=%d (cal: x=%.0f y=%.0f)", touchX, touchY, fx, fy);
#endif
  }
}

// ─── Public driver entry points ──────────────────────────────────────────────

// Buffers are statically allocated; this is a no-op but required by the
// driver_interface contract so that pendant.cpp can call it early.
void display_alloc() {
  LOGI(TAG, "display_alloc: static buffers (%u + %u bytes)",
       (unsigned)sizeof(buf1), (unsigned)sizeof(buf2));
}

void display_setup(lv_display_t *disp, lv_indev_t *indev) {
  LOGI(TAG, "display_setup: GC9A01 240x240 on ESP32-C3");

  tft.begin();
  tft.setRotation(0);
  tft.setBrightness(255);

#if LV_USE_LOG != 0
  lv_log_register_print_cb(lvgl_log);
#endif

  lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, display_flush);

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_indev_read);

  LOGI(TAG, "display_setup: done.");
}

#endif  // ESP32_2424S012
