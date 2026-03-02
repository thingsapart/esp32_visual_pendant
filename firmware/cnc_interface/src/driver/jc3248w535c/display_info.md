# JC3248W535C Display Driver — Debugging Notes

Board: Guition/Makerfabs JC3248W535C  
MCU: ESP32-S3-WROOM-1-N8R8 (240 MHz, 8 MB QIO Flash, 8 MB OPI PSRAM)  
Panel: 3.2" IPS 320×480 (portrait native), AXS15231B controller via QSPI (SPI2)  
Touch: AXS15231B integrated, accessed via I2C  

---

## Symptom

Display showed a mosaic of ~8×8 pixel blocks: some solid black, some a single colour,
most containing random pixels. The pattern was consistent across frames — not flicker,
but a stable corruption. After rotation was partially fixed, the left 320 px showed
near-correct (but sheared) content while the right 160 px were random.

---

## Root Causes Found (in order of discovery)

### 1. Wrong compile path being fixed (session 1–2)

The build environment `display-jc3248w535c` in `platformio.ini` defines
`-D ESP32_LVGL_ESP_DISP`, activating the `ESP32_Display_Panel` C++ library path.
All fixes applied to the IDF-native `#else` block were compiled to dead code.

**Lesson:** Always verify which `#if` branch the target build environment activates
before changing code.

---

### 2. Buffer size mismatch — LVGL writing past end of allocation (session 2)

`display_alloc()` allocated `DRAW_BUF_SIZE = TFT_WIDTH * TFT_HEIGHT / 5 * 2`
= **61 440 bytes**, but `display_setup()` passed `LV_DISPLAY_RENDER_MODE_FULL` to
`lv_display_set_buffers()`. LVGL rendered a full 480×320 = **307 200-byte** frame
into a 61 440-byte heap block, writing 245 760 bytes of heap garbage past the end.
`drawBitmap` then blasted all 307 200 bytes to the panel — 4/5 of which was random
heap content — producing the mosaic.

**Fix:** Allocate `DRAW_BUF_FULL_SIZE = TFT_WIDTH * TFT_HEIGHT * 2` (307 200 bytes)
from PSRAM and pass `DRAW_BUF_FULL_SIZE` to `lv_display_set_buffers()`.

---

### 3. GDMA / D-cache coherency (the real mosaic cause, session 3)

Even after allocating the correct size from PSRAM, the mosaic persisted.

**Root cause:** The LVGL draw buffer lives in PSRAM, which is cache-backed on the
ESP32-S3. LVGL renders via the CPU, writing into the D-cache. When
`esp_lcd_panel_draw_bitmap()` triggers a GDMA transfer, the GDMA reads *physical
PSRAM* directly, bypassing the D-cache entirely. Any cache line that has been
written by the CPU but not yet written back to physical PSRAM is invisible to the
GDMA — the GDMA reads stale or zero-initialised PSRAM content instead.

The ESP32-S3 D-cache line size is **32 bytes = 16 RGB565 pixels**. This makes the
corruption appear in 16-pixel-wide horizontal strips, which at a 320-pixel/row stride
groups visually into roughly 8×8 blocks.

**Fix (from GthiN89/JC3248W535EN reference):** Never DMA directly from PSRAM.
Instead, stream the frame in chunks:

```
PSRAM draw buf  ──(CPU memcpy)──►  DMA-capable internal SRAM bounce buf
                                            │
                                   esp_lcd_panel_draw_bitmap()
                                            │
                                          Panel
```

The CPU `memcpy` forces a D-cache writeback for the copied region into the
destination (internal SRAM), which has no cache — so the GDMA reads the correct data.

Two alternating 30 720-byte bounce buffers (`trans_buf1/2`, `MALLOC_CAP_DMA |
MALLOC_CAP_INTERNAL`) allow CPU copy of chunk N+1 to overlap with DMA of chunk N.

---

### 4. Portrait vs. landscape mismatch — sheared image (session 4)

After fixing the mosaic, the display showed the left 320 px as near-correct but
horizontally sheared, with the right 160 px being the next LVGL row's data.

**Root cause:** The AXS15231B panel's native orientation is **portrait: 320 px wide ×
480 px tall**. The build flags define `TFT_WIDTH=480, TFT_HEIGHT=320` for a landscape
UI. The flush callback was sending 480-pixel-wide rows, but the panel treats each row
as 320 pixels wide. Every pixel N in the stream landed at physical position
`(N % 320, N / 320)` instead of `(N % 480, N / 480)` — a 160-pixel diagonal shear.

**Fix:** Add `lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90)` in
`display_setup()`. LVGL's software rotation engine maps the 480×320 logical canvas
into a physically-correct 320×480 buffer. The flush callback then uses
`area->x2 - area->x1 + 1` / `area->y2 - area->y1 + 1` for physical dimensions
(320×480 after rotation) rather than hardcoded `TFT_WIDTH`/`TFT_HEIGHT`.

---

## Additional Fixes Applied

| Issue | Fix |
|---|---|
| `lv_disp_flush_ready()` called before DMA completes | Replaced with `on_color_trans_done` async DMA callback (IDF-native path) |
| Missing RGB565 byte-swap | Added `lv_draw_sw_rgb565_swap()` before each transmission (`LV_COLOR_16_SWAP=0` is correct; swap is done explicitly) |
| No TE sync | Added TE GPIO38 ISR (`GPIO_INTR_NEGEDGE`) with binary semaphore; `display_flush` waits for V-blank before first chunk |
| Missing `data_endian` | Added `.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE` to panel config (IDF-native path) |
| Default init commands missing `0x35` | Supplied full vendor init table including `{0x35, {0x00}, 1, 0}` to enable TE output on GPIO38 |
| `swap_xy` / `mirror` calls | Removed — AXS15231B MADCTL bit has no effect via QSPI (confirmed espressif/esp-iot-solution#579); use `lv_display_set_rotation()` instead |
| TE edge direction | Changed from `GPIO_INTR_POSEDGE` to `GPIO_INTR_NEGEDGE` to match AXS15231B reference BSP |

---

## Final Architecture

```
LVGL render (480×320 logical)
        │
        │  lv_display_set_rotation(90°)
        ▼
PSRAM draw buffer    320×480 physical (307 200 bytes)
        │
        │  display_flush():
        │    1. lv_draw_sw_rgb565_swap() — byte-swap in-place
        │    2. Wait TE negedge semaphore — V-blank sync
        │    3. Loop 10 chunks (48 rows each):
        │         memcpy chunk → trans_buf (internal DMA RAM, 30 720 B)
        │         wait trans_done_sem (previous DMA done)
        │         esp_lcd_panel_draw_bitmap(panel, 0, row, 320, row+48, trans_buf)
        │    4. wait final trans_done_sem
        │    5. lv_display_flush_ready()
        ▼
AXS15231B panel  (320 px × 480 px portrait native)
```

---

## Key References

- [GthiN89/JC3248W535EN](https://github.com/GthiN89/JC3248W535EN) — working IDF + LVGL v8 reference using bounce buffers
- [Shadowtrance/Tactility](https://github.com/Shadowtrance/Tactility) — confirmed `LV_DISPLAY_RENDER_MODE_FULL` requirement
- [espressif/esp-iot-solution#579](https://github.com/espressif/esp-iot-solution/issues/579) — confirms `swap_xy`/`mirror` are no-ops on AXS15231B via QSPI
- [TactilityProject/Tactility#223](https://github.com/TactilityProject/Tactility/issues/223) — confirms PSRAM DMA coherency issue on ESP32-S3 QSPI panels
