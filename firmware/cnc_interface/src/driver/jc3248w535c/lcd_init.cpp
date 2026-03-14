// LCD hardware initialisation for the JC3248W535C (AXS15231B QSPI panel).
//
// Two build-path implementations are selected via flags:
//
//   ESP32_LVGL_ESP_DISP defined
//       → esp_panel:: C++ classes (BusQSPI / LCD_AXS15231B / BacklightPWM_LEDC).
//
//   (default – ESP32_LVGL_ESP_DISP not defined)
//       → IDF-native esp_lcd + esp_lcd_panel_axs15231b.

#ifdef JC3248W535C

#include "lcd_init.h"
#include "debug.h"
#include "perf_trace.h"

#include "esp_idf_version.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#error "This driver requires ESP-IDF v5.x (PlatformIO esp32 platform >= 6.x)."
#endif

#include "esp_timer.h"   // esp_timer_get_time() — µs wall-clock, IRAM-safe

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "JC3248W535C/lcd";

/* =========================================================================
 * Vendor initialisation command sequence (shared by both paths)
 * =========================================================================
 *
 * Sourced from the Guition JC3248W535C BSP reference.
 * The 0x35 command (Tearing Effect line ON) is intentionally sent in the
 * IDF-native sequence below; the esp_panel path sends it via the library.
 */

/* ── ESP32_Display_Panel path: uses esp_panel_lcd_vendor_init_cmd_t ────────*/
#if defined(ESP32_LVGL_ESP_DISP)

#include <memory>
#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_display_panel.hpp"
#include "drivers/lcd/port/esp_panel_lcd_vendor_types.h"

static const esp_panel_lcd_vendor_init_cmd_t vendor_init_cmds[] = {
    {0xBB,(uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5},8,0},
    {0xA0,(uint8_t[]){0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00},17,0},
    {0xA2,(uint8_t[]){0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,0x80,0x80,0x80,0x20,0xF9,0x10,0x02,0xFF,0xFF,0xF0,0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,0x5A},31,0},
    {0xD0,(uint8_t[]){0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,0x42,0xC2,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12},30,0},
    {0xA3,(uint8_t[]){0xA0,0x06,0xAA,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55},22,0},
    {0xC1,(uint8_t[]){0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0D,0x00,0xFF,0x40},30,0},
    {0xC3,(uint8_t[]){0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01},11,0},
    {0xC4,(uint8_t[]){0x00,0x24,0x33,0x80,0x00,0xEA,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50},29,0},
    {0xC5,(uint8_t[]){0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,0x10,0x10,0x00},23,0},
    {0xC6,(uint8_t[]){0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22},20,0},
    {0xC7,(uint8_t[]){0x50,0x32,0x28,0x00,0xA2,0x80,0x8F,0x00,0x80,0xFF,0x07,0x11,0x9C,0x67,0xFF,0x24,0x0C,0x0D,0x0E,0x0F},20,0},
    {0xC9,(uint8_t[]){0x33,0x44,0x44,0x01},4,0},
    {0xCF,(uint8_t[]){0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x08,0x08,0x12,0xA0,0x08},27,0},
    {0xD5,(uint8_t[]){0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00},30,0},
    {0xD6,(uint8_t[]){0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00},30,0},
    {0xD7,(uint8_t[]){0x03,0x01,0x0B,0x09,0x0F,0x0D,0x1E,0x1F,0x18,0x1D,0x1F,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F},19,0},
    {0xD8,(uint8_t[]){0x02,0x00,0x0A,0x08,0x0E,0x0C,0x1E,0x1F,0x18,0x1D,0x1F,0x19},12,0},
    {0xD9,(uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},12,0},
    {0xDD,(uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},12,0},
    {0xDF,(uint8_t[]){0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90},8,0},
    {0xE0,(uint8_t[]){0x3B,0x28,0x10,0x16,0x0C,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D},17,0},
    {0xE1,(uint8_t[]){0x37,0x28,0x10,0x16,0x0B,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F},17,0},
    {0xE2,(uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D},17,0},
    {0xE3,(uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F},17,0},
    {0xE4,(uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D},17,0},
    {0xE5,(uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0F},17,0},
    {0xA4,(uint8_t[]){0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,0x40,0x40,0x20,0xFF,0x60,0x30},16,0},
    {0xA4,(uint8_t[]){0x85,0x85,0x95,0x85},4,0},
    {0xBB,(uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},8,0},
    {0x13,(uint8_t[]){0x00},0,0},
    {0x11,(uint8_t[]){0x00},0,120},
    {0x2C,(uint8_t[]){0x00,0x00,0x00,0x00},4,0},
};

