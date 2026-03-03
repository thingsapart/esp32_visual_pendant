// Driver for the Makerfabs / Guition JC3248W535C ESP32-S3 3.2" QSPI display
//
// Hardware:
//   MCU     : ESP32-S3-WROOM-1-N8R8 (240 MHz, 8 MB Flash, 8 MB OPI PSRAM)
//   Display : 3.2" IPS 320×480 (portrait native), AXS15231B via QSPI (SPI2)
//   Touch   : AXS15231B integrated touch (I2C)
//
// QSPI LCD pin mapping:
//   CS  = GPIO 45   PCLK = GPIO 47   DATA0-3 = GPIO 21/48/40/39
//   RST = NC (-1)   BL   = GPIO  1   TE      = GPIO 38
//
// I2C Touch pin mapping (I2C_NUM_0):
//   SCL = GPIO 8   SDA = GPIO 4   Address = 0x3B
//
// Physical resolution: TFT_WIDTH=320 columns, TFT_HEIGHT=480 rows (portrait native)
// Logical resolution (landscape, rotation=90°): 480 wide × 320 tall
//
// Activated by build flag: -D JC3248W535C=1
//
// Sub-module files (jc3248w535c/):
//   lcd_init.h / lcd_init.cpp   — LCD hardware init (backlight, bus, panel)
//   touch_init.h / touch_init.cpp — Touch hardware init + LVGL read callback
//   axs15231b_panel.h / .c      — IDF-native AXS15231B panel driver macros
//   axs15231b_touch.h / .cpp    — AXS15231B Arduino-style touch class
//
// ═══════════════════════════════════════════════════════════════════════════
// Rendering mode selection
// ═══════════════════════════════════════════════════════════════════════════
//
// Add exactly one of these to your build_flags (or leave all unset → FULL):
//
//   -D JC3248W535C_RENDER_FULL      (default)
//       Classic full-frame mode.  Single PSRAM buffer (307 KB).
//       Every flush rotates the full 480×320 logical frame to a physical
//       320×480 stream and DMA-s it via SRAM bounce buffers.
//       AXS15231B always writes GRAM from (0,0) so partial window commands
//       are silently ignored; this is the baseline safe mode.
//
//   -D JC3248W535C_RENDER_DIRECT
//       LVGL direct mode.  Two PSRAM buffers (~614 KB total).
//       LVGL maintains both buffers in sync with pixel-level dirty tracking
//       and alpha-blending against the previous frame.  The full-frame limitation
//       still applies — only the last flush_cb call per LVGL render cycle sends
//       data to the panel.  Benefit: cheaper alpha-blending / animations.
//
//   -D JC3248W535C_RENDER_PARTIAL
//       LVGL partial mode.  Small SRAM buffer (configurable via
//       JC3248W535C_PARTIAL_COLS, default 48 cols → 30 KB).
//       An LV_EVENT_INVALIDATE_AREA rounder expands every dirty rectangle to
//       span the full logical height, satisfying the AXS15231B full-row
//       requirement.  Only the dirty x-strip is rotated and DMA-d.
//       Most efficient mode for UIs with mostly-static areas.
//       ⚠ Requires LVGL ≥ 9.2.2 (bug #8582 fix: renderer must honour the
//         rounded area, not just the flush callback).
//
// ═══════════════════════════════════════════════════════════════════════════
// Partial mode: AXS15231B row alignment
// ═══════════════════════════════════════════════════════════════════════════
//
// The AXS15231B GRAM is 320 wide × 480 tall (portrait).  The controller
// requires updates to span complete rows: exactly 320 pixels per row.
// Sending a narrower window either does nothing or corrupts the display.
//
// With 90° CW software rotation (landscape 480×320 logical):
//   • Physical row    ↔  logical X   (area->x1 … area->x2 selects row range)
//   • Physical column ↔  logical Y   (must span 0 … TFT_HEIGHT-1 = 0…319
//                                     to cover a complete 320-pixel row)
//
// Therefore the dirty-area rounder expands LOGICAL Y to [0, TFT_HEIGHT-1].
// This is the default (JC3248W535C_ROUND_Y).
//
// If you use a different rotation angle and find the axes are swapped, add:
//   -D JC3248W535C_ROUND_X   to clamp logical X to [0, TFT_WIDTH-1] instead.

