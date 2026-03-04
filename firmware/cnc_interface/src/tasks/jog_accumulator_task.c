/**
 * @file jog_accumulator_task.c
 *
 * Adaptive, accumulating jog mode — see jog_accumulator_task.h for the full
 * design description.
 *
 * Only compiled on ESP32 hardware targets where FreeRTOS is available.
 */

#define UI_DEBUG_LOCAL_LEVEL D_INFO
#include "debug.h"

#include "jog_accumulator_task.h"

#ifdef ESP32_HW

#include <math.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/task_registry.h"

#include "config/app_settings.h"

/* Convenience wrappers so the hot path stays readable */
#define s_feed_xy() \
    app_settings_get_float(APP_SETTINGS_GROUP_JOG, APP_SETTINGS_JOG_FEED_XY)
#define s_feed_z()  \
    app_settings_get_float(APP_SETTINGS_GROUP_JOG, APP_SETTINGS_JOG_FEED_Z)
#define s_accel_x() \
    app_settings_get_float(APP_SETTINGS_GROUP_JOG, APP_SETTINGS_JOG_ACCEL_X)
#define s_accel_y() \
    app_settings_get_float(APP_SETTINGS_GROUP_JOG, APP_SETTINGS_JOG_ACCEL_Y)
#define s_accel_z() \
    app_settings_get_float(APP_SETTINGS_GROUP_JOG, APP_SETTINGS_JOG_ACCEL_Z)
#define s_lead_ahead_ms() \
    ((int)app_settings_get_int(APP_SETTINGS_GROUP_JOG, APP_SETTINGS_JOG_LEAD_AHEAD_MS))
#define s_min_sleep_ms() \
    ((int)app_settings_get_int(APP_SETTINGS_GROUP_JOG, APP_SETTINGS_JOG_MIN_SLEEP_MS))
#define s_max_sleep_ms() \
    ((int)app_settings_get_int(APP_SETTINGS_GROUP_JOG, APP_SETTINGS_JOG_MAX_SLEEP_MS))

static const char *TAG = "jog_accum";

/* -----------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

static machine_interface_t *s_machine   = NULL;
static QueueHandle_t         s_queue    = NULL;
static TaskHandle_t          s_task     = NULL;
static volatile bool         s_running  = false;

#if JOG_ACCUM_MOTION_ESTIMATION
/* -----------------------------------------------------------------------
 * Kinematics helper
 *
 * Estimates the wall-clock time (ms) a simple linear jog move will take,
 * assuming a symmetric trapezoidal velocity profile.
 *
 *   v_max  = feed (mm/min) / 60            [mm/s]
 *   a      = accel (mm/s²)  — per-axis value passed by caller
 *
 * Crossover distance d_c = v_max² / a:
 *   d >= d_c  →  trapezoid:  t = d / v_max  +  v_max / a
 *   d <  d_c  →  triangle:   t = 2 * sqrt(d / a)
 *
 * Returns duration in milliseconds.
 * ---------------------------------------------------------------------- */
static float _estimate_move_ms(float dist_mm, float feed_mm_per_min,
                                float accel_mm_s2) {
    if (dist_mm <= 0.0f || feed_mm_per_min <= 0.0f || accel_mm_s2 <= 0.0f) {
        return (float)JOG_ACCUM_MIN_SLEEP_MS;
    }

    const float v_max_mms = feed_mm_per_min / 60.0f;    /* mm/s */
    const float a         = accel_mm_s2;                /* mm/s² */
    const float crossover = (v_max_mms * v_max_mms) / a; /* mm  */

    float t_s;
    if (dist_mm >= crossover) {
        /* Full trapezoidal profile */
        t_s = dist_mm / v_max_mms + v_max_mms / a;
    } else {
        /* Triangle profile — never reaches v_max */
        t_s = 2.0f * sqrtf(dist_mm / a);
    }

    return t_s * 1000.0f; /* → ms */
}
#endif /* JOG_ACCUM_MOTION_ESTIMATION */

/* -----------------------------------------------------------------------
 * Task body
 * ---------------------------------------------------------------------- */
