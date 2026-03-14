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
// Build flags: TFT_WIDTH=480 (landscape width), TFT_HEIGHT=320 (landscape height).
// Physical panel: 320 columns × 480 rows (portrait native).
// The 90° CW pixel rotation is applied entirely inside the flush callbacks;
// LVGL itself has no knowledge of it and always sees landscape 480×320.
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

// ═══════════════════════════════════════════════════════════════════════════
// Rotation angle selection
// ═══════════════════════════════════════════════════════════════════════════
//
// Add exactly one of these to your build_flags (or leave all unset → 90° CW):
//
//   -D JC3248W535C_ROT_90_CW   (default)
//       Landscape top-edge appears at the right side of the physical portrait.
//       Original / default orientation.
//
//   -D JC3248W535C_ROT_90_CCW  (also accepted as -D JC3248W535C_ROT_270_CW)
//       Landscape top-edge appears at the left side of the physical portrait.
//       Use when the board is mounted 180° from the default orientation.
//       physical(C, R) = logical(x = TFT_WIDTH-1-R, y = C)
//
//   -D JC3248W535C_ROT_180
//       Landscape image is rotated 180°: both X and Y axes are reversed.
//       physical(C, R) = logical(x = TFT_WIDTH-1-R, y = TFT_HEIGHT-1-C)
//
// The dirty-area rounder (ROUND_Y) is correct for all three modes because
// in every case physical column C maps to a logical Y dimension that must
// span [0, TFT_HEIGHT-1] to satisfy the AXS15231B full-row requirement.

// ═══════════════════════════════════════════════════════════════════════════
// V-sync / tearing-effect selection
// ═══════════════════════════════════════════════════════════════════════════
//
// By default, each flush waits for the panel's TE (tearing-effect) V-blank
// pulse before starting DMA so display updates are tear-free.  The wait
// has a freshness check: if a TE pulse arrived < 8 ms ago the flush starts
// immediately (0 overhead); if it arrived mid-previous-DMA the code waits
// up to ~16 ms for the next one.
//
//   -D JC3248W535C_NO_VSYNC
//       Skip the TE wait entirely.  The flush starts immediately after
//       rotation, at the cost of possible horizontal tearing.  Eliminates
//       the 0-16 ms TE latency — useful for CNC pendants where scan-line
//       tearing is imperceptible and latency matters more.
//       Note: TE GPIO ISR and the 0x35 init command remain active; only
//       the wait call is bypassed so re-enabling is a single flag change.

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

// ── Allow 270° CW as an alias for 90° CCW ─────────────────────────────────
#if defined(JC3248W535C_ROT_270_CW) && !defined(JC3248W535C_ROT_90_CCW)
#define JC3248W535C_ROT_90_CCW
#endif

// ── Default rotation ───────────────────────────────────────────────────────
#if !defined(JC3248W535C_ROT_90_CW)  && \
    !defined(JC3248W535C_ROT_90_CCW) && \
    !defined(JC3248W535C_ROT_180)
#define JC3248W535C_ROT_90_CW
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
#include "perf_trace.h"

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
//   P_width  = TFT_HEIGHT = 320   (portrait columns  = landscape rows)
//   P_height = TFT_WIDTH  = 480   (portrait rows     = landscape cols)
// TRANS_DIV controls SRAM use vs. DMA transaction count:
//   TRANS_SIZE = P_width * (P_height / TRANS_DIV) * BYTES_PER_PIXEL
//   With TRANS_DIV=40: 320 * 12 * 2 = 7 680 B each → 40 transactions/frame
//   Two alternating buffers (double-buffering): CPU rotation of chunk N+1
//   overlaps DMA of chunk N.
#define TRANS_DIV   10
#define TRANS_SIZE  (TFT_HEIGHT * (TFT_WIDTH / TRANS_DIV) * BYTES_PER_PIXEL)

