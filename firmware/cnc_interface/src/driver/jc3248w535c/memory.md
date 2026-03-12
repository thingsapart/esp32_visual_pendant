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
| `trans_buf1` (DMA bounce) | 15,360 B (1/20 frame) | **Internal SRAM, DMA-capable** | GDMA reads physical PSRAM, bypassing D-cache → stale pixels; bounce via SRAM solves it |
| `trans_buf2` (DMA bounce) | 15,360 B (1/20 frame) | **Internal SRAM, DMA-capable** | Alternates with `trans_buf1` to overlap CPU copy with SPI DMA |
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
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y            # Move .text/.rodata to XIP-PSRAM, growing
                                  # DRAM heap by ~15–40 KB.
                                  # NB: Tasmota's sdkconfig.defaults has both
                                  # disabled; explicit override is required.
CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y
                                  # 32 KB ICache keeps XIP-PSRAM code hot.
                                  # Tasmota default is 16 KB; the extra 16 KB
                                  # of SRAM0 previously used as IRAM is freed
                                  # by FETCH_INSTRUCTIONS anyway.
CONFIG_ESP32S3_DATA_CACHE_32KB=y  # 32 KB DCache for PSRAM rodata/data access
CONFIG_ARDUINO_LOOP_STACK_SIZE=6144
                                  # Was 10,280 (base default). HWM at boot
                                  # showed only ~2 KB peak; 6 KB saves ~4 KB.
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

---

## Tasmota platform 2026.02.30 — regressions and mitigations

### Background

`shared_build.ini` was updated from `espressif32 @ 2025.04.30` to `2026.02.30`.
This brought arduino-esp32 v3.1.10 and IDF 5.3.4, and — critically — a new
packaging arrangement: `framework-arduinoespressif32-libs` ships the pre-built
ESP-IDF static libraries as a **separate** package from the Arduino glue layer.
The pre-built `.a` files bake in their own `sdkconfig` defaults that differ
significantly from the old `framework-arduinoespressif32` package used by 2025.04.30.

### New-platform sdkconfig defaults that increase DRAM usage

The table below compares the two framework lib sdkconfig files:

| Setting | 2025.04.30 default | 2026.02.30 default | DRAM impact |
|---|---|---|---|
| `CONFIG_BT_ENABLED` | `n` | `y` | ~15–45 KB (controller static heap arrays linked in even when disabled at app level) |
| `CONFIG_BT_CONTROLLER_ENABLED` | `n` | `y` | same as above |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | `n` | `y` | corrupts boot if no OTA partition; wastes ~4 KB code |
| `CONFIG_APP_ROLLBACK_ENABLE` | `n` | `y` | same |
| `CONFIG_ESP_TIMER_TASK_STACK_SIZE` | `4096` | `8192` | **+4 KB DRAM** (timer task stack) |
| `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` | `y` | `n` | if not overridden: ~15–40 KB less DRAM |
| `CONFIG_SPIRAM_RODATA` | `y` | `n` | same |
| `CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB` | `y` (32 KB) | `n` (16 KB) | if not overridden: 16 KB less IRAM freed → less DRAM headroom |
| `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` | `n` (NONE) | `y` | ~2–4 KB code, coredump stack |
| `CONFIG_PPP_SUPPORT` | `n` | `y` | ~4–8 KB LWIP code + data |
| `CONFIG_LWIP_IPV6` | `n` | `y` | ~8 KB LWIP DRAM for IPv6 state |
| `CONFIG_LWIP_MAX_SOCKETS` | `10` | `16` | ~1–2 KB socket descriptor table |
| `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` | `4096` | `16384` | **+12 KB** (TLS RX buffer, when `ASYMMETRIC_CONTENT_LEN` is not set) |
| ESP RainMaker full stack | absent | compiled in | several KB BSS/data |

Items that **already had explicit overrides** in `custom_sdkconfig` (`FETCH_INSTRUCTIONS`,
`RODATA`, `INSTRUCTION_CACHE_32KB`, `BT_ENABLED`, `APP_ROLLBACK_ENABLE`) were
unaffected.  The items without existing overrides pushed the build past the point
where enough contiguous internal SRAM remained to create any FreeRTOS task stack.

