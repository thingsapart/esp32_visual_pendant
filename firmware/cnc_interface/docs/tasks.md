# FreeRTOS Task Architecture — `cnc_interface` APP_PENDANT

This document describes every FreeRTOS task created by the `cnc_interface` firmware
in the `APP_PENDANT` configuration (specifically the ESP32-P4 SDIO-bridge builds
such as `display-jc8012p4a1-sdio`). It explains what each task does, why it exists,
what it consumes and produces, and how the tasks relate to each other.

---

## Priority Table

```
Priority  Task                  Core   Stack    Description
────────────────────────────────────────────────────────────────────────────
  +5      MachineSendTask        1     8 KB    G-code TX + poll driver
  +4      MachineRemoteProc      1     8 KB    State response parser
  +4      jog_accum              1     4 KB    Encoder-click batch → G-code
  +3      remote_send_task       1     4 KB    ESP-NOW / SDIO outgoing serialiser
 (+3)     LVGL swdraw workers   any    —       Created internally by esp_lvgl_port
  +2      lvgl_task              0    24 KB    LVGL render loop + UI tick
  +2      lv_tick_task / hook    0    64 B     LVGL millisecond tick
  +2      bridged_sdio_rx        1     4 KB    C6 SDIO inbound packet poller
  +1      bridged_hb             1     2 KB    C6 heartbeat / sync ping
  +1      log_writer            any    —       Async serial log drain
  +1      Machine (heartbeat)    —     —       ABSENT in APP_PENDANT (see §7)
────────────────────────────────────────────────────────────────────────────
```

`tskIDLE_PRIORITY` = 0; all priorities above are `tskIDLE_PRIORITY + N`.

---

## 1. `MachineSendTask`  — priority +5 · Core 1 · 8 KB

**Source:** `src/tasks/machine_send_task.c`
**Created in:** `pendant.cpp` → `setup()` via `machine_send_task_run()`

### What it does
Owns the entire outgoing G-code pipeline **and** drives the machine poll loop.
On each iteration:
1. Waits on `machine_send_queue` with a computed timeout (`poll_interval −
   elapsed`). If a G-code command arrives before the timeout it is forwarded
   immediately to the active machine implementation's `_send_gcode()`.
2. When the poll timer fires (every `MACHINE_SEND_GCODE_INTERVAL_MS` = 50 ms,
   multiplied by `MACHINE_POLL_EVERY_NTH_INTERVAL` = 3 → effective ~150 ms)
   it calls `machine_interface_task_loop_iter()`, which triggers
   `_update_machine_state()` to inject a state-query command (M409, or an
   ESP-NOW state-request packet for the remote transport).

`_send_gcode()` for the **remote** transport calls `remote_wrapper_send()` which
enqueues into `remote_send_queue` (handled by `remote_send_task`).

### Why highest priority?
It must never be starved by UI work. A jammed send queue means jog commands pile
up and appear unresponsive. At priority +5 it preempts everything except ISRs.

### Produces
- Outgoing G-code / commands on `remote_send_queue` (remote machine)
- Outgoing UART bytes (RRF serial machine — direct, no queue)

### Consumes
- `machine_send_queue` (5 slots × 192 B) — filled by `jog_accum`, `lvgl_task`
  user commands (home, probe, WCS, etc.), and `machine_interface_task_loop_iter`
  poll commands

### Blocks on
- `xQueueReceive(machine_send_queue, gcode, ticks_to_wait)` — bounded wait
  calculated so the poll timer stays on cadence even when the queue is idle

---

## 2. `MachineRemoteProc`  — priority +4 · Core 1 · 8 KB

**Source:** `src/tasks/machine_response_proc_task.c`
**Created in:** `pendant.cpp` → `setup()` via `machine_response_proc_task_run()`

### What it does
Receives raw binary packets from the hub/C6-bridge and parses them into the
machine state model. Each packet is a complete `proc_msg_t` copied directly into
the FreeRTOS queue (no shared ring-buffer). After parsing,
`machine_interface_process_machine_state_response()` updates the
`machine_interface_t` struct fields and fires state-change callbacks, which
`lvgl_task` reads inside `interface_tick()`.

### Why priority +4?
Parsing must happen quickly so the UI gets fresh state. At +4 it can preempt
`remote_send_task` (+3), `lvgl_task` (+2), and `bridged_sdio_rx` (+2), so a
completed SDIO inbound frame is parsed before the next frame can arrive.

### Produces
- Updated `machine_interface_t` state (positions, status, spindle, tools, …)
- State-change callbacks → consumed by `lvgl_task` via `interface_tick()`