// Partial-mode LVGL render buffer.
// This must be a FULL-FRAME buffer (TFT_WIDTH × TFT_HEIGHT).
//
// Why full-frame is required:
//   get_max_row() probes the rounder with LV_EVENT_INVALIDATE_AREA to find how
//   many rows fit per pass.  Our ROUND_Y rounder always expands the probe to
//   full logical height (TFT_HEIGHT = 320 rows).  For the probe to pass the
//   condition (height ≤ max_row), max_row must be ≥ 320.
//   max_row = buf_size / (dirty_area_width × BYTES_PER_PIXEL)
//   → buf_size ≥ TFT_WIDTH × TFT_HEIGHT × BYTES_PER_PIXEL for all widths ≤ TFT_WIDTH.
//
// PARTIAL mode still saves CPU vs FULL mode: LVGL only re-renders dirty areas
// and the flush callback rotates/DMAs only the dirty column strip.
// JC3248W535C_PARTIAL_COLS no longer affects buffer sizing (LVGL controls
// the dirty area width organically from UI invalidation).
#define PARTIAL_BUF_SIZE  DRAW_BUF_FULL_SIZE

// ──────────────────────────────────────────────────────────────────────────
// Draw buffers (allocated in display_alloc)
// ──────────────────────────────────────────────────────────────────────────
static uint8_t *s_draw_buf  = NULL;   // primary draw buffer (all modes)
static uint8_t *s_draw_buf2 = NULL;   // secondary buffer (DIRECT mode only)

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

// Maximum time to wait for a single DMA chunk to complete.
// A full-width 320-pixel row chunk over QSPI at 80 MHz takes < 5 ms;
// 500 ms is a generous ceiling.  If we never get the ISR within this window
// the SPI/GDMA subsystem is wedged and we bail out to avoid a TWDT panic.
#define DMA_SEM_TIMEOUT_MS 500

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
// rotate_and_dma_range — rotation + DMA transfer helper
// ══════════════════════════════════════════════════════════════════════════
//
// Reads a strip of the logical pixel buffer (PSRAM or SRAM), applies the
// selected rotation and RGB565 byte-swap, then streams it to the panel in
// TRANS_SIZE-byte chunks via the DMA bounce buffers.
//
// Rotation pixel mappings (portrait 320×480 physical, logical 480×320):
//   90° CW  (default): physical(C, R) = logical(x=R,           y=TFT_HEIGHT-1-C)
//   90° CCW           : physical(C, R) = logical(x=TFT_WIDTH-1-R, y=C)
//   180°              : physical(C, R) = logical(x=TFT_WIDTH-1-R, y=TFT_HEIGHT-1-C)
//   with C ∈ [0, TFT_HEIGHT=320),  R ∈ [start_row, end_row)
//
// Parameters
//   px_map          pointer to the LVGL logical buffer (row-major)
//   logical_stride  pixels per logical row in px_map
//                     FULL / DIRECT : TFT_WIDTH (= 480, landscape buffer width)
//                     PARTIAL       : TFT_WIDTH  (full-frame PSRAM buffer)
//   col_offset      logical X offset of the first pixel in px_map
//                     FULL / DIRECT : 0
//                     PARTIAL       : area->x1
//   start_row       first physical portrait row to send (inclusive)
//     90° CW  full  : 0          partial: area->x1
//     90° CCW full  : 0          partial: TFT_WIDTH-1-area->x2
//     180°    full  : 0          partial: TFT_WIDTH-1-area->x2
//   end_row         one past the last physical portrait row
//     90° CW  full  : TFT_WIDTH  partial: area->x2+1
//     90° CCW full  : TFT_WIDTH  partial: TFT_WIDTH-area->x1
//     180°    full  : TFT_WIDTH  partial: TFT_WIDTH-area->x1
//
// PSRAM cache-friendly access pattern:
//   90° CW / 180°: outer loop C selects a logical row — inner loop reads
//                  it sequentially (forward for CW, backward for 180°). ✓
//   90° CCW      : outer loop C selects a logical row (row=C) — inner loop
//                  reads it in reverse (still within one cache line). ✓
//
// The caller must prime s_trans_done_sem with one Give before calling this
// function in the IDF-native path (so the very first Take in the loop succeeds
// immediately, enabling overlap between rotation and DMA for all chunks).
// This function waits for the last chunk's DMA to complete before returning.

