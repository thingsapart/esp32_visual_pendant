# JC3248W535C — Memory Constraints & Layout

## Hardware budget

| Region | Total | Notes |
|---|---|---|
| Internal SRAM | 164,500 B (~160 KB) | Xtensa TCM + DRAM, accessible by DMA |
| OPI PSRAM | 8,388,608 B (8 MB) | 80 MHz DDR octal, cache-backed, **NOT directly DMA-safe** |

## Observed runtime allocation (IDF-native path, from log)

| Checkpoint | Int. SRAM free | Int. largest block | PSRAM free |
|---|---|---|---|
| After boot (pre-init) | ~112,672 B | ~32,768 B | ~8,381,924 B |
| After `display_alloc()` | ~51,224 B | ~21,504 B | ~8,074,720 B |
| After `interface_init()` | ~10,504 B | ~7,680 B | ~7,736,516 B |

At the last checkpoint only a **7,680 B** contiguous block remains in internal SRAM —
smaller than any FreeRTOS task stack.  Without remediation all `xTaskCreate*` calls
fail and `abort = true` cascades to kill every subsequent task.

## Static allocation table

| Allocation | Size | Location | Reason |
|---|---|---|---|
| `draw_buf` (LVGL frame buffer) | 307,200 B (320×480×2) | **PSRAM** | Too large for SRAM; CPU renders through D-cache so write-back cache keeps it fast |
| `trans_buf1` (DMA bounce) | 30,720 B (1/10 frame) | **Internal SRAM, DMA-capable** | GDMA reads physical PSRAM, bypassing D-cache → stale pixels; bounce via SRAM solves it |
| `trans_buf2` (DMA bounce) | 30,720 B (1/10 frame) | **Internal SRAM, DMA-capable** | Alternates with `trans_buf1` to overlap CPU copy with SPI DMA |
| TE semaphore (`te_sync_sem`) | ~88 B | Internal SRAM | Used in GPIO ISR — must be accessible without cache |
| DMA-done semaphore (`trans_done_sem`) | ~88 B | Internal SRAM | Used in SPI DMA ISR callback |
| `te_isr_handler` (ISR code) | ~60 B | **IRAM** (`IRAM_ATTR`) | Must not miss I-cache during interrupt |
| `on_color_trans_done` (DMA callback) | ~60 B | **IRAM** (`IRAM_ATTR`) | Called from SPI DMA ISR — must be in IRAM |
| LVGL task stack | 12,288 B | **PSRAM** (via `LVGL_TASK_PSRAM`) | SRAM exhausted; task stack is D-cache backed so hot frames stay in cache |
| Machine task stacks (×3–4, 6–8 KB each) | ~28 KB total | **PSRAM** (via `CONFIG_SPIRAM_USE_MALLOC + ALWAYSINTERNAL=4096`) | Same reasoning; freed from SRAM budget |
| FreeRTOS TCBs, queues, small primitives | < 4 KB each | **Internal SRAM** | Below the `ALWAYSINTERNAL=4096` threshold |

## Why DMA cannot read PSRAM directly

The PSRAM sits on the same physical bus (OPI / SPI2) as the QSPI display.  Even on
boards where they are electrically separate, the ESP32-S3 GDMA reads **physical**
PSRAM addresses, bypassing the Xtensa D-cache.  The CPU writes pixels through the
cache; if those cache lines haven't been evicted yet the GDMA sees stale data — this
manifests as the characteristic ~8×8-pixel mosaic artefact.  The bounce buffer
pattern (CPU `memcpy` PSRAM → SRAM, then DMA from SRAM) is the correct solution.

## sdkconfig keys (platformio.ini `custom_sdkconfig`)

```text
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y          # OPI PSRAM at 80 MHz DDR
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y        # Route large malloc() to PSRAM
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
                                  # Requests > 4 KB → PSRAM; smaller → SRAM
                                  # Effect: task stacks (6–8 KB) go to PSRAM,
                                  #         FreeRTOS primitives stay in SRAM
```

---

## Performance-critical allocations: SRAM vs PSRAM priority

### Keep in SRAM (non-negotiable)

