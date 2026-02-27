# Pendant RAM Accounting — `display-wt32-sc01-plus`

Target: ESP32-S3, 512 KB internal SRAM, 8 MB OPI-PSRAM.  
Build flags: `BOARD_HAS_PSRAM`, `ESP32_HW`, `ASYNC_RESPONSE_PROCESSING`, `ASYNC_GCODE_SENDING`.

---

## Internal SRAM

### FreeRTOS Task Stacks

| Task | Created in | Stack | Notes |
|---|---|---|---|
| `lvgl_task` | [pendant.cpp](../src/apps/pendant.cpp) | **10 KB** | Pinned to core 0; 12 KB if `LVGL_TASK_PSRAM` defined |
| LVGL draw thread | [lv_conf.h](../src/lv_conf.h) `LV_DRAW_THREAD_STACK_SIZE` | **8 KB** | Spawned by LVGL internally |
| `Machine` (multi-machine RRF+Remote) | [machine_task.c](../src/tasks/machine_task.c) `TASK_STACK_SIZE = 6 KB` | **6 KB** | ⚠️ See DWC note below |
| `MachineRRFProc` | [machine_response_proc_task.c](../src/tasks/machine_response_proc_task.c) `1024*4+512` | **4.5 KB** | Async response processing |
| `MachineRemoteProc` | [machine_response_proc_task.c](../src/tasks/machine_response_proc_task.c) | **4.5 KB** | Async response processing |
| `remote_send_task` | [remote_comms_wrapper.c](../src/driver/remote_comms_wrapper.c) | **4 KB** | ESP-NOW send path |
| `remote_recv_task` | [remote_comms_wrapper.c](../src/driver/remote_comms_wrapper.c) | **4 KB** | ESP-NOW recv path |
| `MachineSendTask` | [pendant.cpp](../src/apps/pendant.cpp) `2 KB` | **2 KB** | Async G-code sender |
| `lv_tick_task` (if `USE_TICK_TASK`) | [pendant.cpp](../src/apps/pendant.cpp) | **64 B** | Tiny tick task |
| FreeRTOS idle tasks × 2, timer task, TCBs (~9 tasks × ~240 B overhead) | IDF | ~6 KB | IDF internal |
| **Task stack subtotal** | | **~49 KB** | |

> **⚠️ DWC mode stack requirement:** The two DWC polling functions in
> [machine_rrf.c](../src/machine/machine_rrf.c) each allocate a
> `char response_buffer[4096]` on the stack (previously `static`, now
> stack-local to reclaim BSS). DWC mode is **not currently active** (build uses
> `RRF` serial + remote, not `DWC_MACHINE_MODE`). If DWC is re-enabled,
> `TASK_STACK_SIZE` in [machine_task.c](../src/tasks/machine_task.c) must be
> raised from **6 KB → at least 12 KB** to accommodate the 4 KB response buffer
> plus call-chain overhead. The two stack-local buffers are not live at the same
> time (separate functions), so 12 KB is sufficient.

---

### Static BSS (permanent, internal)

| Symbol | Location | Size | Notes |
|---|---|---|---|
| `static machine_rrf_t machine_rrf` | [pendant.cpp](../src/apps/pendant.cpp) | ~200 B | RRF transport struct |
| `static machine_interface_remote_t machine_remote` | [pendant.cpp](../src/apps/pendant.cpp) | ~2.5 KB | Includes `msg_buffer[10]` × `remote_msg_t` |
| `static multi_machine_interface_t machine` | [pendant.cpp](../src/apps/pendant.cpp) | ~2 KB | Wrapper over RRF + remote |
| `static interface_t interface` | [pendant.cpp](../src/apps/pendant.cpp) | ~0.5 KB | UI bridge; `log_message_buf[256]`, probe handler, mdi handler |
| LVGL static objects, style caches, font tables | LVGL internals | ~5–10 KB | Montserrat 12/14/24 enabled |
| `g_isr_port_data[5]`, misc driver handles | [arduino_serial_wrapper.cpp](../src/driver/arduino_serial_wrapper.cpp) | <1 KB | |
| **BSS subtotal** | | **~12–17 KB** | |