static std::shared_ptr<esp_panel::drivers::BusQSPI>      s_qspi_bus  = nullptr;
static std::shared_ptr<esp_panel::drivers::LCD_AXS15231B> s_lcd       = nullptr;
static std::shared_ptr<esp_panel::drivers::Backlight>     s_backlight = nullptr;

/* ── lcd_hw_init (ESP32_Display_Panel) ─────────────────────────────────────*/
bool lcd_hw_init(
    bool (*/*on_trans_done*/)(esp_lcd_panel_io_handle_t,
                              esp_lcd_panel_io_event_data_t *,
                              void *),
    void * /*cb_user_ctx*/)
{
    LOGI(TAG, "lcd_hw_init (ESP32_Display_Panel)");

    // 1. Backlight
    {
        esp_panel::drivers::BacklightPWM_LEDC::Config bl_cfg = {
            .ledc_channel = esp_panel::drivers::BacklightPWM_LEDC::LEDC_ChannelPartialConfig{
                .io_num   = TFT_BCKL,
                .on_level = 1,
            },
        };
        s_backlight = esp_panel::drivers::BacklightFactory::create(bl_cfg);
        if (!s_backlight) {
            LOGW(TAG, "BacklightFactory disabled – falling back to manual LEDC");
            ledc_timer_config_t ltt = {
                .speed_mode      = LEDC_LOW_SPEED_MODE,
                .duty_resolution = LEDC_TIMER_8_BIT,
                .timer_num       = LEDC_TIMER_0,
                .freq_hz         = 5000,
                .clk_cfg         = LEDC_AUTO_CLK,
            };
            ESP_ERROR_CHECK(ledc_timer_config(&ltt));
            ledc_channel_config_t lcc = {
                .gpio_num   = TFT_BCKL,
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel    = LEDC_CHANNEL_0,
                .intr_type  = LEDC_INTR_DISABLE,
                .timer_sel  = LEDC_TIMER_0,
                .duty       = 255,
                .hpoint     = 0,
            };
            ESP_ERROR_CHECK(ledc_channel_config(&lcc));
        } else {
            s_backlight->begin();
            s_backlight->on();
            s_backlight->setBrightness(100);
        }
    }

    // 2. QSPI bus
    s_qspi_bus = std::make_shared<esp_panel::drivers::BusQSPI>(
        TFT_CS, TFT_PCLK, TFT_D0, TFT_D1, TFT_D2, TFT_D3);
    s_qspi_bus->configQSPI_Mode(3);
    s_qspi_bus->configQSPI_FreqHz(80 * 1000 * 1000);
    s_qspi_bus->configQSPI_TransQueueDepth(10);
    if (!s_qspi_bus->init() || !s_qspi_bus->begin()) {
        LOGE(TAG, "QSPI bus init failed");
        return false;
    }

    // 3. LCD panel
    {
        esp_panel::drivers::LCD_AXS15231B::Config lcd_cfg;
        lcd_cfg.device = esp_panel::drivers::LCD::DevicePartialConfig{
            .reset_gpio_num          = TFT_RST,
            .rgb_ele_order           = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel          = 16,
            .flags_reset_active_high = 0,
        };
        esp_panel::drivers::LCD::VendorPartialConfig vcfg;
        vcfg.hor_res         = 320;
        vcfg.ver_res         = 480;
        vcfg.init_cmds       = vendor_init_cmds;
        vcfg.init_cmds_size  = sizeof(vendor_init_cmds) / sizeof(vendor_init_cmds[0]);
        vcfg.flags_mirror_by_cmd = 1;
        lcd_cfg.vendor = vcfg;

        s_lcd = std::make_shared<esp_panel::drivers::LCD_AXS15231B>(
            s_qspi_bus.get(), lcd_cfg);
        if (!s_lcd->init()) {
            LOGE(TAG, "LCD init failed");
            return false;
        }
        s_lcd->invertColor(true);
        s_lcd->swapXY(true);
        s_lcd->mirrorX(true);
        s_lcd->mirrorY(false);
        s_lcd->setDisplayOnOff(true);
    }

    LOGI(TAG, "LCD ready (ESP32_Display_Panel)");
    return true;
}