#ifdef JC3248W535C

// ── Default render mode ────────────────────────────────────────────────────
#if !defined(JC3248W535C_RENDER_FULL)    && \
    !defined(JC3248W535C_RENDER_PARTIAL) && \
    !defined(JC3248W535C_RENDER_DIRECT)
#define JC3248W535C_RENDER_FULL
#endif

// ── Partial mode: default rounder axis ────────────────────────────────────
#if defined(JC3248W535C_RENDER_PARTIAL) && \
    !defined(JC3248W535C_ROUND_X)       && \
    !defined(JC3248W535C_ROUND_Y)
#define JC3248W535C_ROUND_Y   // clamp logical Y to [0, TFT_HEIGHT-1] — default
#endif

// ── Partial mode: LVGL render buffer column count ─────────────────────────
// Determines how many logical columns LVGL renders per flush call, which
// equals the number of physical rows sent per DMA batch.
// Memory in internal SRAM: PARTIAL_COLS * TFT_HEIGHT * 2 bytes
//   32 → 20 480 B   48 → 30 720 B (default)   64 → 40 960 B
#ifndef JC3248W535C_PARTIAL_COLS
#define JC3248W535C_PARTIAL_COLS 48
#endif

// ──────────────────────────────────────────────────────────────────────────
// Includes
// ──────────────────────────────────────────────────────────────────────────
#if defined(ESP32_LVGL_ESP_DISP)
#include <Arduino.h>
#endif
#include <lvgl.h>

#include "esp_idf_version.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#error "This driver requires ESP-IDF v5.x (PlatformIO esp32 platform >= 6.x)."
#endif

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if !defined(ESP32_LVGL_ESP_DISP)
#include "freertos/semphr.h"
#endif

#include "jc3248w535c/lcd_init.h"
#include "jc3248w535c/touch_init.h"
#include "debug.h"

static const char *TAG = "JC3248W535C";

// ──────────────────────────────────────────────────────────────────────────
// Size constants
// ──────────────────────────────────────────────────────────────────────────

// Bytes per RGB565 pixel (always 2 for this board)
#define BYTES_PER_PIXEL   (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))

// Full logical-frame size (480×320 landscape = physical 320×480 portrait)
#define DRAW_BUF_FULL_SIZE  (TFT_WIDTH * TFT_HEIGHT * BYTES_PER_PIXEL)   // 307 200 B

// DMA bounce buffer size.
// Physical panel dimensions (portrait native):
//   P_width  = TFT_WIDTH  = 320   (portrait columns)
//   P_height = TFT_HEIGHT = 480   (portrait rows)
// After 90° CW software rotation the logical (landscape) layout is:
//   L_width  = TFT_HEIGHT = 480   (logical cols = buffer row stride)
//   L_height = TFT_WIDTH  = 320   (logical rows)
// TRANS_DIV controls SRAM use vs. DMA transaction count:
//   TRANS_SIZE = P_width * (P_height / TRANS_DIV) * BYTES_PER_PIXEL
//   With TRANS_DIV=40: 320 * 12 * 2 = 7 680 B each → 40 transactions/frame
//   Two alternating buffers (double-buffering): CPU rotation of chunk N+1
//   overlaps DMA of chunk N.
#define TRANS_DIV   40
#define TRANS_SIZE  (TFT_WIDTH * (TFT_HEIGHT / TRANS_DIV) * BYTES_PER_PIXEL)

// Partial-mode LVGL render buffer size (internal SRAM).
// Buffer holds PARTIAL_COLS logical columns × full logical height (TFT_WIDTH rows).
#define PARTIAL_BUF_SIZE  (JC3248W535C_PARTIAL_COLS * TFT_WIDTH * BYTES_PER_PIXEL)

