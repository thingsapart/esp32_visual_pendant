# Pendant ↔ Hub ↔ CNC Controller Latency Analysis

## Architecture Overview

```
[PENDANT]  ←—ESP-NOW—→  [HUB (ESP32)]  ←—UART 115200—→  [CNC Controller (STM32H723)]
```

**Command path (user → CNC):** Pendant generates a jog/move command → ESP-NOW frame to Hub → Hub deserializes → G-code sent via UART serial to CNC controller.

**Status path (CNC → pendant):** Hub periodically polls CNC over UART with M114/M409 commands → parses response → fires change callbacks → relays state to pendant over ESP-NOW.

---

## Implemented Optimisations

This document reflects the state **after** the following changes were applied:

| Change | Before | After |
|---|---|---|
| `HUB_POLL_INTERVAL_MS` | 120 ms | **50 ms** |
| Position poll command | M409 K"move.axes[]" (~180 B JSON) | **M114** (compact ASCII, ~35 B) |
| M114 response parser | cJSON DOM (~0.5 ms) | **hand-scanner** (~5 µs) |
| Full axis poll (M409) | every tick | **every 19 ticks (~950 ms)** (machine pos + homed) |
| JOB_STATUS divisor | ÷3 → 360 ms | **÷2 → 100 ms** |
| SPINDLE divisor | ÷13 → 1,560 ms | **÷3 → 150 ms** |
| User-command queue | normal `xQueueSend` | **`xQueueSendToFront`** (jumps ahead of polls) |
| Round-robin broadcast | 10 slots × 120 ms = 1,200 ms | **5 slots × 50 ms = 250 ms** |
| `remote_send_task` delay | 5 ms | **1 ms** |

---

## Poll Schedule (after changes)

Base interval: **50 ms**.

| Category | Divisor | Effective interval | Serial command |
|---|---|---|---|
| **WCS position (M114)** | every tick | **50 ms** | `M114` (~7 B TX, ~35 B RX) |
| **Full axes (M409)** | ÷19 | **950 ms** | `M409 K"move.axes[]" F"d3,v"` (machine pos, homed, limits) |
| JOB_STATUS (feed, speed, status) | ÷2 | **100 ms** | 3 × M409 |
| MESSAGES_AND_DIALOGS | ÷5 | **250 ms** | 1 × M409 |
| PROBES | ÷7 | **350 ms** | 1 × M409 |
| END_STOPS | ÷11 | **550 ms** | 1 × M409 |
| **SPINDLE (RPM)** | ÷3 | **150 ms** | `M409 K"spindles[]"` |
| TOOLS | ÷17 | **850 ms** | 2 × M409 |
| LIST_FILES / LIST_MACROS | ÷9973 | ~8 min | M20 |

---

## Round-Robin Broadcast (Hub → Pendant, after changes)

5 slots at 50 ms each → **full revolution = 250 ms** (was 1,200 ms).

| Slot | Content |
|---|---|
| 0 | Position (WCS) |
| 1 | Machine status + homed flags |
| 2 | Feed rate + spindle RPM |
| 3 | Active WCS + dialogs |
| 4 | Keep-alive + sensors |

> The round-robin is a **fallback cache refresh** only. Change-driven callbacks fire immediately upon parsing any M114 or M409 response, independent of this schedule.

---

## Jog Command Latency (Pendant → CNC UART RX)

| Segment | Typical | Worst case |
|---|---|---|
| Button press → ESP-NOW TX | 2 ms | 5 ms |
| ESP-NOW air + stack | 2 ms | 5 ms |
| Hub recv_queue → `process_message` | 2 ms | 10 ms |
| Priority queue (`xQueueSendToFront`) | 0 ms | 0 ms |
| In-flight poll command finishing (max 1) | 0 ms | 2 ms |
| Serial TX jog command (~20 chars) | 2 ms | 3 ms |
| **Total to CNC UART RX** | **~8 ms** | **~25 ms** |

**Before optimisations (worst case):** ~38 ms (jog sat behind up to 6 queued poll M409 commands on a heavily-loaded tick).