> **Note: DWC response buffers removed from BSS.** The two
> `static char response_buffer[4096]` that previously occupied 8 KB of
> permanent BSS in [machine_rrf.c](../src/machine/machine_rrf.c) have been
> converted to stack-local allocations. They now exist only on the machine task
> stack while the DWC polling functions are executing.

---

### Internal Heap (runtime)

| Allocation | Location | Size | Notes |
|---|---|---|---|
| UART ring buffer (`RING_BUFFER_SIZE = 4 KB`) | [arduino_serial_wrapper.cpp](../src/driver/arduino_serial_wrapper.cpp) `rb_init()` | **4 KB** | RRF serial port RX ring buffer |
| Response proc ring buffers (`SHARED_BUFFER_SIZE = 4 KB` × 2 tasks) | [machine_response_proc_task.c](../src/tasks/machine_response_proc_task.c) | **8 KB** | Heap `malloc`; could move to PSRAM heap |
| G-code send queue (`25 × MAX_GCODE_STR_LEN = 25 × 192 B`) | [machine_send_task.c](../src/tasks/machine_send_task.c) | **~5 KB** | FreeRTOS queue inline-copies each G-code string |
| Remote send queue (`4 × sizeof(remote_send_queue_item_t) ≈ 4 × 257 B`) | [remote_comms_wrapper.c](../src/driver/remote_comms_wrapper.c) | ~1 KB | |
| Response proc queue × 2 (`10 × 1 B`) | [machine_response_proc_task.c](../src/tasks/machine_response_proc_task.c) | ~80 B | Notification-only queue |
| Task args structs, misc callbacks | various | ~2 KB | Small allocations |
| **App heap subtotal** | | **~20 KB** | |
| **ESP-IDF SDK overhead** (TCP stack stubs, NVS, event loop, timers, ESP-NOW, logging) | IDF | **~50–80 KB** | Estimate; varies with enabled components |

---

### Internal SRAM Summary

| Region | Estimated Total |
|---|---|
| Task stacks | ~49 KB |
| Static BSS | ~15 KB |
| App heap | ~20 KB |
| IDF SDK heap | ~65 KB |
| **Total internal** | **~149 KB** |

ESP32-S3 has ~330 KB of usable internal DRAM (after IDF .bss, .data, and IRAM code reservation). This leaves roughly **180 KB headroom** for dynamic growth, but the number is consumed quickly by IDF Wi-Fi/TCP if that is enabled.

---

## PSRAM