// ──────────────────────────────────────────────────────────────────────────
// Draw buffers (allocated in display_alloc)
// ──────────────────────────────────────────────────────────────────────────
static uint8_t *s_draw_buf  = NULL;   // primary draw buffer (all modes)
static uint8_t *s_draw_buf2 = NULL;   // secondary buffer (DIRECT mode only)

#if defined(JC3248W535C_RENDER_DIRECT)
// lv_display_set_buffers() always reads lv_display_get_original_horizontal_resolution()
// which returns disp->hor_res — set at lv_display_create() and never modified by
// lv_display_set_rotation().  With TFT_WIDTH=320, TFT_HEIGHT=480 the wrapper would
// have {w=320, h=480, stride=640}.  After 90° rotation LVGL renders at logical
// 480×320.  When lv_draw_buf_goto_xy() is called for a sync_area with x1 ≥ 320 it
// returns NULL → memcpy to 0x0 → StoreProhibited.
//
// Fix: initialise the two lv_draw_buf_t wrappers manually with the LOGICAL
// (post-rotation, landscape) dimensions so the range-check always passes:
//   w = TFT_HEIGHT = 480  (logical width)
//   h = TFT_WIDTH  = 320  (logical height)
//   stride = TFT_HEIGHT * BYTES_PER_PIXEL = 960
static lv_draw_buf_t s_lv_draw_buf1;
static lv_draw_buf_t s_lv_draw_buf2;
#endif

// DMA bounce buffers — internal SRAM, DMA-capable (MALLOC_CAP_DMA | INTERNAL).
// Even in the ESP32_Display_Panel path these are needed as rotation-output
// staging buffers that _lcd->drawBitmap() reads from.
static uint8_t *s_trans_buf1 = NULL;
static uint8_t *s_trans_buf2 = NULL;

// ──────────────────────────────────────────────────────────────────────────
// IDF-native: async DMA-done semaphore + ISR callback
// ──────────────────────────────────────────────────────────────────────────
// Not needed in the ESP32_Display_Panel path (_lcd->drawBitmap is synchronous).

#if !defined(ESP32_LVGL_ESP_DISP)

static SemaphoreHandle_t s_trans_done_sem = nullptr;

// Called from the SPI DMA ISR when esp_lcd_panel_draw_bitmap() finishes.
// Signals s_trans_done_sem so the flush loop can send the next chunk.
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t /*io*/,
                                           esp_lcd_panel_io_event_data_t * /*edata*/,
                                           void * /*user_ctx*/)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
    return false;
}

#endif  // !ESP32_LVGL_ESP_DISP

// ══════════════════════════════════════════════════════════════════════════
// rotate_and_dma_range — 90° CW rotation + DMA transfer helper
// ══════════════════════════════════════════════════════════════════════════
//
// Reads a strip of the logical pixel buffer (PSRAM or SRAM), applies the
// 90° CW rotation and RGB565 byte-swap, then streams it to the panel in
// TRANS_SIZE-byte chunks via the DMA bounce buffers.
//
// Physical panel pixel mapping (portrait 320×480, from logical 480×320):
//   physical(column C, row R) = logical(x=R, y=TFT_WIDTH-1-C)
//   with C ∈ [0, TFT_WIDTH=320),  R ∈ [start_row, end_row)
//
// Parameters
//   px_map          pointer to the LVGL logical buffer (row-major)
//   logical_stride  pixels per logical row in px_map
//                     FULL / DIRECT : TFT_HEIGHT (= 480, landscape buffer width)
//                     PARTIAL       : area_width (= area->x2 - area->x1 + 1)
//   col_offset      logical X offset of the first pixel in px_map
//                     FULL / DIRECT : 0
//                     PARTIAL       : area->x1
//   start_row       first physical portrait row to send (inclusive)
//   end_row         one past the last physical portrait row (= TFT_HEIGHT for full frame)
//
// PSRAM cache-friendly access pattern:
//   Outer loop: physical column C (0..TFT_WIDTH-1) → selects logical row (TFT_WIDTH-1-C)
//   Inner loop: physical row    R → reads pm[(TFT_WIDTH-1-C)*stride + R] sequentially ✓
//
// The caller must prime s_trans_done_sem with one Give before calling this
// function in the IDF-native path (so the very first Take in the loop succeeds
// immediately, enabling overlap between rotation and DMA for all chunks).
// This function waits for the last chunk's DMA to complete before returning.

