/**
 * @file jog_accumulator_task.h
 * @brief Adaptive, accumulating jog/pendant mode for encoder-driven CNC jog.
 *
 * DESIGN
 * ------
 * Rather than posting one G-Code move per encoder click, this module
 * collects ("accumulates") encoder clicks into a small FreeRTOS queue and
 * dispatches them in batches from a dedicated high-priority task.
 *
 * The task runs the following loop:
 *   1. Block on the click queue (up to JOG_ACCUM_INITIAL_WAIT_MS).
 *   2. On first click arrival, non-blocking drain remaining queue items and
 *      sum them into `total_steps`.
 *   3. If net steps == 0 or no axis is selected, discard and loop back.
 *   4. Send ONE G-Code move for the full accumulated distance via
 *      machine_interface_step_current_axis().
 *   5. Estimate the real-world move duration using a trapezoidal/triangle
 *      velocity profile:
 *        v_max = feed_mm_min / 60
 *        crossover = v_max² / accel         (min dist for full trapezoid)
 *        if dist >= crossover:  t = dist/v_max + v_max/accel   (trapezoid)
 *        else:                  t = 2 * sqrt(dist/accel)        (triangle)
 *   6. Sleep for (t_ms − JOG_ACCUM_LEAD_AHEAD_MS), clamped to
 *      [JOG_ACCUM_MIN_SLEEP_MS, JOG_ACCUM_MAX_SLEEP_MS].
 *      While sleeping, encoder clicks accumulate in the queue again.
 *   7. goto 1.
 *
 * The sleep phase is the key: it lets clicks accumulate so the *next*
 * iteration naturally bundles more steps at higher scroll speeds — a
 * self-regulating steady state.
 *
 * CONFIG MACROS (define before including or in config.h)
 * -------------------------------------------------------
 *  JOG_ACCUM_FEED_XY_MM_MIN  — Jog feed rate for X/Y (mm/min).    Default 3000.
 *  JOG_ACCUM_FEED_Z_MM_MIN   — Jog feed rate for Z (mm/min).       Default 1000.
 *  JOG_ACCUM_ACCEL_X_MM_S2   — X-axis acceleration (mm/s²).        Default 500.
 *  JOG_ACCUM_ACCEL_Y_MM_S2   — Y-axis acceleration (mm/s²).        Default 500.
 *  JOG_ACCUM_ACCEL_Z_MM_S2   — Z-axis acceleration (mm/s²).        Default 200.
 *  JOG_ACCUM_LEAD_AHEAD_MS   — Wake up this many ms before move ends so new
 *                               clicks have time to arrive.       Default 30.
 *  JOG_ACCUM_MIN_SLEEP_MS    — Floor on computed sleep.           Default 10.
 *  JOG_ACCUM_MAX_SLEEP_MS    — Ceiling on computed sleep (safety). Default 500.
 *  JOG_ACCUM_INITIAL_WAIT_MS — How long to block waiting for the first click
 *                               before looping back.              Default 100.
 *  JOG_ACCUM_QUEUE_LEN       — Depth of the click queue.          Default 64.
 *  JOG_ACCUM_TASK_STACK      — FreeRTOS task stack (bytes).       Default 2048.
 *  JOG_ACCUM_TASK_PRIO       — FreeRTOS task priority.            Default tskIDLE_PRIORITY+4.
 *  JOG_ACCUM_TASK_CORE       — Pinned core (-1 = any).            Default 0.
 */

#ifndef JOG_ACCUMULATOR_TASK_H
#define JOG_ACCUMULATOR_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "machine/machine_interface.h"

/* -----------------------------------------------------------------------
 * Config defaults — override via build flags or config.h before including.
 * ---------------------------------------------------------------------- */

#ifndef JOG_ACCUM_FEED_XY_MM_MIN
#define JOG_ACCUM_FEED_XY_MM_MIN  7000.0f
#endif

#ifndef JOG_ACCUM_FEED_Z_MM_MIN
#define JOG_ACCUM_FEED_Z_MM_MIN   1000.0f
#endif