### Mitigations applied to `custom_sdkconfig`

The following keys were added to the `display-jc3248w535c` (and the other S3
targets) `custom_sdkconfig` block in `platformio.ini`:

```text
CONFIG_BT_CONTROLLER_ENABLED=n         # Explicitly disable controller (not just host)
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=n # No OTA partition in the partition table
CONFIG_ESP_TIMER_TASK_STACK_SIZE=4096  # Restore halved timer task stack
CONFIG_PPP_SUPPORT=n                   # Pendant has no modem
CONFIG_LWIP_IPV6=n                     # Pendant uses IPv4 only
CONFIG_LWIP_MAX_SOCKETS=8             # Halve socket table (was 16)
CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096  # Pendant is always TLS client, never server
```

**Estimated additional DRAM recovered: ~30–50 KB.**

### Application-level fixes (P4-era regressions on S3)

Two code paths were silently wasting memory because they were developed on the
ESP32-P4 (which has ~768 KB SRAM) and were never hit on S3:

#### 1. LVGL builtin heap allocated from DRAM (`lv_conf.h`)

`lv_conf.h` routes LVGL's 64 KB builtin pool through `ps_malloc` only when
`defined(HW_ESP32)` is set, but every build in this project defines `ESP32_HW`
(not `HW_ESP32`).  The condition was updated to:

```c
#if (defined(HW_ESP32) || defined(ESP32_HW)) && defined(BOARD_HAS_PSRAM)
```

Effect: LVGL's 64 KB initial pool and up to 64 KB `LV_MEM_POOL_EXPAND_SIZE` are
now allocated from PSRAM instead of DRAM. **Recovers up to 128 KB of DRAM.**

#### 2. Camera receiver always initialised (`pendant.cpp`)

`cam_init()` was guarded by `#if defined(ESP32_HW) && defined(BOARD_HAS_PSRAM)`,
which is true for all S3 production builds — even those with no camera wired.
It unconditionally allocated ~321 KB of PSRAM (307 KB frame buffer + 8 KB pool +
4 KB tjpgd work) and registered an ESP-NOW receive callback, regardless of whether
a camera companion was present.

The guard was changed to `#if defined(ESP32_HW) && defined(APP_CAM_ENABLED)`.
Builds without a camera no longer define `APP_CAM_ENABLED`, so the entire block is
compiled out.  To re-enable for a camera-equipped build, add `-D APP_CAM_ENABLED`
and `-D CAM_MAC_ADDR={...}` to that env.

**Effect: ~321 KB PSRAM reclaimed; one fewer ESP-NOW receive callback registered.**

#### 3. `jog_accumulator` task stack at DRAM threshold

`JOG_ACCUM_TASK_STACK` was 4096 bytes — exactly equal to
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`.  The threshold condition is `size <=
threshold`, so a 4096-byte allocation stays in DRAM rather than going to PSRAM.
The value was raised to **4608** (> 4096), pushing the task stack to PSRAM and
recovering **4 KB of DRAM** with no change to functional behaviour.

### Summary of total DRAM recovery

| Fix | DRAM recovered |
|---|---|
| LVGL pool → PSRAM (`lv_conf.h`) | up to **128 KB** |
| sdkconfig: `ESP_TIMER_TASK_STACK_SIZE` 8192 → 4096 | **4 KB** |
| sdkconfig: `LWIP_IPV6=n` | **~8 KB** |
| sdkconfig: `PPP_SUPPORT=n` | **~4–8 KB** |
| sdkconfig: `MBEDTLS_SSL_OUT_CONTENT_LEN` 16384 → 4096 | **~12 KB** |
| sdkconfig: `LWIP_MAX_SOCKETS` 16 → 8 | **~2 KB** |
| sdkconfig: `BT_CONTROLLER_ENABLED=n` | **~10–20 KB** |
| jog_accumulator stack → PSRAM | **4 KB** |
| **Total** | **~170–186 KB** |