static void rotate_and_dma_range(const uint8_t *px_map,
                                  int logical_stride,
                                  int col_offset,
                                  int start_row,
                                  int end_row)
{
    // Physical portrait column count = TFT_WIDTH = 320
    // (= number of landscape rows after rotation)
    constexpr int P_width  = TFT_WIDTH;
    // Logical landscape row count = TFT_WIDTH = 320
    // (= physical portrait column count)
    constexpr int L_height = TFT_WIDTH;

    const int rows_per_chunk = TRANS_SIZE / (P_width * BYTES_PER_PIXEL);

    for (int R = start_row; R < end_row; R += rows_per_chunk) {
        const int chunk = (R + rows_per_chunk <= end_row)
                          ? rows_per_chunk : (end_row - R);

        // Alternate between the two bounce buffers for double-buffering.
        uint8_t  *tbuf     = ((R / rows_per_chunk) & 1) ? s_trans_buf2 : s_trans_buf1;
        uint16_t *out      = (uint16_t *)tbuf;
        const uint16_t *pm = (const uint16_t *)px_map;

        // 90° CW rotation + RGB565 byte-swap.
        // Inner loop reads pm[(L_height-1-C)*logical_stride + col_offset + R - col_offset + r]
        // = pm[(L_height-1-C)*logical_stride + R + r - col_offset]
        // As R is fixed per outer iteration and r increments, this is sequential. ✓
        for (int C = 0; C < P_width; C++) {
            const uint16_t *src = pm
                                  + (L_height - 1 - C) * logical_stride
                                  + (R - col_offset);
            for (int r = 0; r < chunk; r++) {
                const uint16_t p = src[r];
                out[r * P_width + C] = (uint16_t)((p >> 8) | (p << 8));
            }
        }

#if defined(ESP32_LVGL_ESP_DISP)
        // Synchronous: _lcd->drawBitmap() blocks until the transfer completes.
        lcd_draw_bitmap(0, R, P_width, R + chunk, tbuf);
#else
        // Async: wait for the previous chunk's DMA to finish before we hand
        // the bounce buffer to the GDMA for this chunk.
        xSemaphoreTake(s_trans_done_sem, portMAX_DELAY);
        lcd_draw_bitmap(0, R, P_width, R + chunk, tbuf);
#endif
    }

#if !defined(ESP32_LVGL_ESP_DISP)
    // Wait for the final chunk's DMA before returning (so s_draw_buf is safe
    // for the next LVGL render cycle).
    xSemaphoreTake(s_trans_done_sem, portMAX_DELAY);
#endif
}

// ══════════════════════════════════════════════════════════════════════════
// RENDER MODE: FULL
// ══════════════════════════════════════════════════════════════════════════
//
// Single PSRAM buffer, LV_DISPLAY_RENDER_MODE_FULL.
// LVGL renders the whole 480×320 logical frame then calls this once.
// The full frame is rotated to physical 320×480 and streamed via DMA.
#if defined(JC3248W535C_RENDER_FULL)