/** Per-axis acceleration (mm/s²). Typical RRF/Duet default is ~500 for XY, ~200 for Z. */
#ifndef JOG_ACCUM_ACCEL_X_MM_S2
#define JOG_ACCUM_ACCEL_X_MM_S2   1000.0f
#endif

#ifndef JOG_ACCUM_ACCEL_Y_MM_S2
#define JOG_ACCUM_ACCEL_Y_MM_S2   1000.0f
#endif

#ifndef JOG_ACCUM_ACCEL_Z_MM_S2
#define JOG_ACCUM_ACCEL_Z_MM_S2   200.0f
#endif

/**
 * How many ms before move completion the task wakes up.  Waking early lets
 * any encoder clicks that arrived during the sleep be batched into the next
 * iteration without a visible gap.
 */
#ifndef JOG_ACCUM_LEAD_AHEAD_MS
#define JOG_ACCUM_LEAD_AHEAD_MS   30
#endif

/** Minimum sleep after a move (gives the queue time to fill for the next batch). */
#ifndef JOG_ACCUM_MIN_SLEEP_MS
#define JOG_ACCUM_MIN_SLEEP_MS    5
#endif

/** Safety ceiling on computed sleep to stop the task stalling after a large move. */
#ifndef JOG_ACCUM_MAX_SLEEP_MS
#define JOG_ACCUM_MAX_SLEEP_MS    100
#endif
/**
 * Enable kinematic motion estimation.
 *
 * When 1 (default): after dispatching a G-Code move the task sleeps for the
 * estimated move duration (trapezoidal/triangle profile) minus
 * JOG_ACCUM_LEAD_AHEAD_MS.  This is the self-regulating "smart" mode.
 *
 * When 0: the task dispatches the accumulated move and immediately goes back
 * to waiting for the next batch with no sleep.  Simpler, and useful as a
 * baseline or when the machine's real acceleration/feed values are unknown.
 * The encoder-diff queue and step-batching are still active in this mode.
 *
 * Override at build time: -DJOG_ACCUM_MOTION_ESTIMATION=0
 */
#ifndef JOG_ACCUM_MOTION_ESTIMATION
#define JOG_ACCUM_MOTION_ESTIMATION 1
#endif
/** How long (ms) to block waiting for the very first click before looping back. */
#ifndef JOG_ACCUM_INITIAL_WAIT_MS
#define JOG_ACCUM_INITIAL_WAIT_MS 10
#endif

/** Number of click items the internal queue can hold before overflowing. */
#ifndef JOG_ACCUM_QUEUE_LEN
#define JOG_ACCUM_QUEUE_LEN       384
#endif

#ifndef JOG_ACCUM_TASK_STACK
#define JOG_ACCUM_TASK_STACK      4096
#endif

#ifndef JOG_ACCUM_TASK_PRIO
/* Higher than LVGL task (IDLE+2) but below machine send task (IDLE+5). */
#define JOG_ACCUM_TASK_PRIO       (tskIDLE_PRIORITY + 4)
#endif

/** Core to pin the task to.  -1 = unpin (runs on either core). */
#ifndef JOG_ACCUM_TASK_CORE
#define JOG_ACCUM_TASK_CORE       0
#endif

/* -----------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @brief Initialise the accumulator and create the FreeRTOS task.
 *
 * Must be called once after the machine interface is initialised and the
 * G-Code queue has been assigned.
 *
 * @param machine  Pointer to the active machine interface.
 * @return true on success, false if queue or task creation fails.
 */
bool jog_accumulator_init(machine_interface_t *machine);

/**
 * @brief Post encoder click(s) to the accumulator queue.
 *
 * Safe to call from any context (ISR or task).  A positive diff means
 * movement in the positive axis direction; negative means negative direction.
 *
 * Diffs larger than INT8_MAX / smaller than INT8_MIN are automatically split
 * into multiple INT8-sized queue items so no information is lost even when
 * the encoder driver reports large deltas in a single call.
 *
 * @param diff  Number of encoder steps (positive or negative).
 */
void jog_accumulator_post_clicks(int32_t diff);

/**
 * @brief Return true if the accumulator has been successfully initialised.
 */
bool jog_accumulator_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* JOG_ACCUMULATOR_TASK_H */