/* ── lcd_draw_bitmap (ESP32_Display_Panel — synchronous) ───────────────────*/
void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                     const void *data)
{
    PERF_BEGIN(draw_bitmap_sync);
    if (s_lcd) {
        s_lcd->drawBitmap(x_start, y_start, x_end - x_start, y_end - y_start,
                          data, 0);
    }
    // Synchronous path: measures the entire SPI transfer time.
    PERF_SLOWLOG(TAG, draw_bitmap_sync, 20000);  // >20 ms per chunk is unexpectedly long
}

/* ── lcd_vsync_wait (ESP32_Display_Panel — TE managed by library) ──────────*/
void lcd_vsync_wait() {
    // The ESP32_Display_Panel path does not expose the TE semaphore;
    // the library may handle tear-free internally.  No-op here.
}

/* =========================================================================*/
#else  // IDF-native path
/* =========================================================================*/

#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"

#include "axs15231b_panel.h"

static esp_lcd_panel_handle_t    s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle    = NULL;

// TE semaphore + pulse timestamp – owned here, waited via lcd_vsync_wait()
static SemaphoreHandle_t s_te_sem          = nullptr;
static bool              s_te_isr_installed = false;
static bool              s_te_isr_by_us     = false;
// Timestamp (µs, from esp_timer_get_time) of the most recent TE pulse.
// Written from the ISR, read from the flush task.  Worst-case torn read
// just returns the previous value — treated as stale, which is safe.
static volatile int64_t  s_te_timestamp_us  = 0;

/* ── TE GPIO ISR (IRAM) ──────────────────────────────────────────────────── */
typedef struct {
    SemaphoreHandle_t sem;
} te_isr_arg_t;

// One statically-allocated arg block avoids a heap allocation on the ISR path.
static te_isr_arg_t s_te_isr_arg;

static void IRAM_ATTR te_isr_handler(void *arg) {
    // Record the time of this V-blank pulse so lcd_vsync_wait can decide
    // whether it is fresh enough to use without re-waiting.
    s_te_timestamp_us = esp_timer_get_time();
    te_isr_arg_t *a = (te_isr_arg_t *)arg;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(a->sem, &hp);
    if (hp) portYIELD_FROM_ISR();
}