static void display_flush_full(lv_display_t *disp,
                                const lv_area_t * /*area*/,
                                uint8_t *px_map)
{
    lcd_vsync_wait();   // sync to TE V-blank before the first chunk

#if !defined(ESP32_LVGL_ESP_DISP)
    xSemaphoreGive(s_trans_done_sem);   // prime: first Take in loop succeeds immediately
#endif

    // Full frame:
    //   logical_stride = TFT_HEIGHT = 480  (landscape buffer row width)
    //   end_row        = TFT_HEIGHT = 480  (physical portrait row count)
    rotate_and_dma_range(px_map, TFT_HEIGHT, 0, 0, TFT_HEIGHT);

    lv_display_flush_ready(disp);
}

// ══════════════════════════════════════════════════════════════════════════
// RENDER MODE: DIRECT
// ══════════════════════════════════════════════════════════════════════════
//
// Two PSRAM buffers, LV_DISPLAY_RENDER_MODE_DIRECT.
// LVGL maintains both buffers in sync (pixel-level dirty tracking + alpha
// blending against the previous frame buffer).
//
// AXS15231B still requires a full-frame transfer, so intermediate flush calls
// (partial dirty regions) are acknowledged immediately without sending any data.
// Only the final flush call in each LVGL render cycle sends the full frame.
#elif defined(JC3248W535C_RENDER_DIRECT)

static void display_flush_direct(lv_display_t *disp,
                                  const lv_area_t * /*area*/,
                                  uint8_t *px_map)
{
    // Acknowledge intermediate flush calls without sending to the panel.
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    lcd_vsync_wait();

#if !defined(ESP32_LVGL_ESP_DISP)
    xSemaphoreGive(s_trans_done_sem);
#endif

    rotate_and_dma_range(px_map, TFT_HEIGHT, 0, 0, TFT_HEIGHT);

    lv_display_flush_ready(disp);
}

// ══════════════════════════════════════════════════════════════════════════
// RENDER MODE: PARTIAL
// ══════════════════════════════════════════════════════════════════════════
//
// Small internal-SRAM buffer, LV_DISPLAY_RENDER_MODE_PARTIAL.
//
// The AXS15231B requires complete physical rows (all 320 columns per row).
// We intercept LV_EVENT_INVALIDATE_AREA and force every dirty rectangle to
// span the full logical height [y1=0 … y2=TFT_HEIGHT-1=319].
//
// Mapping with 90° CW software rotation:
//   Logical X  ↔  Physical row  (area->x1..x2 selects which rows to update)
//   Logical Y  ↔  Physical col  (must span 0..319 = full physical row width)
//
// After rounding, the dirty area is always {x1, 0, x2, 319}.
// LVGL calls flush with a buffer of (x2-x1+1) * 320 pixels in logical layout.
// The flush callback rotates and streams only this x-strip.
//
// JC3248W535C_ROUND_Y (default): expand logical Y → good for 90° CW rotation.
// JC3248W535C_ROUND_X:           expand logical X → use for other rotations.
#elif defined(JC3248W535C_RENDER_PARTIAL)

// ── Dirty-area rounder ─────────────────────────────────────────────────────
static void display_rounder_cb(lv_event_t *e)
{
    lv_area_t    *area = (lv_area_t *)lv_event_get_param(e);
    lv_display_t *d    = (lv_display_t *)lv_event_get_target(e);

#if defined(JC3248W535C_ROUND_Y)
    // Clamp logical Y to [0, TFT_HEIGHT-1]: ensures full physical row width
    area->y1 = 0;
    area->y2 = lv_display_get_vertical_resolution(d) - 1;   // 319
#else  // JC3248W535C_ROUND_X
    // Clamp logical X to [0, TFT_WIDTH-1]: use when axes are opposite
    area->x1 = 0;
    area->x2 = lv_display_get_horizontal_resolution(d) - 1; // 479
#endif
}

