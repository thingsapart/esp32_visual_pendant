# JC3248W535C Display Driver — Debugging Notes

Board: Guition/Makerfabs JC3248W535C  
MCU: ESP32-S3-WROOM-1-N8R8 (240 MHz, 8 MB QIO Flash, 8 MB OPI PSRAM)  
Panel: 3.2" IPS 320×480 (portrait native), AXS15231B controller via QSPI (SPI2)  
Touch: AXS15231B integrated, accessed via I2C  

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