// Returns true on success, false if a DMA timeout occurred (frame dropped).
static bool IRAM_ATTR rotate_and_dma_range(const uint8_t *px_map,
                                  int logical_stride,
                                  int col_offset,
                                  int start_row,
                                  int end_row)
{
    // Physical portrait column count = TFT_WIDTH = 320
    // (= number of logical landscape rows)
    constexpr int P_width  = TFT_HEIGHT;
    // Logical landscape row count = TFT_HEIGHT = 320
    // (= physical portrait column count)
    constexpr int L_height = TFT_HEIGHT;

    const int rows_per_chunk = TRANS_SIZE / (P_width * BYTES_PER_PIXEL);

    PERF_BEGIN(rotate_dma_total);
    int chunk_index = 0;
    for (int R = start_row; R < end_row; R += rows_per_chunk) {
        const int chunk = (R + rows_per_chunk <= end_row)
                          ? rows_per_chunk : (end_row - R);

        // Alternate between the two bounce buffers for double-buffering.
        uint8_t  *tbuf     = ((R / rows_per_chunk) & 1) ? s_trans_buf2 : s_trans_buf1;
        (void)chunk_index;
        uint16_t *out      = (uint16_t *)tbuf;
        const uint16_t *pm = (const uint16_t *)px_map;

        // Rotation + RGB565 byte-swap — C-loop unrolled ×4.
        //
        // Unrolling processes 4 adjacent physical columns per outer step, so
        // the inner r-loop writes 4 consecutive uint16_t (two uint32_t stores)
        // instead of a single uint16_t every 640 bytes.  This converts the
        // original scattered 640-byte-stride SRAM writes into near-sequential
        // burst writes and greatly improves DMA-SRAM write-cache utilisation.
        //
        // P_width = TFT_HEIGHT = 320, always divisible by 4.  The alignment
        // of (out + r*P_width + C) is guaranteed 4-byte: out is heap_caps
        // 32-bit aligned, r*320 is always even, C is a multiple of 4.
        //
        // __builtin_bswap16 → single REV16/BYTEREV on GCC/Xtensa LX7.
        // uint32_t pack: (bswap(p0) | bswap(p1)<<16) stores bytes
        // [p0_MSB, p0_LSB, p1_MSB, p1_LSB] in little-endian memory —
        // exactly the big-endian order the AXS15231B expects over SPI. ✓
#if defined(JC3248W535C_ROT_90_CW)
        // 90° CW: physical(C, R+r) = logical(x=R+r, y=TFT_HEIGHT-1-C)
        // Sequential forward read along logical row (TFT_HEIGHT-1-C). ✓
        for (int C = 0; C < P_width; C += 4) {
            const uint16_t *s0 = pm + (L_height - 1 -  C   ) * logical_stride + (R - col_offset);
            const uint16_t *s1 = pm + (L_height - 1 - (C+1)) * logical_stride + (R - col_offset);
            const uint16_t *s2 = pm + (L_height - 1 - (C+2)) * logical_stride + (R - col_offset);
            const uint16_t *s3 = pm + (L_height - 1 - (C+3)) * logical_stride + (R - col_offset);
            for (int r = 0; r < chunk; r++) {
                uint32_t *d = (uint32_t *)(out + r * P_width + C);
                d[0] = (uint32_t)__builtin_bswap16(s0[r]) | ((uint32_t)__builtin_bswap16(s1[r]) << 16);
                d[1] = (uint32_t)__builtin_bswap16(s2[r]) | ((uint32_t)__builtin_bswap16(s3[r]) << 16);
            }
        }
#elif defined(JC3248W535C_ROT_90_CCW)
        // 90° CCW: physical(C, R+r) = logical(x=TFT_WIDTH-1-(R+r), y=C)
        // Reverse read along logical row C (still sequential within the row). ✓
        {
            const int base_col = TFT_WIDTH - 1 - R - col_offset;
            for (int C = 0; C < P_width; C += 4) {
                const uint16_t *s0 = pm +  C    * logical_stride + base_col;
                const uint16_t *s1 = pm + (C+1) * logical_stride + base_col;
                const uint16_t *s2 = pm + (C+2) * logical_stride + base_col;
                const uint16_t *s3 = pm + (C+3) * logical_stride + base_col;
                for (int r = 0; r < chunk; r++) {
                    uint32_t *d = (uint32_t *)(out + r * P_width + C);
                    d[0] = (uint32_t)__builtin_bswap16(s0[-r]) | ((uint32_t)__builtin_bswap16(s1[-r]) << 16);
                    d[1] = (uint32_t)__builtin_bswap16(s2[-r]) | ((uint32_t)__builtin_bswap16(s3[-r]) << 16);
                }
            }
        }
#elif defined(JC3248W535C_ROT_180)
        // 180°: physical(C, R+r) = logical(x=TFT_WIDTH-1-(R+r), y=TFT_HEIGHT-1-C)
        // Reverse read along logical row (TFT_HEIGHT-1-C). ✓
        {
            const int base_col = TFT_WIDTH - 1 - R - col_offset;
            for (int C = 0; C < P_width; C += 4) {
                const uint16_t *s0 = pm + (L_height - 1 -  C   ) * logical_stride + base_col;
                const uint16_t *s1 = pm + (L_height - 1 - (C+1)) * logical_stride + base_col;
                const uint16_t *s2 = pm + (L_height - 1 - (C+2)) * logical_stride + base_col;
                const uint16_t *s3 = pm + (L_height - 1 - (C+3)) * logical_stride + base_col;
                for (int r = 0; r < chunk; r++) {
                    uint32_t *d = (uint32_t *)(out + r * P_width + C);
                    d[0] = (uint32_t)__builtin_bswap16(s0[-r]) | ((uint32_t)__builtin_bswap16(s1[-r]) << 16);
                    d[1] = (uint32_t)__builtin_bswap16(s2[-r]) | ((uint32_t)__builtin_bswap16(s3[-r]) << 16);
                }
            }
        }
#endif

#if defined(ESP32_LVGL_ESP_DISP)
        // Synchronous: _lcd->drawBitmap() blocks until the transfer completes.
        PERF_BEGIN(draw_sync);
        lcd_draw_bitmap(0, R, P_width, R + chunk, tbuf);
        PERF_SLOWLOG(TAG, draw_sync, 15000);  // synchronous draw >15 ms is slow
#else
        // Async: wait for the previous chunk's DMA to finish before we hand
        // the bounce buffer to the GDMA for this chunk.
        PERF_BEGIN(dma_chunk_wait);
        if (xSemaphoreTake(s_trans_done_sem, pdMS_TO_TICKS(DMA_SEM_TIMEOUT_MS)) != pdTRUE) {
            esp_rom_printf("[jc3248w535c] DMA timeout waiting for chunk R=%d (chunk_index=%d) — frame dropped\n",
                           R, chunk_index);
            PERF_SLOWLOG(TAG, rotate_dma_total, 0);  // always log how far we got
            return false;
        }
        PERF_SLOWLOG(TAG, dma_chunk_wait, 5000);  // DMA sem wait >5 ms is abnormal
        lcd_draw_bitmap(0, R, P_width, R + chunk, tbuf);
#endif
        chunk_index++;
    }
    PERF_SLOWLOG(TAG, rotate_dma_total, 60000);  // full frame rotate+DMA >60 ms

#if !defined(ESP32_LVGL_ESP_DISP)
    // Wait for the final chunk's DMA before returning (so s_draw_buf is safe
    // for the next LVGL render cycle).
    PERF_BEGIN(final_dma_wait);
    if (xSemaphoreTake(s_trans_done_sem, pdMS_TO_TICKS(DMA_SEM_TIMEOUT_MS)) != pdTRUE) {
        esp_rom_printf("[jc3248w535c] DMA timeout waiting for final chunk (chunk_index=%d) — frame dropped\n",
                       chunk_index);
        return false;
    }
    PERF_SLOWLOG(TAG, final_dma_wait, 5000);
#endif
    return true;
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
    PERF_BEGIN(flush_full_total);

    // Wait for TE (tearing-effect) V-blank signal.  Times out after 20 ms if
    // the TE GPIO interrupt is not firing.  Skip with -D JC3248W535C_NO_VSYNC
    // to eliminate the 0–16 ms TE latency at the cost of possible tearing.
#if !defined(JC3248W535C_NO_VSYNC)
    PERF_BEGIN(vsync);
    lcd_vsync_wait();
    PERF_SLOWLOG(TAG, vsync, 21000);  // >21 ms means a TE pulse was missed
#endif

#if !defined(ESP32_LVGL_ESP_DISP)
    xSemaphoreGive(s_trans_done_sem);   // prime: first Take in loop succeeds immediately
#endif

    // Full frame:
    //   logical_stride = TFT_WIDTH = 480  (landscape buffer row width)
    //   end_row        = TFT_WIDTH = 480  (physical portrait row count)
    PERF_BEGIN(rotate_dma);
    rotate_and_dma_range(px_map, TFT_WIDTH, 0, 0, TFT_WIDTH);  // errors logged internally
    PERF_SLOWLOG(TAG, rotate_dma, 60000);

    PERF_SLOWLOG(TAG, flush_full_total, 85000);  // total flush >85 ms is alarming

    lv_display_flush_ready(disp);  // always unblock LVGL, even if DMA timed out
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

    PERF_BEGIN(flush_direct_total);

    // Wait for TE V-blank.  Missing a pulse here adds up to 20 ms of latency.
    // Skip with -D JC3248W535C_NO_VSYNC to trade tear-free for lower latency.
#if !defined(JC3248W535C_NO_VSYNC)
    PERF_BEGIN(vsync);
    lcd_vsync_wait();
    PERF_SLOWLOG(TAG, vsync, 21000);  // >21 ms means a TE pulse was missed
#endif

#if !defined(ESP32_LVGL_ESP_DISP)
    xSemaphoreGive(s_trans_done_sem);
#endif

    PERF_BEGIN(rotate_dma);
    rotate_and_dma_range(px_map, TFT_WIDTH, 0, 0, TFT_WIDTH);  // errors logged internally
    PERF_SLOWLOG(TAG, rotate_dma, 60000);

    PERF_SLOWLOG(TAG, flush_direct_total, 85000);  // total flush >85 ms is alarming

    lv_display_flush_ready(disp);  // always unblock LVGL, even if DMA timed out
}
// ══════════════════════════════════════════════════════════════════════════
// RENDER MODE: PARTIAL
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
// In partial mode lv_refr sets layer->buf_area = sub_area, so the layer's
// coordinate origin is (x1, 0).  LVGL stores display pixel (x, y) at:
//   buf[(y - 0) * stride + (x - x1)]  =  buf[y * TFT_WIDTH + (x - x1)]
//
// Physical buffer destination and pixel read index per rotation:
//
//   90° CW : physical(C, R) = logical(R,           TFT_HEIGHT-1-C)
//                            = buf[(TFT_HEIGHT-1-C)*TFT_WIDTH + (R - x1)]
//            DMA rows: [x1, x2]  (same order as logical X)
//
//   90° CCW: physical(C, R) = logical(TFT_WIDTH-1-R, C)
//                            = buf[C*TFT_WIDTH + (TFT_WIDTH-1-R - x1)]
//            DMA rows: [TFT_WIDTH-1-x2, TFT_WIDTH-1-x1]  (reversed)
//
//   180°   : physical(C, R) = logical(TFT_WIDTH-1-R, TFT_HEIGHT-1-C)
//                            = buf[(TFT_HEIGHT-1-C)*TFT_WIDTH + (TFT_WIDTH-1-R - x1)]
//            DMA rows: [TFT_WIDTH-1-x2, TFT_WIDTH-1-x1]  (reversed)
//
// → logical_stride = TFT_WIDTH, col_offset = area->x1 for all modes.
static void display_flush_partial(lv_display_t *disp,
                                   const lv_area_t *area,
                                   uint8_t *px_map)
{
    PERF_BEGIN(flush_partial_total);

#if !defined(ESP32_LVGL_ESP_DISP)
    xSemaphoreGive(s_trans_done_sem);
#endif

    // stride = TFT_WIDTH (full row), col_offset = area->x1 (layer x-origin).
    // For 90° CW, logical X and physical rows share the same order.
    // For 90° CCW and 180°, the X→row mapping is reversed.
    PERF_BEGIN(rotate_dma_partial);
#if defined(JC3248W535C_ROT_90_CW)
    rotate_and_dma_range(px_map, TFT_WIDTH, area->x1,
                          area->x1, area->x2 + 1);
#else   // JC3248W535C_ROT_90_CCW or JC3248W535C_ROT_180
    rotate_and_dma_range(px_map, TFT_WIDTH, area->x1,
                          TFT_WIDTH - 1 - area->x2,
                          TFT_WIDTH     - area->x1);
#endif
    PERF_SLOWLOG(TAG, rotate_dma_partial, 30000);
    PERF_SLOWLOG(TAG, flush_partial_total, 35000);

    // errors logged inside rotate_and_dma_range; always unblock LVGL
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

    LOGI(TAG, "alloc PARTIAL: %u B PSRAM draw buf + 2 × %u B SRAM DMA bounce",
         (unsigned)PARTIAL_BUF_SIZE, (unsigned)TRANS_SIZE);

    // Full-frame PSRAM buffer (same size as FULL mode).
    // LVGL renders dirty areas into it; the flush callback DMAs only the
    // dirty column strip, so CPU and DMA work scale with dirty area size.
    s_draw_buf = (uint8_t *)heap_caps_malloc(PARTIAL_BUF_SIZE,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

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

    // ── 4. LVGL: flush callback + draw buffer ─────────────────────────────
    //
    // TFT_WIDTH=480, TFT_HEIGHT=320 match the logical landscape resolution
    // that lv_display_create() is called with, so lv_display_set_buffers()
    // computes the correct stride (480×2 = 960 bytes/row) without any LVGL
    // rotation trick.  The 90° CW pixel rotation lives entirely in the flush
    // callbacks; LVGL itself has no knowledge of it.

#if defined(JC3248W535C_RENDER_FULL)

    lv_display_set_flush_cb(disp, display_flush_full);
    lv_display_set_buffers(disp, s_draw_buf, NULL,
                           DRAW_BUF_FULL_SIZE,
                           LV_DISPLAY_RENDER_MODE_FULL);

#elif defined(JC3248W535C_RENDER_DIRECT)

    lv_display_set_flush_cb(disp, display_flush_direct);
    lv_display_set_buffers(disp, s_draw_buf, s_draw_buf2,
                           DRAW_BUF_FULL_SIZE,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

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

#if defined(JC3248W535C_ROT_90_CW)
    LOGI(TAG, "DONE — JC3248W535C ready at %d×%d landscape (90° CW rotation)",
         TFT_WIDTH, TFT_HEIGHT);
#elif defined(JC3248W535C_ROT_90_CCW)
    LOGI(TAG, "DONE — JC3248W535C ready at %d×%d landscape (90° CCW / 270° CW rotation)",
         TFT_WIDTH, TFT_HEIGHT);
#elif defined(JC3248W535C_ROT_180)
    LOGI(TAG, "DONE — JC3248W535C ready at %d×%d landscape (180° rotation)",
         TFT_WIDTH, TFT_HEIGHT);
#endif
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