| Item | Why |
|---|---|
| DMA bounce buffers (`trans_buf1/2`, 2 × 30 KB) | Must be accessible to GDMA without cache; the only path data can reach the display panel |
| FreeRTOS semaphore/queue objects | Used inside ISR callbacks — must never cause a cache miss at interrupt time |
| ISR and DMA callback functions (`IRAM_ATTR`) | Code accessed during interrupt must be in IRAM; a cache miss here panics |

### Keep in SRAM if budget allows (performance-sensitive)

| Item | PSRAM penalty | Notes |
|---|---|---|
| LVGL task stack (12 KB) | ~1–3 % task overhead; hot frames warm in cache quickly | Moved to PSRAM as last resort only |
| Machine task stacks (6–8 KB each) | Negligible — these tasks sleep most of the time waiting on UART/queues | Safe in PSRAM |
| Byte-swap scratch (fused into copy loop — no separate buffer needed) | — | See optimisation below |

### Fine in PSRAM

| Item | Notes |
|---|---|
| `draw_buf` (307 KB LVGL frame buffer) | CPU writes go through write-back D-cache; sequential rendering pattern is cache-friendly |
| LVGL object trees / UI widgets | Infrequently touched at the byte level; PSRAM cache-backed access is acceptable |
| Machine state structs, GCode buffers | Low access frequency; latency imperceptible |
| Font / image assets (if heap-allocated) | Accessed sequentially during render; cache warm-up cost is paid once per dirty region |

---

## Flush pipeline and PSRAM bandwidth

Each `display_flush()` call processes 307,200 bytes of PSRAM:

```
LVGL render → draw_buf (PSRAM)
               │
               │ CPU memcpy (with fused byte-swap) — 307 KB PSRAM read, once
               ▼
           trans_buf1 / trans_buf2 (Internal SRAM, DMA-safe, 30 KB each)
               │
               │ GDMA → SPI2 QSPI bus → AXS15231B panel
               ▼
           Display
```

The two bounce buffers alternate so the CPU is copying chunk N+1 from PSRAM
while the GDMA is simultaneously sending chunk N over SPI — hiding ~half
the copy latency behind the SPI transfer time.

OPI PSRAM at 80 MHz DDR delivers roughly 150–200 MB/s burst reads (cached
sequential access).  At that rate, one full 307 KB frame read takes
**~1.5–2 ms**.  The SPI2 bus at 40 MHz QSPI is the actual throughput bottleneck
(~20 MB/s = ~15 ms/frame), so PSRAM latency is not on the critical path.

---

## Open optimisation opportunities

1. **Fuse byte-swap into the copy loop** *(implemented — see driver code)*  
   The original code did a full 307 KB in-place swap pass over PSRAM before the
   chunked copy, then re-read the same 307 KB during `memcpy`.  Fusing the swap
   into the copy reads PSRAM **once** instead of twice, saving ~1.5 ms per frame.

2. **`IRAM_ATTR` on `on_color_trans_done`** *(implemented — see driver code)*  
   The DMA-done callback was missing `IRAM_ATTR`.  Although a flash-cache stall
   during normal operation is unlikely, it is undefined behaviour in ISR context.
   ISR callbacks must always reside in IRAM.

3. **`ESP_INTR_FLAG_IRAM` on SPI DMA ISR** (potential)  
   When registering the panel IO, passing `ESP_INTR_FLAG_IRAM` to the underlying
   SPI driver ensures the DMA done interrupt is unaffected by flash cache
   disable periods.  This requires downstream support in `esp_lcd_new_panel_io_spi`.

4. **Font/image assets in PSRAM at startup** (potential)  
   LVGL font bitmaps and PNG/bin images stored in flash (XIP) are read via the
   I-cache during rendering.  If total asset size fits (~500 KB–1 MB), copying
   them to PSRAM at boot converts unpredictable I-cache misses into a predictable
   and cache-friendly sequential PSRAM scan.

5. **Reduce `TRANS_SIZE` / bounce buffer count** (marginal)  
   `TRANS_SIZE = 1/10 frame = 30,720 B`.  Doubling it to 1/5 frame (61,440 B)
   would halve the ISR round-trips per frame at the cost of another 30 KB from
   the tight SRAM budget — not worth it at current capacity.