Priority is implemented via `gcode_queue_priority` flag set in `process_message()` in [hub.cpp](../src/apps/hub.cpp): whenever the hub is processing a pendant command, all enqueued G-codes use `xQueueSendToFront` so they jump ahead of any background poll M409s already in the queue.  No extra queue memory is needed.

---

## Position Update Latency (CNC → Pendant Screen)

### WCS position via M114 (after changes)

| Segment | Typical | Worst case |
|---|---|---|
| Wait for next M114 poll | 25 ms | 50 ms |
| M114 TX + CNC response (35 B) | 3 ms | 4 ms |
| Hand-scanner parse | < 0.1 ms | < 0.1 ms |
| ESP-NOW queue + `remote_send_task` | 2 ms | 4 ms |
| ESP-NOW air to pendant | 2 ms | 3 ms |
| **Total (CNC position → pendant screen)** | **~32 ms** | **~61 ms** |

**Before optimisations (worst case via M409):** ~172 ms (120 ms poll wait + 15.6 ms serial + 8 ms ESP-NOW relay + 3 ms air).

### Key motion state refresh rates

| State | Before | After | Change |
|---|---|---|---|
| WCS position | 120 ms worst-case poll | **50 ms worst-case poll** | ×2.4 faster poll |
| Machine absolute position | 120 ms (M409 every tick) | ~950 ms (M409 every 19 ticks) | ↓ but still adequate |
| Machine status (idle/running/…) | 360 ms | **100 ms** | ×3.6 |
| Feed rate / speed factor | 360 ms | **100 ms** | ×3.6 |
| Spindle RPM | **1,560 ms** | **150 ms** | **×10.4** |
| Round-robin relay (all categories) | 1,200 ms | **250 ms** | ×4.8 |

> **Note on machine absolute position:** M114 only returns WCS coordinates. Machine-absolute position and homed/limit data are still refreshed by `M409 K"move.axes[]"` every ~950 ms. If absolute machine position must be displayed at high frequency, consider adding a separate periodic M409 on every Nth tick (e.g. ÷5 = 250 ms).

---

## Serial Bandwidth Budget (after changes)

At 115200 baud the available bandwidth is **11,520 B/s**.

| Command | TX bytes | RX bytes | Rate | B/s |
|---|---|---|---|---|
| M114 every 50 ms | 7 | 35 | 20 Hz | 840 |
| M409 spindles every 150 ms | 35 | 80 | 6.7 Hz | 767 |
| M409 JOB_STATUS (×3) every 100 ms | 105 | 150 | 10 Hz | 2,550 |
| M409 move.axes every 950 ms | 45 | 180 | 1 Hz | 237 |
| Other polls (avg) | — | — | — | ~200 |
| **Total** | | | | **~4,600 B/s (40%)** |

40% utilisation leaves comfortable headroom.  For reference, the STM32H723 at 550 MHz can process hundreds of M114 and M409 queries per second; UART bandwidth is the only real constraint.

---

## CNC Controller (STM32H723VGT6) Capacity Estimate

| Command | CNC CPU cost | Response size | Max rate (serial-limited) |
|---|---|---|---|
| `M114` | ~20 µs | ~35 B | ~250/s |
| `M409 K"move.axes[]"` | ~200 µs | ~180 B | ~64/s |
| `M409 K"spindles[]"` | ~100 µs | ~80 B | ~144/s |
| `M409 K"state.status"` | ~50 µs | ~25 B | ~454/s |

Current combined load after optimisations: **~4,600 B/s = 40% of UART capacity**.  
Safe ceiling (60% target): ~6,900 B/s — still ~50% headroom before any motion impact.

---

## Remaining Optimisation Opportunities (not yet implemented)

| Change | Expected gain | Effort |
|---|---|---|
| Separate 20 Hz M114 task (decoupled from main poll timer) | Position latency: ~60 ms → ~20 ms worst case | Large |
| UART baud → 460800 | All serial RTTs ÷4, bandwidth ×4 | Medium + wiring check |
| Skip poll injection when jog queue has pending user commands | Jog worst-case -2 ms | Small |
| Machine-absolute position at ÷5 (250 ms) via M409 | Absolute pos refresh 950→250 ms | 1 line |
