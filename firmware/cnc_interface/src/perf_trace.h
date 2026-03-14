#pragma once
// perf_trace.h — Lightweight block-timing and task-flow trace macros.
//
// ---------------------------------------------------------------------------
// Quick start
// ---------------------------------------------------------------------------
// Enable by adding  -D PERF_TRACE_ENABLED=1  to your build_flags.
// When the flag is absent (or 0) every macro compiles to a strict no-op;
// there is zero runtime overhead and no extra includes are pulled in.
//
// ---------------------------------------------------------------------------
// Timing macros  (µs precision via esp_timer_get_time on HW, CLOCK_MONOTONIC
//                 on POSIX/native simulation)
// ---------------------------------------------------------------------------
//
//   PERF_BEGIN(name)
//       Declares a local  int64_t _pt_<name>  and captures the current time.
//       "name" must be a valid C identifier (not a string).
//
//   PERF_END(tag, name)
//       Logs elapsed since PERF_BEGIN at VERBOSE level.
//
//   PERF_END_D(tag, name)
//       Logs elapsed at DEBUG level.
//
//   PERF_END_I(tag, name)
//       Logs elapsed at INFO level (always visible at default log level).
//
//   PERF_SLOWLOG(tag, name, threshold_us)
//       Logs at ERROR level ONLY when elapsed >= threshold_us.
//       Stay silent otherwise (no regular per-iteration noise).
//       Use this for "should never be this slow" checks.
//
// ---------------------------------------------------------------------------
// Task-flow markers
// ---------------------------------------------------------------------------
//
//   TRACE_TASK_WAKE(tag)
//       Log "[TRACE] WAKE tick=N" at DEBUG level — call at the top of a
//       task's while-loop body (after the task unblocks from vTaskDelay /
//       queue wait / semaphore, etc.).
//
//   TRACE_TASK_SLEEP(tag, delay_ms)
//       Log "[TRACE] SLEEP ~Nms" at DEBUG level — call immediately before
//       the blocking call (vTaskDelay, xQueueReceive, etc.).
//
// ---------------------------------------------------------------------------
// Typical usage pattern
// ---------------------------------------------------------------------------
//
//   void my_task(void *) {
//       while (true) {
//           TRACE_TASK_WAKE(TAG);
//
//           PERF_BEGIN(phase_a);
//           do_phase_a();
//           PERF_SLOWLOG(TAG, phase_a, 5000);   // warn if > 5 ms
//
//           PERF_BEGIN(phase_b);
//           do_phase_b();
//           PERF_END_D(TAG, phase_b);           // always log at DEBUG
//
//           uint32_t sleep_ms = compute_sleep();
//           TRACE_TASK_SLEEP(TAG, sleep_ms);
//           vTaskDelay(pdMS_TO_TICKS(sleep_ms));
//       }
//   }
//
// ---------------------------------------------------------------------------

#ifndef PERF_TRACE_H
#define PERF_TRACE_H

#include "debug.h"

// ── Time source ──────────────────────────────────────────────────────────────
#if defined(ESP32_HW)
#  include "esp_timer.h"
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  define _PT_NOW()   esp_timer_get_time()              // µs since boot
#  define _PT_TICK()  ((unsigned)xTaskGetTickCount())
#else
#  include <time.h>
#  ifdef __cplusplus
extern "C" {
#  endif
static inline int64_t _pt_now_posix(void) {
    struct timespec _ts;
    clock_gettime(CLOCK_MONOTONIC, &_ts);
    return (int64_t)_ts.tv_sec * 1000000LL + (int64_t)(_ts.tv_nsec / 1000);
}
#  ifdef __cplusplus
}
#  endif
#  define _PT_NOW()   _pt_now_posix()
#  define _PT_TICK()  0u
#endif

// ── Active macros (PERF_TRACE_ENABLED != 0) ──────────────────────────────────
#if defined(PERF_TRACE_ENABLED) && PERF_TRACE_ENABLED

// Start a timer.  Declares  int64_t _pt_<name> = current_time_us.
#define PERF_BEGIN(name) \
    int64_t _pt_##name = _PT_NOW()

#undef LOGV
#define LOGV LOGE

#undef LOGD
#define LOGD LOGE

#undef LOGI
#define LOGI LOGE

// Log elapsed since PERF_BEGIN at VERBOSE level.
#define PERF_END(tag, name) \
    LOGV(tag, "[PERF] " #name " %u us", (uint32_t)(_PT_NOW() - _pt_##name))

// Log elapsed at DEBUG level.
#define PERF_END_D(tag, name) \
    LOGD(tag, "[PERF] " #name " %u us", (uint32_t)(_PT_NOW() - _pt_##name))

// Log elapsed at INFO level (always visible at the default D_INFO log level).
#define PERF_END_I(tag, name) \
    LOGI(tag, "[PERF] " #name " %u us", (uint32_t)(_PT_NOW() - _pt_##name))

// Log at ERROR level ONLY when elapsed >= threshold_us, silent otherwise.
// This is the recommended macro for "should not be slow" paths: zero noise
// on fast iterations, loud on pathological cases.
#define PERF_SLOWLOG(tag, name, threshold_us) \
    do { \
        uint32_t _pt_el_##name = (uint32_t)(_PT_NOW() - _pt_##name); \
        if (_pt_el_##name >= (uint32_t)(threshold_us)) { \
            LOGE(tag, "[PERF] SLOW " #name " %u us (threshold %u us)", \
                 _pt_el_##name, (uint32_t)(threshold_us)); \
        } else { \
            LOGV(tag, "[PERF] " #name " %u us", _pt_el_##name); \
        } \
    } while (0)

// Log task wakeup (call at the top of the task loop body).
#define TRACE_TASK_WAKE(tag) \
    LOGD(tag, "[TRACE] WAKE tick=%u", _PT_TICK())

// Log that the task is about to block (call just before the blocking call).
#define TRACE_TASK_SLEEP(tag, delay_ms) \
    LOGD(tag, "[TRACE] SLEEP ~%u ms tick=%u", (unsigned)(delay_ms), _PT_TICK())

// ── Disabled macros — all expand to ((void)0) ────────────────────────────────
#else

#define PERF_BEGIN(name)                        ((void)0)
#define PERF_END(tag, name)                     ((void)0)
#define PERF_END_D(tag, name)                   ((void)0)
#define PERF_END_I(tag, name)                   ((void)0)
#define PERF_SLOWLOG(tag, name, threshold_us)   ((void)0)
#define TRACE_TASK_WAKE(tag)                    ((void)0)
#define TRACE_TASK_SLEEP(tag, delay_ms)         ((void)0)

#endif  // PERF_TRACE_ENABLED

#endif  // PERF_TRACE_H