/* ── lcd_hw_init (IDF-native) ───────────────────────────────────────────── */
bool lcd_hw_init(
    bool (*on_trans_done)(esp_lcd_panel_io_handle_t,
                          esp_lcd_panel_io_event_data_t *,
                          void *),
    void *cb_user_ctx)
{
    LOGI(TAG, "lcd_hw_init (IDF-native)");

    /* vendor init command data blobs – must be static (C++ forbids taking
       the address of a temporary compound literal). */
    static const uint8_t ic0[]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5};
    static const uint8_t ic1[]  = {0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00};
    static const uint8_t ic2[]  = {0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,0x80,0x80,0x80,0x20,0xf9,0x10,0x02,0xff,0xff,0xF0,0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,0x5A};
    static const uint8_t ic3[]  = {0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,0x42,0xC2,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12};
    static const uint8_t ic4[]  = {0xA0,0x06,0xAa,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55};
    static const uint8_t ic5[]  = {0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0d,0x00,0xFF,0x40};
    static const uint8_t ic6[]  = {0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01};
    static const uint8_t ic7[]  = {0x00,0x24,0x33,0x80,0x00,0xea,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50};
    static const uint8_t ic8[]  = {0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,0x10,0x10,0x00};
    static const uint8_t ic9[]  = {0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22};
    static const uint8_t ic10[] = {0x50,0x32,0x28,0x00,0xa2,0x80,0x8f,0x00,0x80,0xff,0x07,0x11,0x9c,0x67,0xff,0x24,0x0c,0x0d,0x0e,0x0f};
    static const uint8_t ic11[] = {0x33,0x44,0x44,0x01};
    static const uint8_t ic12[] = {0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x08,0x08,0x12,0xA0,0x08};
    static const uint8_t ic13[] = {0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00};
    static const uint8_t ic14[] = {0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00};
    static const uint8_t ic15[] = {0x03,0x01,0x0b,0x09,0x0f,0x0d,0x1E,0x1F,0x18,0x1d,0x1f,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F};
    static const uint8_t ic16[] = {0x02,0x00,0x0a,0x08,0x0e,0x0c,0x1E,0x1F,0x18,0x1d,0x1f,0x19};
    static const uint8_t ic17[] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
    static const uint8_t ic18[] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
    static const uint8_t ic19[] = {0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90};
    static const uint8_t ic20[] = {0x3B,0x28,0x10,0x16,0x0c,0x06,0x11,0x28,0x5c,0x21,0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D};
    static const uint8_t ic21[] = {0x37,0x28,0x10,0x16,0x0b,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F};
    static const uint8_t ic22[] = {0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D};
    static const uint8_t ic23[] = {0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F};
    static const uint8_t ic24[] = {0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D};
    static const uint8_t ic25[] = {0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0F};
    static const uint8_t ic26[] = {0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,0x40,0x40,0x20,0xFF,0x60,0x30};
    static const uint8_t ic27[] = {0x85,0x85,0x95,0x85};
    static const uint8_t ic28[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    static const uint8_t ic29[] = {0x00};
    static const uint8_t ic30[] = {0x00};  // placeholder (0-byte payload for 0x35)
    static const uint8_t ic31[] = {0x00,0x00,0x00,0x00};

    static const axs15231b_lcd_init_cmd_t idf_init_cmds[] = {
        {0xBB, ic0,  8,  0},
        {0xA0, ic1,  17, 0},
        {0xA2, ic2,  31, 0},
        {0xD0, ic3,  30, 0},
        {0xA3, ic4,  22, 0},
        {0xC1, ic5,  30, 0},
        {0xC3, ic6,  11, 0},
        {0xC4, ic7,  29, 0},
        {0xC5, ic8,  23, 0},
        {0xC6, ic9,  20, 0},
        {0xC7, ic10, 20, 0},
        {0xC9, ic11, 4,  0},
        {0xCF, ic12, 27, 0},
        {0xD5, ic13, 30, 0},
        {0xD6, ic14, 30, 0},
        {0xD7, ic15, 19, 0},
        {0xD8, ic16, 12, 0},
        {0xD9, ic17, 12, 0},
        {0xDD, ic18, 12, 0},
        {0xDF, ic19, 8,  0},
        {0xE0, ic20, 17, 0},
        {0xE1, ic21, 17, 0},
        {0xE2, ic22, 17, 0},
        {0xE3, ic23, 17, 0},
        {0xE4, ic24, 17, 0},
        {0xE5, ic25, 17, 0},
        {0xA4, ic26, 16, 0},
        {0xA4, ic27, 4,  0},
        {0xBB, ic28, 8,  0},
        {0x13, ic29, 0,  0},
        {0x35, ic30, 1,  0},  // Enable Tearing Effect output (V-blank sync)
        {0x11, ic29, 0,  120},
        {0x2C, ic31, 4,  0},
    };

    // ── 1. SPI bus ─────────────────────────────────────────────────────────
    // max_transfer_sz must fit one full 320×480 frame so DMA can send it in one shot
    const size_t max_sz = TFT_WIDTH * TFT_HEIGHT * 2;  // 307200 B
    spi_bus_config_t bus_cfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        TFT_PCLK, TFT_D0, TFT_D1, TFT_D2, TFT_D3, max_sz);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // Maximise GPIO drive strength on all QSPI lines (clock + 4 data + CS).
    // The SPI bus driver configures GPIO function but leaves drive strength at
    // the default (~20 mA, GPIO_DRIVE_CAP_2).  Raising to 40 mA
    // (GPIO_DRIVE_CAP_3) sharpens edge rise/fall times, which is necessary at
    // 80 MHz QSPI where a few cm of PCB trace mismatch can cause setup/hold
    // violations on the AXS15231B input latches.  Harmless at 40 MHz.
    {
        const int qspi_pins[] = {TFT_PCLK, TFT_D0, TFT_D1, TFT_D2, TFT_D3, TFT_CS};
        for (size_t i = 0; i < sizeof(qspi_pins) / sizeof(qspi_pins[0]); i++) {
            if (qspi_pins[i] >= 0) {
                gpio_set_drive_capability((gpio_num_t)qspi_pins[i], GPIO_DRIVE_CAP_3);
            }
        }
    }

    // ── 2. Panel IO (QSPI mode-3, cmd_bits=32, quad) ─────────────────────
    esp_lcd_panel_io_spi_config_t io_cfg =
        AXS15231B_PANEL_IO_QSPI_CONFIG(TFT_CS, on_trans_done, cb_user_ctx);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &s_io_handle));

    // ── 3. LCD panel (custom vendor init with TE enabled) ─────────────────
    axs15231b_vendor_config_t vendor_cfg = {
        .init_cmds      = idf_init_cmds,
        .init_cmds_size = sizeof(idf_init_cmds) / sizeof(idf_init_cmds[0]),
        .flags          = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = TFT_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags          = { .reset_active_high = 0 },
        .vendor_config  = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(s_io_handle, &panel_cfg,
                                                &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // ── 4. TE (tearing-effect) GPIO ISR ───────────────────────────────────
    // GPIO fires at V-blank start.  Waiting before each frame eliminates tearing.
#if defined(TFT_TE) && TFT_TE >= 0
    s_te_sem = xSemaphoreCreateBinary();
    if (s_te_sem != nullptr) {
        s_te_isr_arg.sem = s_te_sem;
        gpio_config_t te_io = {};
        te_io.intr_type     = GPIO_INTR_NEGEDGE;
        te_io.mode          = GPIO_MODE_INPUT;
        te_io.pull_up_en    = GPIO_PULLUP_DISABLE;
        te_io.pull_down_en  = GPIO_PULLDOWN_DISABLE;
        te_io.pin_bit_mask  = (1ULL << TFT_TE);
        if (gpio_config(&te_io) == ESP_OK) {
            esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
            if (err == ESP_OK) {
                s_te_isr_by_us = true;
            } else if (err != ESP_ERR_INVALID_STATE) {
                LOGW(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
                vSemaphoreDelete(s_te_sem); s_te_sem = nullptr;
                goto skip_te;
            }
            if (gpio_isr_handler_add((gpio_num_t)TFT_TE, te_isr_handler,
                                     (void *)&s_te_isr_arg) == ESP_OK) {
                s_te_isr_installed = true;
                LOGI(TAG, "TE sync enabled on GPIO %d", TFT_TE);
            } else {
                LOGW(TAG, "gpio_isr_handler_add failed – TE sync disabled");
                if (s_te_isr_by_us) { gpio_uninstall_isr_service(); s_te_isr_by_us = false; }
                vSemaphoreDelete(s_te_sem); s_te_sem = nullptr;
            }
        } else {
            LOGW(TAG, "TE GPIO config failed – TE sync disabled");
            vSemaphoreDelete(s_te_sem); s_te_sem = nullptr;
        }
    }
skip_te:;
#else
    LOGI(TAG, "TFT_TE not defined – skipping TE sync");
#endif

    // ── 5. Backlight (LEDC PWM) ────────────────────────────────────────────
    ledc_timer_config_t ltt = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ltt));
    ledc_channel_config_t lcc = {
        .gpio_num   = TFT_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 255,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lcc));

    LOGI(TAG, "LCD ready (IDF-native)");
    return true;
}