// ── Partial flush callback ──────────────────────────────────────────────────
//
// After the rounder, area is always {x1, 0, x2, TFT_HEIGHT-1}.
// px_map has (x2-x1+1) * TFT_HEIGHT pixels in LOGICAL row-major order:
//   pixel at logical(x, y) = ((uint16_t*)px_map)[y * area_width + (x - x1)]
//
// Physical destination window: x_start=0, y_start=x1, x_end=TFT_HEIGHT, y_end=x2+1
//   physical(C, R) = logical(R, TFT_HEIGHT-1-C)
//                  = ((uint16_t*)px_map)[(TFT_HEIGHT-1-C) * area_width + (R-x1)]
//
static void display_flush_partial(lv_display_t *disp,
                                   const lv_area_t *area,
                                   uint8_t *px_map)
{
    const int area_width = area->x2 - area->x1 + 1;  // logical cols = physical rows

#if !defined(ESP32_LVGL_ESP_DISP)
    xSemaphoreGive(s_trans_done_sem);
#endif

    // logical_stride = area_width (partial buffer row stride, not TFT_WIDTH)
    // col_offset     = area->x1  (first physical row in this flush)
    rotate_and_dma_range(px_map, area_width, area->x1,
                          area->x1, area->x2 + 1);

    lv_display_flush_ready(disp);
}

#endif  // render mode selection

// ══════════════════════════════════════════════════════════════════════════
// display_alloc
// ══════════════════════════════════════════════════════════════════════════
//
// Allocates draw buffers and DMA bounce buffers.  Called early (before the
// LVGL task or any other large heap consumer) to secure contiguous blocks.

