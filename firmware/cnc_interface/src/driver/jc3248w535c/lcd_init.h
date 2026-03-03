// LCD hardware initialisation for the JC3248W535C (AXS15231B QSPI panel).
//
// Provides a uniform interface over two backend implementations:
//   • ESP32_Display_Panel library  (ESP32_LVGL_ESP_DISP defined)
//   • IDF-native esp_lcd           (default)
//
// The implementations live in lcd_init.cpp.  The caller (jc3248w535c.cpp)
// selects behaviour purely through build flags; this header is the same for all.

#pragma once

#ifdef JC3248W535C

#ifdef __cplusplus
extern "C++" {
#endif

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

// ---------------------------------------------------------------------------
// Initialise LCD hardware: backlight, QSPI bus, AXS15231B panel.
//
// Parameters
//   on_trans_done   DMA-complete callback registered with the panel IO.
//                   Fires from the SPI DMA ISR after each esp_lcd_panel_draw_bitmap().
//                   Pass nullptr if you do not need async notification
//                   (or if using the ESP32_Display_Panel path, which is synchronous
//                    and ignores this parameter entirely).
//   cb_user_ctx     Forwarded verbatim as user_ctx to on_trans_done.
//
// Returns true on success.
// ---------------------------------------------------------------------------
bool lcd_hw_init(
    bool (*on_trans_done)(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *edata,
                          void *user_ctx),
    void *cb_user_ctx);

// ---------------------------------------------------------------------------
// Draw a bitmap region in **physical** panel coordinates (portrait 320 × 480).
//
// x_start, y_start  : top-left corner of the destination window (inclusive).
// x_end,   y_end    : bottom-right corner of the destination window (exclusive,
//                     i.e. the width is x_end-x_start, height is y_end-y_start).
// data              : packed RGB565 pixels, MSB-first / big-endian byte order
//                     (already byte-swapped relative to LVGL's native LE layout).
//                     In the IDF-native path this buffer MUST be DMA-accessible
//                     (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL).
//
// In the ESP32_Display_Panel path the call is synchronous.
// In the IDF-native path the call is asynchronous: on_trans_done fires when done.
// ---------------------------------------------------------------------------
void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                     const void *data);

// ---------------------------------------------------------------------------
// Wait for the TE (tearing-effect / V-blank) pulse before starting the next
// frame transfer.  Blocks for up to ~20 ms (one frame period at 50 fps).
//
// No-op if:
//   • TFT_TE is not defined or is < 0
//   • TFT_TE GPIO configuration failed during lcd_hw_init()
//   • ESP32_Display_Panel path (TE management delegated to the library)
// ---------------------------------------------------------------------------
void lcd_vsync_wait();

#ifdef __cplusplus
}
#endif

#endif // JC3248W535C