### Consumes
- `machine_remote_proc_queue` (40 slots × 254 B) — filled by `bridged_sdio_rx`
  via the registered receive callback

### Blocks on
- `xQueueReceive(machine_remote_proc_queue, &msg, portMAX_DELAY)` — sleeps
  until a packet arrives

---

## 3. `jog_accum`  — priority +4 · Core 1 · 4 KB

**Source:** `src/tasks/jog_accumulator_task.c`
**Created in:** `pendant.cpp` → `setup()` via `jog_accumulator_init()`

### What it does
Batches encoder "click" deltas that arrive at human hand-speed and converts them
into a **single** consolidated G-code jog move per batch. Two phases:

1. **Wait** on `s_queue` for the first click (`JOG_ACCUM_INITIAL_WAIT_MS`).
2. **Drain** — non-blocking loop consuming every additional click already in the
   queue into a running total.
3. Dispatch one `machine_interface_step_current_axis()` call with the net
   distance.
4. Optionally **sleep** for the estimated move duration (kinematic model) so
   the CNC controller isn't flooded — the task wakes up just before the move
   finishes, ready for the next batch.

### Why priority +4?
Same as `MachineRemoteProc` — both can preempt `remote_send_task` and the UI.
Jog latency is safety-critical (user expects immediate response to encoder turns).

### Why Core 1?
Its only output is `machine_send_queue` consumed by `MachineSendTask` (Core 1).
Running on Core 1 avoids cross-core overhead and keeps both jog dispatch and
command transmission on the same scheduler.

### Produces
- Items on `machine_send_queue` via `machine_interface_step_current_axis()`
  → `machine_interface_send_gcode()` → `xQueueSendToFront`

### Consumes
- `s_queue` (jog click queue, `int8_t` items) — filled by `lvgl_task` via
  `jog_accumulator_post_clicks(diff)` on each encoder read

### Blocks on
- `xQueueReceive(s_queue, &click, pdMS_TO_TICKS(JOG_ACCUM_INITIAL_WAIT_MS))` —
  waits for first click
- `vTaskDelay(estimated_move_ms)` — self-throttle between batches

---

## 4. `remote_send_task`  — priority +3 · Core 1 · 4 KB

**Source:** `src/driver/remote_comms_wrapper.c`
**Created in:** `remote_send_task_run()`, called during `remote_wrapper_init()`

### What it does
Serialises outgoing ESP-NOW / SDIO transmissions. Calling `esp_now_send()`
(or `c6_sdio_bridge_write()`) directly from high-priority contexts would block
those contexts for the full SDIO frame time (~1–5 ms). This task decouples the
callers from the physical bus by buffering packets in `remote_send_queue`.

On each iteration: dequeue one `remote_send_queue_item_t`, call
`remote_wrapper_send_now()` (which calls `c6_sdio_bridge_write()` for SDIO
builds), yield 1 ms to let the ESP-NOW/SDIO driver flush.

### Why priority +3?
Below `MachineRemoteProc` (+4) so that a newly received state packet is always
parsed before the next outgoing command is dispatched — response processing
takes priority over transmit. Above `lvgl_task` (+2) so display rendering does
not delay time-sensitive outgoing G-code.

### Produces
- Physical SDIO / ESP-NOW frames to the C6 bridge

### Consumes
- `remote_send_queue` (16 slots × ~257 B) — filled by `MachineSendTask` via
  `remote_wrapper_send()` (200 ms timeout on queue-full)

### Blocks on
- `xQueueReceive(remote_send_queue, &item, portMAX_DELAY)` — sleeps when queue
  is empty

---

## 5. `lvgl_task`  — priority +2 · Core 0 · 24 KB

**Source:** `src/apps/pendant.cpp` (function `lvgl_task()`)
**Created in:** `pendant.cpp` → `setup()`

### What it does
The main UI render loop. Each iteration:
1. `lv_task_handler()` — runs LVGL's draw engine, handles touch input via the
   registered `lvgl_port_touchpad_read` callback (polled, no separate touch task),
   processes LVGL timers and animations, dispatches display-flush callbacks.
2. `vTaskDelay(sleep_time)` — LVGL returns the time until the next event;
   sleeping this long prevents CPU waste without adding latency.
