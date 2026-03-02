// Driver for the Makerfabs / Jingcai JC3248W535C ESP32-S3 3.2" QSPI display
//
// Hardware:
//   MCU     : ESP32-S3-WROOM-1-N8R8 (240 MHz, 8 MB QIO Flash, 8 MB OPI PSRAM)
//   Display : 3.2" IPS 320×480 (portrait), AXS15231B via QSPI (SPI2)
//   Touch   : AXS15231B integrated (accessed via I2C)
//
// QSPI LCD pin mapping:
//
// NOTE: this board is unusual because the QSPI interface used by the
// display is the same physical SPI2 bus that carries the on‑module OPI
// PSRAM.  DMA reads from PSRAM therefore contend with the panel and
// will corrupt the image (typically leaving only a few narrow columns
// of valid pixels).  As a consequence the driver avoids PSRAM for LVGL
// draw buffers and only uses internal RAM unless a later allocation
// failure forces a fallback.
//   CS    = GPIO 45    PCLK  = GPIO 47
//   DATA0 = GPIO 21    DATA1 = GPIO 48    DATA2 = GPIO 40    DATA3 = GPIO 39
//   RST   = NC  (-1)   BL    = GPIO  1    TE    = GPIO 38
//
// I2C Touch pin mapping (I2C_NUM_0):
//   SCL = GPIO 8    SDA = GPIO 4    INT = NC (-1)    RST = NC (-1)
//   Address: 0x3B
//
// Logical resolution (landscape after rotation=1): TFT_WIDTH=480, TFT_HEIGHT=320
//
// Key differences vs. WT32-SC01-Plus:
//   • QSPI serial bus   (not 8-bit parallel I80)
//   • AXS15231B panel   (not ST7796)
//   • AXS15231B touch   (not FT5x06/FT6336)
//   • 8 MB Flash / OPI PSRAM (not 16 MB)
//   • No encoder header on this board
//   • I2C_NUM_0 for touch (not I2C_NUM_1)
//   • Touch I2C address 0x3B (not 0x38)
//   • sdkconfig: CONFIG_SPIRAM_MODE_QUAD (not OPI) — see note below
//
// sdkconfig / PSRAM note:
//   The JC3248W535C N8R8 module has OPI PSRAM wired in octal mode (same
//   electrical interface as on the WT32-SC01-Plus OPI variant).  Use:
//     board_build.psram_type = opi
//     CONFIG_SPIRAM_MODE_OCT=y
//     CONFIG_SPIRAM_SPEED_80M=y
//   The 8 MB flash means the 16 MB partition table from the WT32 build is
//   replaced with config/esp32/partitions_8MB.csv.
//
// Activated by build flag: -D JC3248W535C=1

#include <memory>   // for std::shared_ptr used in globals

#ifdef JC3248W535C

/*
 * Split implementation depending on whether the Arduino ESP32_Display_Panel
 * library is being used.  The PlatformIO build flags for the QSPI display
 * boards set `-D ESP32_LVGL_ESP_DISP` when the library should drive the
 * panel, otherwise the original IDF-native code (below) is compiled.
 *
 * A secondary flag `JC3248W535C_ESP_DISP_TOUCH` may be enabled in the native
 * path to pull in the esp_panel touch driver and helper `touch_indev_read`
 * from the ESP32_Display_Panel branch.  When that flag is off the driver
 * falls back to the existing raw I2C touch implementation.
 */

/* touch object used by LVGL panel code or optionally enabled in the
   native IDF path when JC3248W535C_ESP_DISP_TOUCH is defined */
#if defined(ESP32_LVGL_ESP_DISP) || defined(JC3248W535C_ESP_DISP_TOUCH)

#include "esp_heap_caps.h"
#include "driver/i2c.h"

#include "esp_display_panel.hpp"
#include "drivers/touch/esp_panel_touch_axs15231b.hpp"

static std::shared_ptr<esp_panel::drivers::Touch> _touch = nullptr;

#endif

#if defined(JC3248W535C_TOUCH_DRV)

#include "driver/axs15231b_touch.h"

// Touch module
#define TFT_rot   1
#define TFT_res_W 320
#define TFT_res_H 480

#define Touch_SDA  4
#define Touch_SCL  8
#define Touch_INT  3
#define Touch_ADDR 0x3B

AXS15231B_Touch touch(Touch_SCL, Touch_SDA, Touch_INT, Touch_ADDR, TFT_rot);
#endif


#if defined(ESP32_LVGL_ESP_DISP)

#include <Arduino.h>
#include <lvgl.h>

#include "esp_idf_version.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#error "This driver requires ESP-IDF v5.x (PlatformIO esp32 platform >= 6.x)."
#endif

#include "driver/ledc.h"

#include "drivers/lcd/port/esp_panel_lcd_vendor_types.h"    // for esp_panel_lcd_vendor_init_cmd_t
#include "ui/touch_calib/touch_calib.h"
#include "debug.h"