/* ── lcd_draw_bitmap (IDF-native — async DMA) ──────────────────────────── */
void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                     const void *data)
{
    // esp_lcd_panel_draw_bitmap enqueues a DMA descriptor and returns immediately.
    // Actual completion is signalled via on_color_trans_done ISR.
    // Therefore this call should only take O(µs).  Long times here mean SPI
    // bus contention or the DMA descriptor queue is full.
    PERF_BEGIN(draw_submit);
    if (s_panel_handle) {
        esp_lcd_panel_draw_bitmap(s_panel_handle,
                                  x_start, y_start, x_end, y_end, data);
    }
    PERF_SLOWLOG(TAG, draw_submit, 2000);  // DMA submit > 2 ms suggests driver stall
}

/* ── lcd_vsync_wait (IDF-native — TE semaphore) ────────────────────────── */
//
// Synchronises to the panel's V-blank (tearing-effect) pulse before the
// caller starts DMA.  Uses a timestamp-based freshness check to avoid two
// failure modes:
//
//   A) "No discard": consuming a stale pulse that arrived mid-DMA during the
//      previous frame causes DMA to start at a random scan position →
//      "egg-running" tearing artefact.
//
//   B) "Always discard" (original code): unconditionally throwing away the
//      pending pulse phase-locks the wait to ~one full frame period (13–16
//      ms) even when the render just finished near V-blank → wasted latency.
//
// Solution: if a pulse is already in the semaphore AND its timestamp is
// recent (< TE_FRESH_THRESHOLD_US old), it arrived near the current V-blank
// and is safe to use immediately.  If it is older (arrived mid-previous-DMA),
// discard it and wait for the next genuine V-blank.
//
// Threshold is half the frame period (~8 ms at 60 Hz): conservative enough
// that any pulse younger than 8 ms is still at the top of the panel scan.
#define TE_FRESH_THRESHOLD_US  8000   // half of 16.67 ms @ 60 Hz

void lcd_vsync_wait() {
    if (s_te_sem != nullptr) {
        // Non-blocking peek: is there a pending pulse?
        if (xSemaphoreTake(s_te_sem, 0) == pdTRUE) {
            int64_t age_us = esp_timer_get_time() - s_te_timestamp_us;
            if (age_us >= 0 && age_us < TE_FRESH_THRESHOLD_US) {
                // Fresh — arrived ≤ 8 ms ago, still near the top of the frame.
                // Use it immediately; no blocking needed.
                return;
            }
            // Stale — arrived during the previous DMA transfer.
            // The pulse was consumed by the Take above; fall through to wait
            // for the next genuine V-blank.
        }
        // Block until the next V-blank (or up to 20 ms on timeout).
        PERF_BEGIN(te_sem_wait);
        BaseType_t got = xSemaphoreTake(s_te_sem, pdMS_TO_TICKS(20));
        PERF_SLOWLOG(TAG, te_sem_wait, 17000);  // >17 ms means TE took > frame period
        if (!got) {
            LOGD(TAG, "lcd_vsync_wait: TE timeout (no pulse in 20 ms)");
        }
    }
}

#endif // ESP32_LVGL_ESP_DISP / IDF-native paths

#endif // JC3248W535C