3. `lvgl_ui_task_handler()` — processes deferred UI update jobs (e.g. widget
   rebuilds that must run outside LVGL's render lock).
4. `interface_tick(&interface)` — reads the latest `machine_interface_t` state
   and propagates it to all bound UI components (position labels, status
   indicators, speed readout, etc.).
5. Encoder read → `jog_accumulator_post_clicks()` — pushes delta to `jog_accum`
   queue when the encoder is in jog (not UI) mode.

Also subscribed to the task WDT (`esp_task_wdt_add`) so that render hangs are
caught even though the P4 swdraw workers preempt IDLE0 during rendering.

### Why Core 0?
LVGL internally uses thread-local storage (timer lists, display state) tuned for
single-threaded access. Pinning to Core 0 means only this task ever calls LVGL
APIs for display/input — no locking needed.

### Produces
- DMA pixel transfers to the display (via LVGL flush callbacks → esp_lcd_panel)
- Clicks/diffs on jog click queue → `jog_accum`
- Items on `machine_send_queue` for user-initiated commands (home, probe, WCS,
  macro, jog step-size changes)

### Consumes
- `machine_interface_t` read-only state (updated by `MachineRemoteProc`)
- Touch events (polled inside `lv_task_handler`)
- Encoder delta (read from ISR-backed counter)

### Blocks on
- `vTaskDelay(sleep_time)` — adaptive, driven by LVGL's next-event estimate
- DMA semaphore (inside flush callback) — waits for hardware DMA completion

---

## 6. `lv_tick_task` / tick hook  — priority +2 · Core 0 · 64 B

**Source:** `src/apps/pendant.cpp`
**Created in:** `pendant.cpp` → `setup()`

### What it does
Increments LVGL's internal millisecond counter (`lv_tick_inc`) once per FreeRTOS
tick (1 ms at `CONFIG_FREERTOS_HZ=1000`). LVGL uses this counter for all
animation timers and input debounce.

Implemented as either:
- A dedicated minimal task (`xTaskCreatePinnedToCore`, 64 B stack, Core 0), or
- A FreeRTOS tick hook (`esp_register_freertos_tick_hook_for_cpu`) — cheaper,
  no stack overhead. The hook variant is used when `CONFIG_USE_TICK_HOOK` is set.

### Why it exists
LVGL does not call any OS-provided time functions; it expects the host to call
`lv_tick_inc()` regularly. Without this, animations freeze and touch debounce
stops working.

---

## 7. `bridged_sdio_rx`  — priority +2 · Core 1 · 4 KB

**Source:** `src/driver/esp_bridged_esp_now.c`
**Created in:** `esp_now_init()` (called during `remote_wrapper_init()`)

### What it does
Polls the C6 SDIO slave for inbound ESP-NOW frames. On each iteration it calls
`c6_sdio_bridge_read()` (which acquires `s_bus_mutex`, calls `essl_get_packet`,
releases mutex). If a frame arrives it validates the framing bytes and dispatches
to the registered receive callback (`s_recv_cb`), which eventually calls
`machine_response_proc_task_data_ready()` to enqueue the payload into
`machine_remote_proc_queue`.

When no frame is available, `c6_sdio_bridge_read()` returns quickly (ESSL
returns `ESP_ERR_NOT_FOUND` without waiting), and the task yields for
`SDIO_RX_IDLE_BACKOFF_MS` = 50 ms.

### Why priority +2 (raised from original +1)?
At +1 this task could be starved by any short burst from higher-priority tasks,
delaying state updates from the C6. At +2 it matches `lvgl_task` — both tasks
get equal CPU share when both are runnable, keeping inbound latency predictable.

### Why pinned to Core 1?
`c6_sdio_bridge_read()` shares `s_bus_mutex` with `remote_send_task` (also Core
1). Pinning both to the same core means the mutex is never contested across cores
(Core 1 runs one at a time). It also keeps all SDIO bus traffic on the machine
pipeline core, away from the display pipeline.

### Produces
- Payload items on `machine_remote_proc_queue` → consumed by `MachineRemoteProc`

### Consumes
- SDIO inbound register on the C6 slave via ESSL

### Blocks on
- `vTaskDelay(SDIO_RX_IDLE_BACKOFF_MS)` when no packet available
- `s_bus_mutex` for the duration of each ESSL transaction

---

## 8. `bridged_hb`  — priority +1 · Core 1 · 2 KB

**Source:** `src/driver/esp_bridged_esp_now.c`

### What it does
Sends a periodic heartbeat / diagnostic ping to the C6 bridge every 5 seconds
(`BRIDGE_DEBUG_TRIGGER = '?'`). The C6 replies with a debug frame confirming its
TX gate is open. If no reply arrives after a configurable number of retries,
`bridged_hb` logs a warning — useful for spotting a hung C6 without rebooting.

### Why it exists
The C6's TX gate only opens after it processes its first inbound frame. This task
ensures a periodic re-ping reconnects a bridge that restarted without a full P4
reboot.

### Blocks on
- `vTaskDelay(pdMS_TO_TICKS(5000))` between pings

---

## 9. `log_writer`  — priority +1 · unpinned · variable stack

**Source:** `src/driver/arduino_serial_wrapper.cpp`

### What it does
Receives log lines from `LOGI`/`LOGW`/`LOGE` macros (queued by all tasks) and
writes them to the UART serial port. Decouples log emission from log output so
that writing a log line in a high-priority task does not block on UART TX.

### Why priority +1?
Logging is best-effort. At +1 it yields to every machine and UI task but stays
above idle, so log output is not delayed indefinitely.

---

## 10. `Machine` heartbeat  — **ABSENT in APP_PENDANT (non-DWC)**

**Source:** `src/tasks/machine_task.c`

This task is **not started** in the `APP_PENDANT` non-DWC configuration.

### Why?
`MachineSendTask` (+5) already owns the poll loop via an internal timer that
calls `machine_interface_task_loop_iter()`. Starting `machine_task` as well means
**two tasks** call `_update_machine_state()` on the same `machine_interface_t`
without synchronization:
- Data race on `self->polli` and `self->poll_state` (written by both, no mutex)
- Combined ~17 state requests/second fed into `remote_send_queue` (16 slots),
  leaving no headroom for user commands

The task is preserved in the DWC mode path (where there is no `MachineSendTask`
poll loop) and in `MACHINE_REMOTE_ONLY` builds (where there is no
`MachineSendTask` at all).

---

## Data Flow Diagram

```
 ┌─────────────────────────────────────────────────────────────────────┐
 │  USER INTERACTION (Core 0)                                          │
 │  Touch / Button / Encoder                                           │
 └──────────────────────┬──────────────────────────────────────────────┘
                        │ lv_indev polling (inside lv_task_handler)
                        ▼
 ┌────────────────────────────────────┐
 │  lvgl_task  (Core 0, prio +2)      │
 │  • lv_task_handler()               │
 │  • interface_tick()                │◄──── machine_interface_t state
 │  • encoder read                    │      (updated by MachineRemoteProc)
 └──────┬─────────────────────────────┘
        │                         │
        │ jog_accumulator_post_  │ machine_interface_send_gcode
        │ clicks(diff)             │ (home, probe, WCS, …)
        ▼                         │
 ┌──────────────────┐             │
 │ jog click queue  │             │
 │ (int8_t, Core 0→1)│            │
 └──────────────────┘             │
        │ xQueueReceive             │
        ▼                         ▼
 ┌──────────────────────────────────────────────────────┐
 │  jog_accum  (Core 1, prio +4)                        │
 │  Batch clicks → single net distance → xQueueSendToFront
 └──────────────────────┬───────────────────────────────┘
                        │
                        ▼ xQueueSend/ToFront [5 slots × 192 B]
 ┌────────────────────────────────────────────────────────────────────┐
 │  machine_send_queue  (FreeRTOS queue)                              │
 └────────────────────────────────┬───────────────────────────────────┘
                                  │ xQueueReceive (timeout = until next poll)
                                  ▼
 ┌───────────────────────────────────────────────────────────────────┐
 │  MachineSendTask  (Core 1, prio +5)                               │
 │  • Dequeue G-code → machine._send_gcode()                         │
 │  • Poll timer → machine_interface_task_loop_iter()                │
 │    → _update_machine_state() → remote_wrapper_send()              │
 └──────────────────────────────────┬────────────────────────────────┘
                                    │ xQueueSend(200 ms timeout)
                                    ▼ [16 slots × ~257 B]
 ┌───────────────────────────────────────────────────────────────────┐
 │  remote_send_queue  (FreeRTOS queue)                              │
 └────────────────────────────────┬──────────────────────────────────┘
                                  │ xQueueReceive
                                  ▼
 ┌───────────────────────────────────────────────────────────────────┐
 │  remote_send_task  (Core 1, prio +3)                              │
 │  • c6_sdio_bridge_write(s_bus_mutex) ─── SHARED BUS MUTEX ──┐    │
 └───────────────────────────────────────────────────────────────────┘
                                                                 │
 ┌───────────────────────────────────────────────────────────────────┐
 │  SDIO bus  ↔  C6 bridge (ESP32-C6)  ↔  ESP-NOW  ↔  Hub/CNC      │
 └───────────────────────────────────────────────────────────────────┘
                                                                 │
 ┌───────────────────────────────────────────────────────────────────┐
 │  bridged_sdio_rx  (Core 1, prio +2)                               │
 │  • c6_sdio_bridge_read(s_bus_mutex) ─── SHARED BUS MUTEX ──┘    │
 │  • s_recv_cb() → data_ready() → xQueueSend(0)                    │
 └──────────────────────────────────┬────────────────────────────────┘
                                    │
                                    ▼ [40 slots × 254 B]
 ┌───────────────────────────────────────────────────────────────────┐
 │  machine_remote_proc_queue  (FreeRTOS queue)                      │
 └────────────────────────────────┬──────────────────────────────────┘
                                  │ xQueueReceive portMAX_DELAY
                                  ▼
 ┌───────────────────────────────────────────────────────────────────┐
 │  MachineRemoteProc  (Core 1, prio +4)                             │
 │  • machine_interface_process_machine_state_response()             │
 │  • Updates machine_interface_t fields                             │
 │  • Fires state-change callbacks                                   │
 └──────────────────────────────────┬────────────────────────────────┘
                                    │ (scalar state writes, no LVGL calls)
                                    ▼
                           machine_interface_t
                           (read by lvgl_task → interface_tick)
```

---

## Queue Summary

| Queue | Slots | Item size | Producer(s) | Consumer |
|-------|------:|----------:|-------------|----------|
| `machine_send_queue` | 5 | 192 B | `lvgl_task`, `jog_accum`, `MachineSendTask` poll | `MachineSendTask` |
| `remote_send_queue` | 16 | ~257 B | `MachineSendTask` | `remote_send_task` |
| `machine_remote_proc_queue` | 40 | 254 B | `bridged_sdio_rx` | `MachineRemoteProc` |
| `jog_click_queue` | configurable | 1 B | `lvgl_task` encoder read | `jog_accum` |

---

## Shared Resource: SDIO Bus (`s_bus_mutex`)

A single priority-inheritance mutex (`xSemaphoreCreateMutex`) in
`c6_bridge_sdio.c` serialises ALL ESSL operations. Both the write path
(`c6_sdio_bridge_write`, called from `remote_send_task`) and the read path
(`c6_sdio_bridge_read`, called from `bridged_sdio_rx`) hold this mutex for the
duration of their transaction.

**Why a single mutex matters:** The ESSL handle `s_essl` maintains internal
state (available-buffer counters for the slave) that is not safe for concurrent
access from two tasks/cores. A pair of simultaneous `essl_send_packet` +
`essl_get_packet` corrupts this state, causing the write side to spin forever
waiting for slave buffer space that never appears — exactly the
"machine-send-task queue never drains" symptom.

Both SDIO tasks are pinned to Core 1, so FreeRTOS's per-core preemption
guarantees they cannot physically run simultaneously. The mutex is still
required because FreeRTOS does not serialise task execution globally — a task
switch on Core 1 can interrupt `essl_get_packet` mid-transaction and let
`remote_send_task` enter `essl_send_packet`.

---

## Design Rules

1. **No LVGL API calls outside `lvgl_task`.**  All LVGL object manipulation
   happens inside `lvgl_task` (via `lv_task_handler`, `interface_tick`, or
   `lvgl_ui_task_handler`). Other tasks must only write to the
   `machine_interface_t` scalar state — never touch `lv_obj_t` directly.

2. **No blocking waits inside flush callbacks.**  LVGL flush callbacks run in
   `lvgl_task` context. They may wait on the DMA semaphore (`s_trans_done_sem`)
   but must not call `vTaskDelay` or any potentially-blocking IPC primitive.

3. **State callbacks are read-safe, not LVGL-safe.**  The `state_change_cb` and
   `pos_changed_cb` fired by `MachineRemoteProc` may update shared scalar state
   but must not call LVGL. The UI reads this state in `interface_tick()` on the
   next `lvgl_task` iteration.

4. **`machine_send_queue` sends use 0 or 5 ms timeouts — never blocking.**
   A 0-tick `xQueueSendToFront` is used for user commands (jog, home, …) so
   the high-priority caller is never delayed. A 5 ms timeout is used for poll
   M409 commands; missing a single poll interval is harmless.

5. **`machine_task` must not run alongside `MachineSendTask` in APP_PENDANT.**
   See §10. Both call `machine_interface_task_loop_iter()` on the same object
   with no synchronisation.