void display_alloc()
{
    extern void ram_usage();
    ram_usage();

#if defined(JC3248W535C_RENDER_FULL)

    LOGI(TAG, "alloc FULL: %u B PSRAM draw buf + 2 × %u B SRAM DMA bounce",
         (unsigned)DRAW_BUF_FULL_SIZE, (unsigned)TRANS_SIZE);

    // Full-frame LVGL draw buffer in PSRAM.
    // LVGL renders via CPU → D-cache.  DMA reads physical PSRAM directly, so
    // we never DMA from this buffer; we copy chunk-by-chunk through SRAM first.
    s_draw_buf = (uint8_t *)heap_caps_malloc(DRAW_BUF_FULL_SIZE,
                                              MALLOC_CAP_SPIRAM  | MALLOC_CAP_8BIT);
    if (!s_draw_buf) {
        LOGW(TAG, "PSRAM alloc failed – retrying in internal RAM");
        s_draw_buf = (uint8_t *)heap_caps_malloc(DRAW_BUF_FULL_SIZE,
                                                  MALLOC_CAP_INTERNAL |
                                                  MALLOC_CAP_8BIT     |
                                                  MALLOC_CAP_DMA);
    }

#elif defined(JC3248W535C_RENDER_DIRECT)

    LOGI(TAG, "alloc DIRECT: 2 × %u B PSRAM draw bufs + 2 × %u B SRAM DMA bounce",
         (unsigned)DRAW_BUF_FULL_SIZE, (unsigned)TRANS_SIZE);

    s_draw_buf = (uint8_t *)heap_caps_malloc(DRAW_BUF_FULL_SIZE,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_draw_buf2 = (uint8_t *)heap_caps_malloc(DRAW_BUF_FULL_SIZE,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_draw_buf2) {
        LOGW(TAG, "second PSRAM buf failed – falling back to internal RAM");
        s_draw_buf2 = (uint8_t *)heap_caps_malloc(DRAW_BUF_FULL_SIZE,
                                                   MALLOC_CAP_INTERNAL |
                                                   MALLOC_CAP_8BIT     |
                                                   MALLOC_CAP_DMA);
    }

#elif defined(JC3248W535C_RENDER_PARTIAL)

    LOGI(TAG, "alloc PARTIAL: %u B SRAM draw buf (%d cols) + 2 × %u B SRAM DMA bounce",
         (unsigned)PARTIAL_BUF_SIZE, JC3248W535C_PARTIAL_COLS, (unsigned)TRANS_SIZE);

    // Partial draw buffer in internal SRAM: small enough to fit without PSRAM,
    // and directly DMA-accessible (no cache-coherency workaround needed).
    s_draw_buf = (uint8_t *)heap_caps_malloc(PARTIAL_BUF_SIZE,
                                              MALLOC_CAP_INTERNAL |
                                              MALLOC_CAP_8BIT     |
                                              MALLOC_CAP_DMA);

#endif  // render mode buffer allocation

    assert(s_draw_buf && "LVGL draw buffer allocation failed");

    // DMA bounce buffers: always internal SRAM + DMA-capable.
    // Used as rotation output and DMA-source in both build paths.
    s_trans_buf1 = (uint8_t *)heap_caps_malloc(TRANS_SIZE,
                                                MALLOC_CAP_DMA      |
                                                MALLOC_CAP_INTERNAL |
                                                MALLOC_CAP_8BIT);
    s_trans_buf2 = (uint8_t *)heap_caps_malloc(TRANS_SIZE,
                                                MALLOC_CAP_DMA      |
                                                MALLOC_CAP_INTERNAL |
                                                MALLOC_CAP_8BIT);
    assert(s_trans_buf1 && s_trans_buf2 && "DMA bounce buffer allocation failed");

    ram_usage();
}

// ══════════════════════════════════════════════════════════════════════════
// display_setup
// ══════════════════════════════════════════════════════════════════════════
//
// Initialises LCD hardware, touch hardware, and wires up the LVGL display
// and input device.  Called once from the driver entry point.

void display_setup(lv_display_t *disp, lv_indev_t *indev)
{
#if defined(JC3248W535C_RENDER_FULL)
    LOGI(TAG, "DISPLAY SETUP JC3248W535C — RENDER_FULL");
#elif defined(JC3248W535C_RENDER_DIRECT)
    LOGI(TAG, "DISPLAY SETUP JC3248W535C — RENDER_DIRECT");
#elif defined(JC3248W535C_RENDER_PARTIAL)
    LOGI(TAG, "DISPLAY SETUP JC3248W535C — RENDER_PARTIAL (cols=%d)",
         JC3248W535C_PARTIAL_COLS);
#endif

    // ── 1. LCD hardware ────────────────────────────────────────────────────
#if !defined(ESP32_LVGL_ESP_DISP)
    // Create the DMA-done semaphore *before* lcd_hw_init so on_color_trans_done
    // can safely post to it from the very first draw_bitmap ISR.
    s_trans_done_sem = xSemaphoreCreateCounting(1, 0);
    assert(s_trans_done_sem && "s_trans_done_sem creation failed");
    bool ok = lcd_hw_init(on_color_trans_done, NULL);
#else
    bool ok = lcd_hw_init(nullptr, nullptr);
#endif
    if (!ok) {
        LOGE(TAG, "lcd_hw_init failed — display will not work");
        return;
    }

    // ── 2. Touch hardware ─────────────────────────────────────────────────
    if (!touch_hw_init()) {
        LOGW(TAG, "touch_hw_init failed — touch will not work");
    }

    // ── 3. LVGL: optional log sink ─────────────────────────────────────────
#if LV_USE_LOG != 0
    lv_log_register_print_cb([](const char *buf) {
        printf("%s", buf);
    });
#endif

    // ── 4. LVGL: software rotation — MUST be set before lv_display_set_buffers ──
    //
    // Physical panel: portrait 320 (H) × 480 (V).
    // ROTATION_90 swaps the logical resolution to landscape (TFT_WIDTH × TFT_HEIGHT)
    // and maps touch events accordingly.  Our flush callbacks handle the physical
    // rotation of pixel data themselves.
    //
    // WHY ROTATION FIRST:
    // lv_display_set_buffers() internally initialises lv_draw_buf_t wrappers whose
    // stride = hor_res × bpp, using the CURRENT horizontal resolution at call time.
    //
    // If rotation is set AFTER set_buffers:
    //   • At set_buffers time hor_res == TFT_WIDTH (portrait physical, e.g. 320)
    //     → stride = 320×2 = 640 bytes / row
    //   • After rotation, LVGL's logical hor_res becomes TFT_HEIGHT (e.g. 480)
    //   • In DIRECT mode, lv_draw_buf_copy() iterates 480 pixels × 2 bytes = 960
    //     bytes/row but advances the pointer only 640 bytes → overruns the buffer
    //     within a few rows → eventually writes to address 0x0 → StoreProhibited
    //
    // With rotation set FIRST:
    //   • At set_buffers time hor_res == TFT_HEIGHT (landscape logical, e.g. 480)
    //     → stride = 480×2 = 960 bytes / row
    //   • 480 × 2 × 320 rows = 307 200 B == DRAW_BUF_FULL_SIZE ✓
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    // ── 5. LVGL: flush callback + draw buffer ─────────────────────────────
    //
    // Set buffers AFTER rotation so lv_draw_buf_t stride is computed from the
    // post-rotation (landscape) horizontal resolution.

#if defined(JC3248W535C_RENDER_FULL)

    lv_display_set_flush_cb(disp, display_flush_full);
    lv_display_set_buffers(disp, s_draw_buf, NULL,
                           DRAW_BUF_FULL_SIZE,
                           LV_DISPLAY_RENDER_MODE_FULL);

#elif defined(JC3248W535C_RENDER_DIRECT)

    lv_display_set_flush_cb(disp, display_flush_direct);

    // Use logical (landscape, post-rotation) dimensions for the draw_buf headers.
    // See the comment above s_lv_draw_buf1/2 for the full explanation.
    constexpr uint32_t L_W      = TFT_HEIGHT;                    // 480 logical columns
    constexpr uint32_t L_H      = TFT_WIDTH;                     // 320 logical rows
    constexpr uint32_t L_STRIDE = TFT_HEIGHT * BYTES_PER_PIXEL;  // 960 bytes / row
    lv_draw_buf_init(&s_lv_draw_buf1, L_W, L_H,
                     LV_COLOR_FORMAT_RGB565, L_STRIDE,
                     s_draw_buf, DRAW_BUF_FULL_SIZE);
    lv_draw_buf_init(&s_lv_draw_buf2, L_W, L_H,
                     LV_COLOR_FORMAT_RGB565, L_STRIDE,
                     s_draw_buf2, DRAW_BUF_FULL_SIZE);
    lv_display_set_draw_buffers(disp, &s_lv_draw_buf1, &s_lv_draw_buf2);
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_DIRECT);

#elif defined(JC3248W535C_RENDER_PARTIAL)

    lv_display_set_flush_cb(disp, display_flush_partial);
    lv_display_set_buffers(disp, s_draw_buf, NULL,
                           PARTIAL_BUF_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Register the row-alignment rounder.
    // Fires before each render pass; expands dirty rectangles to full height
    // (= full physical row width) to satisfy the AXS15231B constraint.
    lv_display_add_event_cb(disp, display_rounder_cb,
                             LV_EVENT_INVALIDATE_AREA, NULL);

#if defined(JC3248W535C_ROUND_Y)
    LOGI(TAG, "partial rounder: logical Y → [0, %d]", TFT_HEIGHT - 1);
#else
    LOGI(TAG, "partial rounder: logical X → [0, %d]", TFT_WIDTH - 1);
#endif

#endif  // render mode LVGL wiring

    // ── 5. LVGL: touch input device ────────────────────────────────────────
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_indev_read);

    LOGI(TAG, "DONE — JC3248W535C ready at %d×%d landscape (90° SW rotation)",
         TFT_WIDTH, TFT_HEIGHT);
}

// ── Optional LVGL custom allocator — routes to PSRAM ──────────────────────
#if defined(LVGL_UI_MALLOC) && (LVGL_UI_MALLOC == lvgl_ui_spiram_malloc)

void *lvgl_ui_spiram_malloc(size_t size)
{
#if defined(ESP32_HW) && defined(BOARD_HAS_PSRAM)
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

#endif  // LVGL_UI_MALLOC

#endif  // JC3248W535C