/* vendor initialization commands copied from the original IDF driver. */
static const esp_panel_lcd_vendor_init_cmd_t vendor_init_cmds[] = {
    {0xBB, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5}, 8,0},
    {0xA0, (uint8_t[]){0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00},17,0},
    {0xA2, (uint8_t[]){0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,0x80,0x80,0x80,0x20,0xF9,0x10,0x02,0xFF,0xFF,0xF0,0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,0x5A},31,0},
    {0xD0, (uint8_t[]){0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,0x42,0xC2,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12},30,0},
    {0xA3, (uint8_t[]){0xA0,0x06,0xAA,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55},22,0},
    {0xC1, (uint8_t[]){0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0D,0x00,0xFF,0x40},30,0},
    {0xC3, (uint8_t[]){0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01},11,0},
    {0xC4, (uint8_t[]){0x00,0x24,0x33,0x80,0x00,0xEA,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50},29,0},
    {0xC5, (uint8_t[]){0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,0x10,0x10,0x00},23,0},
    {0xC6, (uint8_t[]){0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22},20,0},
    {0xC7, (uint8_t[]){0x50,0x32,0x28,0x00,0xA2,0x80,0x8F,0x00,0x80,0xFF,0x07,0x11,0x9C,0x67,0xFF,0x24,0x0C,0x0D,0x0E,0x0F},20,0},
    {0xC9, (uint8_t[]){0x33,0x44,0x44,0x01},4,0},
    {0xCF, (uint8_t[]){0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x08,0x08,0x12,0xA0,0x08},27,0},
    {0xD5, (uint8_t[]){0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00},30,0},
    {0xD6, (uint8_t[]){0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00},30,0},
    {0xD7, (uint8_t[]){0x03,0x01,0x0B,0x09,0x0F,0x0D,0x1E,0x1F,0x18,0x1D,0x1F,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F},19,0},
    {0xD8, (uint8_t[]){0x02,0x00,0x0A,0x08,0x0E,0x0C,0x1E,0x1F,0x18,0x1D,0x1F,0x19},12,0},
    {0xD9, (uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},12,0},
    {0xDD, (uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},12,0},
    {0xDF, (uint8_t[]){0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90},8,0},
    {0xE0, (uint8_t[]){0x3B,0x28,0x10,0x16,0x0C,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D},17,0},
    {0xE1, (uint8_t[]){0x37,0x28,0x10,0x16,0x0B,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F},17,0},
    {0xE2, (uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D},17,0},
    {0xE3, (uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F},17,0},
    {0xE4, (uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D},17,0},
    {0xE5, (uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0F},17,0},
    {0xA4, (uint8_t[]){0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,0x40,0x40,0x20,0xFF,0x60,0x30},16,0},
    {0xA4, (uint8_t[]){0x85,0x85,0x95,0x85},4,0},
    {0xBB, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},8,0},
    {0x13, (uint8_t[]){0x00},0,0},
    {0x11, (uint8_t[]){0x00},0,120},
    {0x2C, (uint8_t[]){0x00,0x00,0x00,0x00},4,0},
};

// globals used by both branches
static const char *TAG = "JC3248W535C";

/* scratch objects (allocated only when ESP32_LVGL_ESP_DISP is defined) */
#if defined(ESP32_LVGL_ESP_DISP)
static std::shared_ptr<esp_panel::drivers::BusQSPI> _qspi_bus = nullptr;
static std::shared_ptr<esp_panel::drivers::LCD_AXS15231B> _lcd = nullptr;
static std::shared_ptr<esp_panel::drivers::Backlight> _backlight = nullptr;
#endif

#else // !ESP32_LVGL_ESP_DISP

static const char *TAG = "JC3248W535C";

#include <Arduino.h>
#include <lvgl.h>

#include "esp_idf_version.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#error "This driver requires ESP-IDF v5.x (PlatformIO esp32 platform >= 6.x)."
#endif

#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "jc3248w535c/axs15231b_panel.h"
#include "ui/touch_calib/touch_calib.h"

#ifdef JC3248W535C_ESP_DISP_TOUCH
#include "esp_display_panel.hpp"
#include "drivers/touch/esp_panel_touch_axs15231b.hpp"
#endif

#include "debug.h"

#endif // ESP32_LVGL_ESP_DISP

/* ── Constants ─────────────────────────────────────────────────────────────*/
#define BYTES_PER_PIXEL    (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
// Partial rendering: 1/5 of screen (used by ESP32_Display_Panel path)
#define DRAW_BUF_SIZE      (TFT_WIDTH * TFT_HEIGHT / 5 * BYTES_PER_PIXEL)
// Full-frame buffer required by IDF-native path (AXS15231B needs full-screen updates)
#define DRAW_BUF_FULL_SIZE (TFT_WIDTH * TFT_HEIGHT * BYTES_PER_PIXEL)
// Size of each DMA bounce-buffer chunk (1/20 frame, internal SRAM, DMA-capable).
// The LVGL draw buffer lives in PSRAM; DMA cannot directly read PSRAM safely because
// the CPU D-cache may hold dirty lines that the GDMA never sees – producing the
// characteristic ~8×8 block mosaic.  Copying PSRAM→internal-DMA-RAM first solves it.
//
// Divisor controls the SRAM cost vs. flush iteration count tradeoff:
//   /10 → 30,720 B each →  61 KB total, 10 DMA transactions/frame
//   /20 → 15,360 B each →  31 KB total, 20 DMA transactions/frame  ← chosen
//   /40 →  7,680 B each →  15 KB total, 40 DMA transactions/frame
// At QSPI 80 MHz the per-transaction overhead of 20 chunks is negligible.
#define TRANS_DIV          40
#define TRANS_SIZE         (TFT_WIDTH * (TFT_HEIGHT / TRANS_DIV) * BYTES_PER_PIXEL)

/* ── AXS15231B raw I2C touch protocol ──────────────────────────────────────
 * Write a fixed 11-byte payload to trigger a touch-data read, then read 8
 * bytes back.  Data layout (per AXS15231B datasheet):
 *   [0] gesture   [1] num_touches
 *   [2] x_h (bits 3:0 = x[11:8])   [3] x_l (x[7:0])
 *   [4] y_h (bits 3:0 = y[11:8])   [5] y_l (y[7:0])
 *   [6] pressure  [7] area
 */
static const uint8_t AXS_TOUCH_READ_CMD[11] = {
    0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00
};

/* ── DMA bounce buffers (shared by both implementation paths) ───────────────
 *
 * These small buffers live in internal SRAM with MALLOC_CAP_DMA so that the
 * GDMA can read them safely.  Each flush copies the PSRAM draw buffer into
 * these buffers one chunk at a time, avoiding the cache-coherency issue that
 * arises when DMA reads directly from cache-backed PSRAM.
 */
static uint8_t *trans_buf1 = NULL;
static uint8_t *trans_buf2 = NULL;