static void _jog_accumulator_task(void *pv) {
    (void)pv;

/* Avoid expensive floating-point conversion at task startup (newlib's
 * dtoa) which can push this small task stack over the guard. Print
 * rounded integer values instead. */
#if JOG_ACCUM_MOTION_ESTIMATION
    LOGI(TAG, "Jog accumulator task started [motion-estimation ON] "
         "(feed_xy=%d feed_z=%d accel_x=%d accel_y=%d accel_z=%d "
         "lead=%d min=%d max=%d)",
         (int)lroundf(s_feed_xy()),
         (int)lroundf(s_feed_z()),
         (int)lroundf(s_accel_x()),
         (int)lroundf(s_accel_y()),
         (int)lroundf(s_accel_z()),
         s_lead_ahead_ms(),
         s_min_sleep_ms(),
         s_max_sleep_ms());
#else
    LOGI(TAG, "Jog accumulator task started [motion-estimation OFF] "
         "(feed_xy=%d feed_z=%d)",
         (int)lroundf(s_feed_xy()),
         (int)lroundf(s_feed_z()));
#endif

    for (;;) {
        /* ----------------------------------------------------------------
         * 1. Block waiting for the first click of a new batch.
         * -------------------------------------------------------------- */
        int8_t click = 0;
        BaseType_t got = xQueueReceive(s_queue, &click,
                                       pdMS_TO_TICKS(JOG_ACCUM_INITIAL_WAIT_MS));
        if (got != pdTRUE) {
            /* Nothing arrived — idle, loop back. */
            continue;
        }

        /* ----------------------------------------------------------------
         * 2. Non-blocking drain: collect every click currently in queue.
         * -------------------------------------------------------------- */
        int32_t total = (int32_t)click;  /* widen with sign extension */
        int8_t  extra = 0;
        while (xQueueReceive(s_queue, &extra, 0) == pdTRUE) {
            total += (int32_t)extra;
        }

        /* ----------------------------------------------------------------
         * 3. Skip if net movement is zero or no axis is selected.
         * -------------------------------------------------------------- */
        if (total == 0) {
            continue;
        }

        axis_t axis = s_machine->current_move_axis;
        if (axis == AXIS_OFF) {
            /* Axis not selected — discard accumulated clicks silently.  A
             * pile-up here would cause a sudden lurch when the user first
             * selects an axis, so we drain and throw them away.            */
            LOGV(TAG, "AXIS_OFF — discarding %d accumulated clicks", (int)total);
            continue;
        }

        /* ----------------------------------------------------------------
         * 4. Determine feed for the active axis and dispatch ONE G-Code.
         * -------------------------------------------------------------- */
        float feed_mm_min;
        float accel_mm_s2;
        float step_size;
        switch (axis) {
            case AXIS_X:
                feed_mm_min = s_feed_xy();
                accel_mm_s2 = s_accel_x();
                step_size   = s_machine->current_move_step_xy;
                break;
            case AXIS_Y:
                feed_mm_min = s_feed_xy();
                accel_mm_s2 = s_accel_y();
                step_size   = s_machine->current_move_step_xy;
                break;
            case AXIS_Z:
            default:
                feed_mm_min = s_feed_z();
                accel_mm_s2 = s_accel_z();
                step_size   = s_machine->current_move_step_z;
                break;
        }
        float   dist_mm = fabsf((float)total * step_size);

        LOGI(TAG, "Dispatching jog: axis=%d steps=%d dist=%.4fmm feed=%.0f accel=%.0f",
             (int)axis, (int)total, (double)dist_mm, (double)feed_mm_min,
             (double)accel_mm_s2);

        machine_interface_step_current_axis(s_machine, feed_mm_min, total);

        /* ----------------------------------------------------------------
         * 5. Sleep until (approximately) the move finishes.
         *
         * Motion-estimation mode: derive sleep from a trapezoidal/triangle
         * kinematic model so the task wakes up just before the move ends,
         * allowing new clicks to accumulate for the next batch.
         *
         * Simple mode: no sleep — the task immediately loops back and blocks
         * on the queue again, relying solely on click accumulation during the
         * natural latency of the G-Code dispatch path.
         * -------------------------------------------------------------- */
#if JOG_ACCUM_MOTION_ESTIMATION
        float t_move_ms = _estimate_move_ms(dist_mm, feed_mm_min, accel_mm_s2);

        int32_t sleep_ms = (int32_t)(t_move_ms) - s_lead_ahead_ms();

        if (sleep_ms < s_min_sleep_ms()) {
            sleep_ms = s_min_sleep_ms();
        }
        if (sleep_ms > s_max_sleep_ms()) {
            sleep_ms = s_max_sleep_ms();
        }

        LOGV(TAG, "  estimated move=%.1fms sleep=%dms", (double)t_move_ms, (int)sleep_ms);

        vTaskDelay(pdMS_TO_TICKS((uint32_t)sleep_ms));
#else
        /* Simple mode: yield once so higher-priority tasks stay responsive,
         * then immediately drain the queue for the next batch.             */
        taskYIELD();
#endif /* JOG_ACCUM_MOTION_ESTIMATION */
    }

    /* Should never reach here */
    LOGE(TAG, "Jog accumulator task unexpectedly exiting");
    s_running = false;
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool jog_accumulator_init(machine_interface_t *machine) {
    if (!machine) {
        LOGE(TAG, "jog_accumulator_init: NULL machine pointer");
        return false;
    }

    if (s_running) {
        LOGW(TAG, "jog_accumulator_init: already initialised");
        return true;
    }

    s_machine = machine;

    /* Create the click queue — item type is int8_t. */
    s_queue = xQueueCreate(JOG_ACCUM_QUEUE_LEN, sizeof(int8_t));
    if (!s_queue) {
        LOGE(TAG, "Failed to create jog accumulator click queue");
        return false;
    }

    /* Spawn the task. */
#if JOG_ACCUM_TASK_CORE >= 0
    BaseType_t rc = xTaskCreatePinnedToCore(
        _jog_accumulator_task,
        "jog_accum",
        JOG_ACCUM_TASK_STACK,
        NULL,
        JOG_ACCUM_TASK_PRIO,
        &s_task,
        JOG_ACCUM_TASK_CORE);
#else
    BaseType_t rc = xTaskCreate(
        _jog_accumulator_task,
        "jog_accum",
        JOG_ACCUM_TASK_STACK,
        NULL,
        JOG_ACCUM_TASK_PRIO,
        &s_task);
#endif

    if (rc != pdPASS) {
        LOGE(TAG, "Failed to create jog accumulator task (err %d)", (int)rc);
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }

    s_running = true;
    task_registry_register_handle(s_task, "jog_accum");
    LOGI(TAG, "Jog accumulator initialised (queue_len=%d prio=%d core=%d)",
         JOG_ACCUM_QUEUE_LEN, JOG_ACCUM_TASK_PRIO, JOG_ACCUM_TASK_CORE);
    return true;
}

void jog_accumulator_post_clicks(int32_t diff) {
    if (!s_running || !s_queue || diff == 0) {
        return;
    }

    /* Split diffs that exceed int8_t range into multiple queue items so no
     * information is lost even when the encoder driver reports large deltas. */
    while (diff != 0) {
        int8_t chunk;
        if (diff > INT8_MAX) {
            chunk = INT8_MAX;
        } else if (diff < INT8_MIN) {
            chunk = INT8_MIN;
        } else {
            chunk = (int8_t)diff;
        }
        diff -= chunk;

        /* Post without blocking; if the queue is full, drop the remainder.
         * A full queue means the machine is receiving moves faster than it
         * can execute them — dropping is better than stalling the LVGL task. */
        BaseType_t sent = xQueueSend(s_queue, &chunk, 0);
        if (sent != pdTRUE) {
            LOGW(TAG, "Jog click queue full — dropping %d remaining clicks", (int)(diff + chunk));
            break;
        }
    }
}

bool jog_accumulator_is_running(void) {
    return s_running;
}

#else /* !ESP32_HW — stub for native/sim builds */

bool jog_accumulator_init(machine_interface_t *machine) {
    (void)machine;
    return false;
}

void jog_accumulator_post_clicks(int32_t diff) {
    (void)diff; /* no-op in sim/native builds */
}

bool jog_accumulator_is_running(void) {
    return false;
}

#endif /* ESP32_HW */