| Allocation | Location | Size | Notes |
|---|---|---|---|
| LGFX draw `buf1` (full-frame, `480×320×2`) | [wt32_sc01_plus.cpp](../src/driver/wt32_sc01_plus.cpp) `display_alloc()` | **300 KB** | `USE_PSRAM` + `VDO_BUF_CAPS = MALLOC_CAP_SPIRAM` |
| LGFX draw `buf2` (double-buffer, `USE_DOUBLE`) | [wt32_sc01_plus.cpp](../src/driver/wt32_sc01_plus.cpp) | **300 KB** | Remove `#define USE_DOUBLE` to save 300 KB (tearing trade-off) |
| LVGL memory pool (`LV_MEM_SIZE = 100 KB`, `.ext_ram.bss`) | [lv_conf.h](../src/lv_conf.h) | **100 KB** | Placed in PSRAM via `LV_ATTRIBUTE_LARGE_RAM_ARRAY` |
| LVGL pool expand (`LV_MEM_POOL_EXPAND_SIZE = 64 KB`) | [lv_conf.h](../src/lv_conf.h) | **0–64 KB** | Allocated lazily when pool is exhausted |
| LVGL draw layer buffer (`LV_DRAW_LAYER_SIMPLE_BUF_SIZE = 24 KB`) | [lv_conf.h](../src/lv_conf.h) | **24 KB** | Used for transformed/layered widgets |
| Camera `frame_buf` (`TFT_HEIGHT × TFT_WIDTH × 2 = 320×480×2`) | [cam_receiver.c](../src/driver/cam_receiver.c) `cam_receiver_create()` | **300 KB** | ✅ Reduced from 640×480 (614 KB); saves ~314 KB |
| Camera JPEG tile pool (`CAM_RECEIVER_POOL_SIZE=4 × TILE_BUF_SIZE=4 KB`) | [cam_receiver.h](../src/driver/cam_receiver.h) | **16 KB** | Reusable pool; tiles decode immediately |
| Camera TJpgDec work buffer | [cam_receiver.c](../src/driver/cam_receiver.c) | **4 KB** | Only allocated if `CAM_USE_TJPGD` |
| MDI log buffer (`MDI_LOG_BUF_SIZE = 1 KB`, `MDI_MALLOC`) | [mdi_handler.c](../src/ui/mdi_handler.c) | **1 KB** | Goes to PSRAM when `BOARD_HAS_PSRAM` |
| LVGL task stack (if `LVGL_TASK_PSRAM` defined) | [pendant.cpp](../src/apps/pendant.cpp) | **12 KB** | Currently in internal RAM (not defined) |
| **PSRAM subtotal** | | **~1,045–1,121 KB** | |

---

## Remaining Optimization Candidates

| Priority | Item | Saving | Action |
|---|---|---|---|
| 🟡 Medium | Response proc ring buffers (`malloc`, 2×4 KB internal heap) | **8 KB internal** | Change `malloc` → `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` in [machine_response_proc_task.c](../src/tasks/machine_response_proc_task.c) |
| 🟡 Medium | Serial UART ring buffer (`malloc`, 4 KB internal heap) | **4 KB internal** | Change `malloc` → `heap_caps_malloc(RING_BUFFER_SIZE, MALLOC_CAP_SPIRAM)` in [arduino_serial_wrapper.cpp](../src/driver/arduino_serial_wrapper.cpp) `rb_init()`. Verify ISR safety for OPI-PSRAM. |
| 🟡 Medium | G-code send queue (25 × 192 B = 4.8 KB) | **2–3 KB internal** | Reduce `QUEUE_LENGTH` from 25→10 in [machine_send_task.c](../src/tasks/machine_send_task.c). Most use cases queue <5 commands at a time. |
| 🟢 Low | LVGL task stack in internal RAM (10 KB) | **10 KB internal** | Define `LVGL_TASK_PSRAM` to move it to PSRAM via `xTaskCreateWithCaps` |
| 🟢 Low | LGFX double draw buffer (`buf2`, 300 KB) | **300 KB PSRAM** | Remove `#define USE_DOUBLE` in [wt32_sc01_plus.cpp](../src/driver/wt32_sc01_plus.cpp) for single-buffer partial rendering (some tearing possible on fast updates) |

---

## Already Applied Optimisations

| Item | Saving | Location |
|---|---|---|
| Camera `frame_buf` capped to display res (`TFT_HEIGHT × TFT_WIDTH`) instead of `640×480` | **~314 KB PSRAM** | [pendant.cpp](../src/apps/pendant.cpp) `cam_init()` |
| DWC `response_buffer[4096] ×2` moved from `static` BSS to stack-local | **8 KB internal BSS** | [machine_rrf.c](../src/machine/machine_rrf.c) — stack-only cost when DWC is active |
| LVGL pool placed in PSRAM `.ext_ram.bss` (`BOARD_HAS_PSRAM`) | **100 KB internal** | [lv_conf.h](../src/lv_conf.h) `LV_ATTRIBUTE_LARGE_RAM_ARRAY` |