#if defined(ESP32_LVGL_ESP_DISP)
/* ── Module globals (ESP32_Display_Panel) ───────────────────────────────────*/
static uint8_t *draw_buf = NULL;

/* flush callback uses the C++ panel object */
static void display_flush(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
    // In LVGL v9, lv_display_set_rotation(ROTATION_90) swaps the *logical*
    // resolution reported to the UI (480×320 landscape) and maps touch events,
    // but does NOT physically rotate the pixel buffer in FULL render mode.
    // The buffer delivered here is always in LOGICAL landscape layout:
    //   480 columns × 320 rows  (L_width × L_height)
    // The physical AXS15231B panel expects portrait data:
    //   320 columns × 480 rows  (P_width × P_height = TFT_WIDTH × TFT_HEIGHT)
    //
    // 90° CW rotation formula:
    //   physical(col=C, row=R) = logical_buf[(L_height-1-C) * L_width + R]
    //   where C ∈ [0, P_width), R ∈ [0, P_height)
    //
    // Loop order — OUTER over C, INNER over R:
    //   Each inner loop reads logical row (L_height-1-C) sequentially
    //   (stride-1 PSRAM reads = cache-friendly).
    //   Writes stride P_width into the SRAM bounce buffer (fine in SRAM).
    constexpr int L_width        = TFT_HEIGHT;  // 480 — logical buffer columns
    constexpr int L_height       = TFT_WIDTH;   // 320 — logical buffer rows
    constexpr int P_width        = TFT_WIDTH;   // 320 — physical panel columns
    constexpr int P_height       = TFT_HEIGHT;  // 480 — physical panel rows
    constexpr int rows_per_chunk = TRANS_SIZE / (P_width * BYTES_PER_PIXEL);

    if (_lcd) {
        for (int R_start = 0; R_start < P_height; R_start += rows_per_chunk) {
            int chunk_rows = ((R_start + rows_per_chunk) <= P_height)
                             ? rows_per_chunk : (P_height - R_start);
            uint8_t  *tbuf     = ((R_start / rows_per_chunk) & 1) ? trans_buf2 : trans_buf1;
            uint16_t *tbuf_u16 = (uint16_t *)tbuf;
            // Rotate + fused byte-swap: read PSRAM once, write transposed to SRAM
            for (int C = 0; C < P_width; C++) {
                const uint16_t *src = (const uint16_t *)px_map
                                      + (L_height - 1 - C) * L_width + R_start;
                for (int r = 0; r < chunk_rows; r++) {
                    uint16_t p = src[r];  // sequential PSRAM read ✓
                    tbuf_u16[r * P_width + C] = (uint16_t)((p >> 8) | (p << 8));
                }
            }
            _lcd->drawBitmap(0, R_start, P_width, chunk_rows, tbuf, 0);
        }
    }
    lv_display_flush_ready(disp);
}

/* touch callback driven by library Touch object */
static void touch_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
    if (!_touch) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    esp_panel::drivers::TouchPoint point;
    if (_touch->readPoints(&point, 1, 0) > 0) {
        data->state = LV_INDEV_STATE_PR;
        float fx = (float)point.x;
        float fy = (float)point.y;
        touch_calib_apply_inplace(&fx, &fy);
        data->point.x = (lv_coord_t)fx;
        data->point.y = (lv_coord_t)fy;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

#else // original IDF-native callbacks

/* ── Module globals ─────────────────────────────────────────────────────────*/
static esp_lcd_panel_handle_t    panel_handle    = NULL;
static esp_lcd_panel_io_handle_t io_handle       = NULL;
static uint8_t                  *draw_buf        = NULL;

// TE (tearing-effect) V-blank sync
static SemaphoreHandle_t te_sync_sem    = nullptr;
static bool              te_isr_installed = false;
static bool              te_isr_by_us    = false;

// DMA transfer-done semaphore (gates the bounce-buffer loop in display_flush)
static SemaphoreHandle_t trans_done_sem = nullptr;

/* ── TE GPIO ISR (IRAM) ──────────────────────────────────────────────────────*/
static void IRAM_ATTR te_isr_handler(void *arg) {
    SemaphoreHandle_t sem = (SemaphoreHandle_t)arg;
    BaseType_t needYield = pdFALSE;
    xSemaphoreGiveFromISR(sem, &needYield);
    if (needYield == pdTRUE) portYIELD_FROM_ISR();
}

/* ── Async DMA-done callback ─────────────────────────────────────────────────
 * Fires when the SPI DMA finishes sending one chunk.  Signals trans_done_sem
 * so the flush loop can dispatch the next chunk (or unblock after the last).
 * IRAM_ATTR: called from SPI DMA ISR — must not cause a flash-cache miss.
 */
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx) {
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(trans_done_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
    return false;
}

/* ── LVGL display-flush callback ────────────────────────────────────────────
 *
 * Key points for AXS15231B QSPI:
 *   1. The panel IGNORES x/y window coordinates – it always writes from (0,0).
 *      Partial updates therefore corrupt the frame.  We must always send the
 *      full frame and use LV_DISPLAY_RENDER_MODE_FULL.
 *   2. The SPI host transmits bytes MSB-first, so the high-byte of each RGB565
 *      word arrives first.  LVGL stores pixels little-endian (low byte first),
 *      so we must byte-swap every pixel before transmission.
 *   3. Flush-done is signalled asynchronously by on_color_trans_done once the
 *      DMA transfer completes, not synchronously here.
 *   4. We wait for the TE (tearing-effect) V-blank pulse before starting the
 *      transfer to prevent visible tearing.
 */
static void display_flush(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
    // In LVGL v9, lv_display_set_rotation(ROTATION_90) swaps the *logical*
    // resolution reported to the UI (480×320 landscape) and maps touch events,
    // but does NOT physically rotate the pixel buffer in FULL render mode.
    // The buffer delivered here is always in LOGICAL landscape layout:
    //   480 columns × 320 rows  (L_width × L_height)
    // The physical AXS15231B panel expects portrait data:
    //   320 columns × 480 rows  (P_width × P_height = TFT_WIDTH × TFT_HEIGHT)
    //
    // 90° CW rotation formula:
    //   physical(col=C, row=R) = logical_buf[(L_height-1-C) * L_width + R]
    //   where C ∈ [0, P_width), R ∈ [0, P_height)
    //
    // Loop order — OUTER over C, INNER over R:
    //   Each inner loop reads logical row (L_height-1-C) sequentially
    //   (stride-1 PSRAM reads = cache-friendly).
    //   Writes go to tbuf at stride P_width (SRAM — no penalty).
    //
    // Double-buffering: rotating chunk N into tbuf while DMA sends chunk N-1.
    constexpr int L_width        = TFT_HEIGHT;  // 480 — logical buffer columns
    constexpr int L_height       = TFT_WIDTH;   // 320 — logical buffer rows
    constexpr int P_width        = TFT_WIDTH;   // 320 — physical panel columns
    constexpr int P_height       = TFT_HEIGHT;  // 480 — physical panel rows
    constexpr int rows_per_chunk = TRANS_SIZE / (P_width * BYTES_PER_PIXEL);

    // 1. Wait for TE falling edge (V-blank sync) before first chunk.
    if (te_sync_sem != nullptr) {
        xSemaphoreTake(te_sync_sem, 0);
        xSemaphoreTake(te_sync_sem, pdMS_TO_TICKS(20));
    }

    xSemaphoreGive(trans_done_sem); // prime: first Take in loop succeeds immediately
    for (int R_start = 0; R_start < P_height; R_start += rows_per_chunk) {
        int chunk_rows = ((R_start + rows_per_chunk) <= P_height)
                         ? rows_per_chunk : (P_height - R_start);
        uint8_t  *tbuf     = ((R_start / rows_per_chunk) & 1) ? trans_buf2 : trans_buf1;
        uint16_t *tbuf_u16 = (uint16_t *)tbuf;

        // Rotate + fused byte-swap: read PSRAM once, write transposed+swapped to SRAM
        for (int C = 0; C < P_width; C++) {
            const uint16_t *src = (const uint16_t *)px_map
                                  + (L_height - 1 - C) * L_width + R_start;
            for (int r = 0; r < chunk_rows; r++) {
                uint16_t p = src[r];  // sequential PSRAM read ✓
                tbuf_u16[r * P_width + C] = (uint16_t)((p >> 8) | (p << 8));
            }
        }

        xSemaphoreTake(trans_done_sem, portMAX_DELAY);  // wait for previous DMA done
        if (esp_lcd_panel_draw_bitmap(panel_handle,
                                      0, R_start,
                                      P_width, R_start + chunk_rows,
                                      tbuf) != ESP_OK) {
            xSemaphoreGive(trans_done_sem);
            break;
        }
    }
    xSemaphoreTake(trans_done_sem, portMAX_DELAY);  // wait for final DMA done
    lv_display_flush_ready(disp);
}

/* ── LVGL touch-input callback ──────────────────────────────────────────────*/
static void touch_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
#if defined(JC3248W535C_TOUCH_DRV)
    uint16_t x, y;
    if (touch.touched()) {
        // Read touched point from touch module
        touch.readData(&x, &y);

        // Set the coordinates
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
#elif defined(JC3248W535C_ESP_DISP_TOUCH)
    /* use the esp_panel driver when the new flag is enabled */
    if (!_touch) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    esp_panel::drivers::TouchPoint point;
    if (_touch->readPoints(&point, 1, 0) > 0) {
        data->state = LV_INDEV_STATE_PR;
        float fx = TFT_WIDTH - (float)point.y;
        float fy = TFT_HEIGHT - (float)point.x;
        touch_calib_apply_inplace(&fx, &fy);
        data->point.x = (lv_coord_t)fx;
        data->point.y = (lv_coord_t)fy;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
#else
    // Write read-trigger command
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_TOUCH_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, AXS_TOUCH_READ_CMD, sizeof(AXS_TOUCH_READ_CMD), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin((i2c_port_t)I2C_TOUCH_PORT, cmd,
                                         pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    // Read 8 bytes of touch data
    uint8_t td[8] = {0};
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_TOUCH_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, td, sizeof(td), I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin((i2c_port_t)I2C_TOUCH_PORT, cmd,
                                pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK || td[1] == 0) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    // Decode coordinates (native portrait 320×480)
    uint16_t raw_x = (uint16_t)((td[2] & 0x0F) << 8) | td[3];
    uint16_t raw_y = (uint16_t)((td[4] & 0x0F) << 8) | td[5];

    float fx = (float)raw_x;
    float fy = (float)raw_y;
    touch_calib_apply_inplace(&fx, &fy);

    data->state   = LV_INDEV_STATE_PR;
    data->point.x = (lv_coord_t)fx;
    data->point.y = (lv_coord_t)fy;

#if DEBUG_TOUCH != 0
    LOGI(TAG, "TOUCH: raw(%d,%d) → (%d,%d)", raw_x, raw_y,
         data->point.x, data->point.y);
#endif
#endif
}

#endif // ESP32_LVGL_ESP_DISP

/* ── Public API ─────────────────────────────────────────────────────────────*/

void display_alloc() {
    extern void ram_usage();
    ram_usage();

    /*
     * The JC3248W535C board uses a QSPI display that shares the same
     * physical SPI2 bus as the on‑module OPI PSRAM.  DMA transfers from
     * PSRAM will collide with the display transactions and typically
     * result in a nearly blank screen with only a few stray columns of
     * pixels (the exact symptom described by users).  To avoid the
     * contention we allocate the LVGL draw buffer from internal memory
     * (which is accessible by SPI2 DMA without touching the PSRAM bus).
     *
     * In the unlikely event that there isn’t enough internal RAM we fall
     * back to PSRAM but log a warning so the problem can be diagnosed.
     */
    LOGI(TAG, "Allocating LVGL draw buffer: %u bytes (PSRAM) + 2x%u B DMA bounce bufs (internal)",
         (unsigned)DRAW_BUF_FULL_SIZE, (unsigned)TRANS_SIZE);

    // ── Full-frame LVGL draw buffer in PSRAM ─────────────────────────────────
    // Both paths use a full-frame buffer because AXS15231B ignores CASET/RASET
    // window coordinates and always renders from (0,0).
    draw_buf = (uint8_t *)heap_caps_malloc(DRAW_BUF_FULL_SIZE,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!draw_buf) {
        LOGW(TAG, "PSRAM full-frame alloc failed, trying internal RAM");
        draw_buf = (uint8_t *)heap_caps_malloc(DRAW_BUF_FULL_SIZE,
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    }
    assert(draw_buf && "LVGL draw buffer allocation failed");

    // ── DMA bounce buffers in internal SRAM ──────────────────────────────────
    // LVGL renders into draw_buf (PSRAM) via the D-cache.  DMA reads from
    // physical PSRAM directly and bypasses the cache, so dirty cache lines
    // are invisible to the GDMA and produce stale/random pixels (the 8×8
    // mosaic).  These small internal-SRAM buffers are DMA-safe; each flush
    // copies one chunk of PSRAM through the cache into a trans_buf and then
    // DMA-s from there.  Two alternating buffers allow the CPU copy and the
    // DMA transfer to overlap on consecutive chunks.
    trans_buf1 = (uint8_t *)heap_caps_malloc(TRANS_SIZE,
                                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    trans_buf2 = (uint8_t *)heap_caps_malloc(TRANS_SIZE,
                                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(trans_buf1 && trans_buf2 && "DMA bounce buffer allocation failed");

    ram_usage();
}

#if defined(ESP32_LVGL_ESP_DISP)
void display_setup(lv_display_t *disp, lv_indev_t *indev) {
    LOGI(TAG, "DISPLAY SETUP JC3248W535C (ESP32_Display_Panel)");

    // 1. Backlight
    {
        esp_panel::drivers::BacklightPWM_LEDC::Config backlight_cfg = {
            .ledc_channel = esp_panel::drivers::BacklightPWM_LEDC::LEDC_ChannelPartialConfig{
                .io_num = TFT_BCKL,
                .on_level = 1,
            },
        };
        _backlight = esp_panel::drivers::BacklightFactory::create(backlight_cfg);
        if (!_backlight) {
            LOGW(TAG, "Backlight factory disabled, falling back to manual LEDC init");
            // manual configuration similar to original IDF code
            ledc_timer_config_t ledc_timer_cfg = {
                .speed_mode      = LEDC_LOW_SPEED_MODE,
                .duty_resolution = LEDC_TIMER_8_BIT,
                .timer_num       = LEDC_TIMER_0,
                .freq_hz         = 5000,
                .clk_cfg         = LEDC_AUTO_CLK,
            };
            ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer_cfg));
            ledc_channel_config_t ledc_ch_cfg = {
                .gpio_num   = TFT_BCKL,
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel    = LEDC_CHANNEL_0,
                .intr_type  = LEDC_INTR_DISABLE,
                .timer_sel  = LEDC_TIMER_0,
                .duty       = 255,
                .hpoint     = 0,
            };
            ESP_ERROR_CHECK(ledc_channel_config(&ledc_ch_cfg));
        } else {
            _backlight->begin();
            _backlight->on();
            _backlight->setBrightness(100);
        }
    }

    // 2. QSPI bus
    _qspi_bus = std::make_shared<esp_panel::drivers::BusQSPI>(
        TFT_CS, TFT_PCLK, TFT_D0, TFT_D1, TFT_D2, TFT_D3);
    _qspi_bus->configQSPI_Mode(3);
    _qspi_bus->configQSPI_FreqHz(40 * 1000 * 1000);
    _qspi_bus->configQSPI_TransQueueDepth(10);
    ESP_ERROR_CHECK(_qspi_bus->init() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(_qspi_bus->begin() ? ESP_OK : ESP_FAIL);

    // 3. LCD panel object with full configuration (includes vendor commands)
    {
        esp_panel::drivers::LCD_AXS15231B::Config lcd_cfg;
        // device parameters
        lcd_cfg.device = esp_panel::drivers::LCD::DevicePartialConfig{
            .reset_gpio_num = TFT_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .flags_reset_active_high = 0,
        };
        // vendor init sequence from earlier array
        esp_panel::drivers::LCD::VendorPartialConfig vcfg;
        vcfg.hor_res = 320;
        vcfg.ver_res = 480;
        vcfg.init_cmds = vendor_init_cmds;
        vcfg.init_cmds_size = sizeof(vendor_init_cmds) / sizeof(vendor_init_cmds[0]);
        vcfg.flags_mirror_by_cmd = 1;
        lcd_cfg.vendor = vcfg;

        _lcd = std::make_shared<esp_panel::drivers::LCD_AXS15231B>(
            _qspi_bus.get(), lcd_cfg);
        ESP_ERROR_CHECK(_lcd->init() ? ESP_OK : ESP_FAIL);
        _lcd->invertColor(true);
        _lcd->swapXY(true);
        _lcd->mirrorX(true);
        _lcd->mirrorY(false);
        _lcd->setDisplayOnOff(true);
    }

    // 4. Touch panel via I2C
    {
        // first attempt to create via factory (requires I2C enabled macros)
        esp_panel::drivers::BusI2C::Config i2c_cfg = {
            .host_id = (i2c_port_t)I2C_TOUCH_PORT,
            .host = esp_panel::drivers::BusI2C::HostPartialConfig{
                .sda_io_num = TOUCH_SDA,
                .scl_io_num = TOUCH_SCL,
                .sda_pullup_en = true,
                .scl_pullup_en = true,
                .clk_speed = I2C_TOUCH_FREQUENCY,
            },
            .control_panel = ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(AXS15231B),
        };
        esp_panel::drivers::BusFactory::Config touch_bus_cfg(i2c_cfg);

        esp_panel::drivers::TouchAXS15231B::Config touch_cfg;
        touch_cfg.device = esp_panel::drivers::Touch::DevicePartialConfig{
            .x_max = 320,
            .y_max = 480,
            .rst_gpio_num = -1,
            .int_gpio_num = -1,
        };

        // try factory first
        _touch = std::make_shared<esp_panel::drivers::TouchAXS15231B>(
            touch_bus_cfg, touch_cfg);
        if (_touch && _touch->init() && _touch->begin()) {
            _touch->swapXY(true);
        } else {
            LOGW(TAG, "Touch factory disabled; creating I2C bus manually");
            // manual bus creation
            esp_panel::drivers::BusI2C::ControlPanelFullConfig cp_cfg =
                ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(AXS15231B);
            auto i2c_bus = std::make_shared<esp_panel::drivers::BusI2C>(
                TOUCH_SCL, TOUCH_SDA, cp_cfg);
            ESP_ERROR_CHECK(i2c_bus->init() ? ESP_OK : ESP_FAIL);
            ESP_ERROR_CHECK(i2c_bus->begin() ? ESP_OK : ESP_FAIL);

            _touch.reset();
            _touch = std::make_shared<esp_panel::drivers::TouchAXS15231B>(
                i2c_bus.get(), touch_cfg);
            ESP_ERROR_CHECK(_touch ? ESP_OK : ESP_FAIL);
            ESP_ERROR_CHECK(_touch->init() ? ESP_OK : ESP_FAIL);
            ESP_ERROR_CHECK(_touch->begin() ? ESP_OK : ESP_FAIL);
            _touch->swapXY(true);
        }
    }

    // 5. LVGL wiring
#if LV_USE_LOG != 0
    lv_log_register_print_cb([](const char *buf) {
        Serial.printf("%s", buf);
        Serial.flush();
    });
#endif
    lv_display_set_flush_cb(disp, display_flush);
    // DRAW_BUF_FULL_SIZE: AXS15231B requires full-frame updates; partial tiles
    // all land at (0,0) producing the garbled-block pattern.
    lv_display_set_buffers(disp, draw_buf, NULL, DRAW_BUF_FULL_SIZE,
                           LV_DISPLAY_RENDER_MODE_FULL);
    // The physical panel is 320 (H) × 480 (V) portrait.  Build flags define
    // TFT_WIDTH=480, TFT_HEIGHT=320 for the desired landscape UI layout.
    // LVGL software rotation maps the 480×320 logical canvas onto the physical
    // 320×480 panel; the flush callback receives a physically-correct 320×480
    // buffer so each panel scanline is exactly 320 pixels wide.
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_indev_read);

    LOGI(TAG, "DONE — JC3248W535C ready at %dx%d landscape (90° SW rotation)",
         TFT_WIDTH, TFT_HEIGHT);
}

#else // !ESP32_LVGL_ESP_DISP

void display_setup(lv_display_t *disp, lv_indev_t *indev) {
    LOGI(TAG, "DISPLAY SETUP JC3248W535C (IDF-native, AXS15231B QSPI)");

    // ── 1. SPI bus ───────────────────────────────────────────────────────
    // max_transfer_sz must be >= one full frame so the DMA can send it in one shot
    spi_bus_config_t bus_cfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        TFT_PCLK, TFT_D0, TFT_D1, TFT_D2, TFT_D3, DRAW_BUF_FULL_SIZE);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // ── 2. Panel IO (QSPI SPI mode 3, 40 MHz, cmd_bits=32) ──────────────
    // Pass on_color_trans_done + disp so the ISR can call lv_display_flush_ready()
    // once DMA is done; this replaces the unsafe synchronous call in flush.
    esp_lcd_panel_io_spi_config_t io_cfg =
        AXS15231B_PANEL_IO_QSPI_CONFIG(TFT_CS, on_color_trans_done, disp);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io_handle));

    // ── 3. Custom init commands ──────────────────────────────────────────
    // The default commands shipped with esp_lcd_axs15231b do NOT enable the
    // Tearing Effect (TE) output on the panel.  Without TE the display can
    // write new pixel data mid-frame, producing the characteristic garbled
    // 8×8-block pattern.  We supply the full vendor sequence from the Guition
    // reference BSP, including the 0x35 command that turns on TE output.

    /* data blobs must live in static storage; C++ forbids taking the address
       of a temporary (compound literal).  Each array corresponds to one
       command listed below. */
    static const uint8_t init_data_0[]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5};
    static const uint8_t init_data_1[]  = {0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t init_data_2[]  = {0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xf9, 0x10, 0x02, 0xff, 0xff, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A};
    static const uint8_t init_data_3[]  = {0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12};
    static const uint8_t init_data_4[]  = {0xA0, 0x06, 0xAa, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55};
    static const uint8_t init_data_5[]  = {0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0d, 0x00, 0xFF, 0x40};
    static const uint8_t init_data_6[]  = {0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01};
    static const uint8_t init_data_7[]  = {0x00, 0x24, 0x33, 0x80, 0x00, 0xea, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50};
    static const uint8_t init_data_8[]  = {0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00};
    static const uint8_t init_data_9[]  = {0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22};
    static const uint8_t init_data_10[] = {0x50, 0x32, 0x28, 0x00, 0xa2, 0x80, 0x8f, 0x00, 0x80, 0xff, 0x07, 0x11, 0x9c, 0x67, 0xff, 0x24, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t init_data_11[] = {0x33, 0x44, 0x44, 0x01};
    static const uint8_t init_data_12[] = {0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08};
    static const uint8_t init_data_13[] = {0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00};
    static const uint8_t init_data_14[] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00};
    static const uint8_t init_data_15[] = {0x03, 0x01, 0x0b, 0x09, 0x0f, 0x0d, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F};
    static const uint8_t init_data_16[] = {0x02, 0x00, 0x0a, 0x08, 0x0e, 0x0c, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19};
    static const uint8_t init_data_17[] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
    static const uint8_t init_data_18[] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
    static const uint8_t init_data_19[] = {0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90};
    static const uint8_t init_data_20[] = {0x3B, 0x28, 0x10, 0x16, 0x0c, 0x06, 0x11, 0x28, 0x5c, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D};
    static const uint8_t init_data_21[] = {0x37, 0x28, 0x10, 0x16, 0x0b, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F};
    static const uint8_t init_data_22[] = {0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D};
    static const uint8_t init_data_23[] = {0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F};
    static const uint8_t init_data_24[] = {0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D};
    static const uint8_t init_data_25[] = {0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F};
    static const uint8_t init_data_26[] = {0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30};
    static const uint8_t init_data_27[] = {0x85, 0x85, 0x95, 0x85};
    static const uint8_t init_data_28[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t init_data_29[] = {0x00};
    static const uint8_t init_data_30[] = {0x00};
    static const uint8_t init_data_31[] = {0x00, 0x00, 0x00, 0x00};

    static const axs15231b_lcd_init_cmd_t idf_init_cmds[] = {
        {0xBB, init_data_0, 8, 0},
        {0xA0, init_data_1, 17, 0},
        {0xA2, init_data_2, 31, 0},
        {0xD0, init_data_3, 30, 0},
        {0xA3, init_data_4, 22, 0},
        {0xC1, init_data_5, 30, 0},
        {0xC3, init_data_6, 11, 0},
        {0xC4, init_data_7, 29, 0},
        {0xC5, init_data_8, 23, 0},
        {0xC6, init_data_9, 20, 0},
        {0xC7, init_data_10, 20, 0},
        {0xC9, init_data_11, 4, 0},
        {0xCF, init_data_12, 27, 0},
        {0xD5, init_data_13, 30, 0},
        {0xD6, init_data_14, 30, 0},
        {0xD7, init_data_15, 19, 0},
        {0xD8, init_data_16, 12, 0},
        {0xD9, init_data_17, 12, 0},
        {0xDD, init_data_18, 12, 0},
        {0xDF, init_data_19, 8, 0},
        {0xE0, init_data_20, 17, 0},
        {0xE1, init_data_21, 17, 0},
        {0xE2, init_data_22, 17, 0},
        {0xE3, init_data_23, 17, 0},
        {0xE4, init_data_24, 17, 0},
        {0xE5, init_data_25, 17, 0},
        {0xA4, init_data_26, 16, 0},
        {0xA4, init_data_27, 4, 0},
        {0xBB, init_data_28, 8, 0},
        {0x13, init_data_29, 0, 0},
        {0x35, init_data_30, 1, 0},  // Enable Tearing Effect output (V-blank sync)
        {0x11, init_data_29, 0, 120},
        {0x2C, init_data_31, 4, 0},
    };

    // ── 4. LCD panel ─────────────────────────────────────────────────────
    axs15231b_vendor_config_t vendor_cfg = {
        .init_cmds      = idf_init_cmds,
        .init_cmds_size = sizeof(idf_init_cmds) / sizeof(idf_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = TFT_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        // LITTLE endian: the driver sends each 16-bit pixel low-byte first over
        // the bus.  Combined with the lv_draw_sw_rgb565_swap() call in the flush
        // callback this produces the correct RGB565 byte order on the wire.
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags          = { .reset_active_high = 0 },
        .vendor_config  = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(io_handle, &panel_cfg,
                                                &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // NOTE: esp_lcd_panel_swap_xy / mirror do NOT work correctly on AXS15231B
    // (the hardware MADCTL bit has no effect; see espressif/esp-iot-solution#579).
    // Software rotation via lv_display_set_rotation() should be used instead.
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // ── 5. TE (tearing-effect) GPIO ISR ──────────────────────────────────
    // GPIO 38 outputs a pulse at the start of each V-blank.  Waiting for this
    // pulse before sending a new frame eliminates mid-frame tearing artefacts.
#if defined(TFT_TE) && TFT_TE >= 0
    te_sync_sem = xSemaphoreCreateBinary();
    if (te_sync_sem != nullptr) {
        gpio_config_t te_io = {};
        te_io.intr_type     = GPIO_INTR_NEGEDGE;  // falling edge = end of blank / start of active frame
        te_io.mode          = GPIO_MODE_INPUT;
        te_io.pull_up_en    = GPIO_PULLUP_DISABLE;
        te_io.pull_down_en  = GPIO_PULLDOWN_DISABLE;
        te_io.pin_bit_mask  = (1ULL << TFT_TE);
        if (gpio_config(&te_io) == ESP_OK) {
            esp_err_t isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
            if (isr_err == ESP_OK) {
                te_isr_by_us = true;
            } else if (isr_err != ESP_ERR_INVALID_STATE) {
                LOGW(TAG, "gpio_install_isr_service: %s", esp_err_to_name(isr_err));
                vSemaphoreDelete(te_sync_sem);
                te_sync_sem = nullptr;
                goto skip_te;
            }
            if (gpio_isr_handler_add((gpio_num_t)TFT_TE, te_isr_handler,
                                     (void *)te_sync_sem) == ESP_OK) {
                te_isr_installed = true;
                LOGI(TAG, "TE sync enabled on GPIO %d", TFT_TE);
            } else {
                LOGW(TAG, "gpio_isr_handler_add failed – continuing without TE sync");
                if (te_isr_by_us) { gpio_uninstall_isr_service(); te_isr_by_us = false; }
                vSemaphoreDelete(te_sync_sem);
                te_sync_sem = nullptr;
            }
        } else {
            LOGW(TAG, "TE GPIO config failed – continuing without TE sync");
            vSemaphoreDelete(te_sync_sem);
            te_sync_sem = nullptr;
        }
    }
skip_te:;
#else
    LOGI(TAG, "TFT_TE not defined – skipping TE sync");
#endif

    // ── 6. Backlight (LEDC PWM on GPIO1) ────────────────────────────────
    ledc_timer_config_t ledc_timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer_cfg));

    ledc_channel_config_t ledc_ch_cfg = {
        .gpio_num   = TFT_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 255,   // full brightness
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_ch_cfg));

    // ── 7. DMA transfer-done semaphore ─────────────────────────────────────
    // Signals from on_color_trans_done (DMA ISR) to display_flush (task)
    // that a bounce-buffer chunk has been fully transmitted.
    trans_done_sem = xSemaphoreCreateCounting(1, 0);
    assert(trans_done_sem && "trans_done_sem creation failed");

#if defined(JC3248W535C_TOUCH_DRV)
    if(!touch.begin()) {
        LOGE("Failed to initialize touch module!");
        return;
    }
#elif !defined(JC3248W535C_ESP_DISP_TOUCH)
    // ── 8. I2C for touch (I2C_NUM_0, SCL=8, SDA=4, addr=0x3B) ──────────
    // manual configuration used when panel driver touch is disabled
    i2c_config_t i2c_cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TOUCH_SDA,
        .scl_io_num       = TOUCH_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed    = I2C_TOUCH_FREQUENCY,
        },
    };
    ESP_ERROR_CHECK(i2c_param_config((i2c_port_t)I2C_TOUCH_PORT, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install((i2c_port_t)I2C_TOUCH_PORT,
                                       I2C_MODE_MASTER, 0, 0, 0));
#else
    // ── 8. Touch driver setup when JC3248W535C_ESP_DISP_TOUCH is enabled
    {
        esp_panel::drivers::BusI2C::Config i2c_cfg = {
            .host_id = (i2c_port_t)I2C_TOUCH_PORT,
            .host = esp_panel::drivers::BusI2C::HostPartialConfig{
                .sda_io_num = TOUCH_SDA,
                .scl_io_num = TOUCH_SCL,
                .sda_pullup_en = true,
                .scl_pullup_en = true,
                .clk_speed = I2C_TOUCH_FREQUENCY,
            },
            .control_panel = ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(AXS15231B),
        };
        esp_panel::drivers::BusFactory::Config touch_bus_cfg(i2c_cfg);

        esp_panel::drivers::TouchAXS15231B::Config touch_cfg;
        touch_cfg.device = esp_panel::drivers::Touch::DevicePartialConfig{
            .x_max = 480,
            .y_max = 320,
            .rst_gpio_num = -1,
            .int_gpio_num = -1,
        };

        _touch = std::make_shared<esp_panel::drivers::TouchAXS15231B>(
            touch_bus_cfg, touch_cfg);
        if (_touch && _touch->init() && _touch->begin()) {
            _touch->swapXY(true);
        } else {
            LOGW(TAG, "Touch factory disabled; creating I2C bus manually");
            esp_panel::drivers::BusI2C::ControlPanelFullConfig cp_cfg =
                ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(AXS15231B);
            auto i2c_bus = std::make_shared<esp_panel::drivers::BusI2C>(
                TOUCH_SCL, TOUCH_SDA, cp_cfg);
            ESP_ERROR_CHECK(i2c_bus->init() ? ESP_OK : ESP_FAIL);
            ESP_ERROR_CHECK(i2c_bus->begin() ? ESP_OK : ESP_FAIL);

            _touch.reset();
            _touch = std::make_shared<esp_panel::drivers::TouchAXS15231B>(
                i2c_bus.get(), touch_cfg);
            ESP_ERROR_CHECK(_touch ? ESP_OK : ESP_FAIL);
            ESP_ERROR_CHECK(_touch->init() ? ESP_OK : ESP_FAIL);
            ESP_ERROR_CHECK(_touch->begin() ? ESP_OK : ESP_FAIL);
            _touch->swapXY(true);
        }
    }
#endif

    // ── 8. LVGL wiring ───────────────────────────────────────────────────
    // AXS15231B MUST use FULL render mode; it has no partial-update capability
    // and always writes its internal buffer from (0,0), so partial tiles would
    // all overwrite the top-left corner and produce the garbled-block pattern.
    //
    // Two full-size SPIRAM buffers (double-buffering) allow LVGL to render the
    // next frame while the previous one is being DMA'd to the panel.
#if LV_USE_LOG != 0
    lv_log_register_print_cb([](const char *buf) {
        Serial.printf("%s", buf);
        Serial.flush();
    });
#endif

    // Single PSRAM draw buffer is sufficient: flush is now fully synchronous
    // (lv_display_flush_ready is called only after ALL DMA chunks complete),
    // so LVGL cannot start rendering the next frame before the current one is
    // fully displayed. Double-buffering would waste another 307 KB of PSRAM.
    lv_display_set_flush_cb(disp, display_flush);
    lv_display_set_buffers(disp, draw_buf, NULL, DRAW_BUF_FULL_SIZE,
                           LV_DISPLAY_RENDER_MODE_FULL);
    // Physical panel: 320 (H) × 480 (V) portrait.  Build flags: TFT_WIDTH=480,
    // TFT_HEIGHT=320 (landscape UI).  90° SW rotation maps logical 480×320 →
    // physical 320×480 so the flush callback receives correctly-strided data.
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_indev_read);

    LOGI(TAG, "DONE — JC3248W535C ready at %dx%d landscape (90° SW rotation)",
         TFT_WIDTH, TFT_HEIGHT);
}

#endif // ESP32_LVGL_ESP_DISP

#if defined(LVGL_UI_MALLOC) && (LVGL_UI_MALLOC == lvgl_ui_spiram_malloc)

void *lvgl_ui_spiram_malloc(size_t size) {
    // check if SPIRAM is enabled and allocate on SPIRAM if allocatable
#if defined(ESP32_HW) && defined(BOARD_HAS_PSRAM)
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    // try allocating in internal memory
    return malloc(size);
#endif
}

#endif

#endif  // JC3248W535C

