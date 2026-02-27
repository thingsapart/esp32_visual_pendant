// mdi_handler.c  —  MDI (Manual Data Input) backend implementation

#include "ui/mdi_handler.h"

#include <stdio.h>
#include <string.h>
// For case-insensitive comparisons if needed in future
#include <strings.h>

#include "debug.h"
#include "lvgl_ui.h"   // data_binding_notify_state_changed, obj_registry_get

// PSRAM allocation on ESP32 hardware with PSRAM enabled.
#if defined(ESP32_HW) && defined(BOARD_HAS_PSRAM)
#  include "esp_heap_caps.h"
#  define MDI_MALLOC(sz)  heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#  define MDI_MALLOC(sz)  malloc(sz)
#endif

static const char *TAG = "MDI";

// ---------------------------------------------------------------------------
// Predefined G-code shortcut table
//
// Each entry maps the suffix after "MDI.gcode." to a fixed G-code string.
// Add / edit rows here to change what the shortcut buttons send.
// ---------------------------------------------------------------------------
static const struct {
    const char *key;
    const char *gcode;
} s_gcode_table[] = {
    // ---- Tool commands ----
    { "T0",         "T0 M6"                        },   // Select tool 0 + execute tool-change macro
    { "probe_tool", "M98 P\"/macros/probe_tool.g\"" },  // Run tool-length probe macro
    { "M6",         "M6"                           },   // Tool change (with last T)

    // ---- Spindle ----
    { "M3",         "M3 S1000"                     },   // Spindle CW  (default 1000 RPM)
    { "M4",         "M4 S1000"                     },   // Spindle CCW
    { "M5",         "M5"                           },   // Spindle stop

    // ---- Coolant / Pause ----
    { "M8",         "M8"                           },   // Coolant on
    { "M9",         "M9"                           },   // Coolant off
    { "M0",         "M0"                           },   // Unconditional pause

    // ---- Coordinate modes ----
    { "G90",        "G90"                          },   // Absolute positioning
    { "G91",        "G91"                          },   // Relative positioning
    { "G92",        "G92 X0 Y0 Z0"                 },   // Set current position as WCS origin

    // ---- Homing / TLO ----
    { "G28",        "G28"                          },   // Go to machine home (all axes)
    { "G28Z",       "G28 Z0"                       },   // Home Z only
    { "G49",        "G49"                          },   // Cancel tool-length offset

    // ---- Units / Plane ----
    { "G20",        "G20"                          },   // Inch units
    { "G21",        "G21"                          },   // Metric units
    { "G17",        "G17"                          },   // XY working plane

    // ---- Status / Emergency ----
    { "M119",       "M119"                         },   // Report endstop states
    { "M112",       "M112"                         },   // Emergency stop
    { "M30",        "M30"                          },   // End program / return to top
};
#define GCODE_TABLE_SIZE  (sizeof(s_gcode_table) / sizeof(s_gcode_table[0]))

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * Append @p msg (+ newline) to the ring-buffer.
 * If the new content would exceed MDI_LOG_BUF_SIZE, the oldest characters
 * are discarded to make room — the buffer always remains NUL-terminated.
 *
 * Called from both machine-thread (mdi_handler_on_log) and LVGL-task
 * (mdi_send_gcode echo).  The former only sets flags; the latter is the
 * only caller that writes to the buffer from the LVGL task.  In practice
 * the machine thread also appends, so writes are not serialised — but given
 * that this is a log display buffer the worst case is a minor visual glitch,
 * and a mutex would add FreeRTOS overhead in the hot RX path.
 */
static void _log_append(mdi_handler_t *mdi, const char *msg)
{
    if (!mdi->log_buf || !msg) return;
    int msg_len = (int)strlen(msg);
    int needed  = mdi->log_len + msg_len + 2; /* +1 newline  +1 NUL */

    if (needed > MDI_LOG_BUF_SIZE) {
        /* Drop the oldest characters to make room. */
        int drop = needed - MDI_LOG_BUF_SIZE;
        if (drop >= mdi->log_len) {
            mdi->log_len = 0;
        } else {
            memmove(mdi->log_buf, mdi->log_buf + drop, mdi->log_len - drop);
            mdi->log_len -= drop;
        }
    }

    memcpy(mdi->log_buf + mdi->log_len, msg, msg_len);
    mdi->log_len += msg_len;
    mdi->log_buf[mdi->log_len++] = '\n';
    mdi->log_buf[mdi->log_len]   = '\0';
}

/** Notify data-binding of the current is_busy state (LVGL task). */
static void _notify_busy(mdi_handler_t *mdi)
{
    data_binding_notify_state_changed(
        "MDI.is_busy",
        (binding_value_t){.type = BINDING_TYPE_BOOL, .as.b_val = mdi->is_busy});
}

/** Busy-timeout LVGL timer callback — re-enables the MDI UI. */
static void _busy_timeout_cb(lv_timer_t *timer)
{
    mdi_handler_t *mdi = (mdi_handler_t *)lv_timer_get_user_data(timer);
    if (!mdi) return;
    LOGW(TAG, "MDI in-flight command timed out — re-enabling UI");
    mdi->is_busy              = false;
    mdi->busy_timeout_timer   = NULL;  /* timer auto-deletes itself (repeat_count=1) */
    _notify_busy(mdi);
}

/**
 * Set/clear the busy state and (re)arm / cancel the timeout timer.
 * Must be called from the LVGL task.
 */
static void _set_busy(mdi_handler_t *mdi, bool busy)
{
    mdi->is_busy = busy;

    if (busy) {
        if (mdi->busy_timeout_timer) {
            lv_timer_reset(mdi->busy_timeout_timer);
        } else {
            mdi->busy_timeout_timer = lv_timer_create(
                _busy_timeout_cb, MDI_BUSY_TIMEOUT_MS, mdi);
            if (mdi->busy_timeout_timer)
                lv_timer_set_repeat_count(mdi->busy_timeout_timer, 1);
        }
    } else {
        if (mdi->busy_timeout_timer) {
            lv_timer_delete(mdi->busy_timeout_timer);
            mdi->busy_timeout_timer = NULL;
        }
    }

    _notify_busy(mdi);
}

/**
 * Send a G-code command: echo to log, send to machine, mark UI as busy.
 * Must be called from the LVGL task.
 */
static void _send_gcode(mdi_handler_t *mdi, const char *gcode)
{
    if (!mdi->machine || !gcode || !gcode[0]) return;

    /* Refuse to send while disconnected.  This avoids silently dropping
     * the command AND prevents _set_busy() from firing data-binding
     * notifications (which update LVGL widgets) when we will never
     * receive the machine's "ok" acknowledgement. */
    if (mdi->machine->is_connected && !mdi->machine->is_connected(mdi->machine)) {
        _log_append(mdi, "! Not connected to machine");
        mdi->log_dirty = true;
        return;
    }

    /* Echo the sent command into the log so the operator can see it. */
    char echo[270];
    snprintf(echo, sizeof(echo), "> %s", gcode);
    _log_append(mdi, echo);
    mdi->log_dirty = true;

    mdi->machine->send_gcode(mdi->machine, gcode, 0);
    _set_busy(mdi, true);
}

/**
 * LV_EVENT_READY callback on the input textarea.
 * Fires when the LVGL keyboard's Enter / Confirm key is pressed.
 */
static void _input_ready_cb(lv_event_t *e)
{
    mdi_handler_t *mdi = (mdi_handler_t *)lv_event_get_user_data(e);
    if (!mdi) return;

    lv_obj_t *ta = lv_event_get_target(e);
    const char *text = lv_textarea_get_text(ta);
    if (!text || !text[0]) return;

    _send_gcode(mdi, text);
    lv_textarea_set_text(ta, "");
}

// ---------------------------------------------------------------------------
// Public API — lifecycle
// ---------------------------------------------------------------------------

void mdi_handler_init(mdi_handler_t *mdi, machine_interface_t *machine)
{
    memset(mdi, 0, sizeof(*mdi));
    mdi->machine = machine;

    /* Allocate log ring-buffer (PSRAM if available). */
    mdi->log_buf = (char *)MDI_MALLOC(MDI_LOG_BUF_SIZE);
    if (!mdi->log_buf) {
        LOGE(TAG, "Failed to allocate MDI log buffer (%d bytes)", MDI_LOG_BUF_SIZE);
        return;
    }
    mdi->log_buf[0] = '\0';
    mdi->log_len    = 0;

    /* Resolve widget references created during create_ui(). */
    mdi->input_field = (lv_obj_t *)obj_registry_get("mdi_input_field");
    mdi->log_area    = (lv_obj_t *)obj_registry_get("mdi_log_area");

    if (mdi->input_field) {
        /* Hook the LVGL keyboard Enter/Confirm key. */
        lv_obj_add_event_cb(mdi->input_field, _input_ready_cb,
                             LV_EVENT_READY, mdi);
    } else {
        LOGW(TAG, "'mdi_input_field' not found in obj_registry — "
                  "Enter-key sending will not work");
    }

    if (!mdi->log_area) {
        LOGW(TAG, "'mdi_log_area' not found in obj_registry");
    }

    LOGI(TAG, "MDI handler initialised (log buf %d bytes, %s)",
         MDI_LOG_BUF_SIZE,
#if defined(ESP32_HW) && defined(BOARD_HAS_PSRAM)
         "PSRAM"
#else
         "heap"
#endif
    );
}

void mdi_handler_deinit(mdi_handler_t *mdi)
{
    if (!mdi) return;
    if (mdi->busy_timeout_timer) {
        lv_timer_delete(mdi->busy_timeout_timer);
        mdi->busy_timeout_timer = NULL;
    }
    if (mdi->log_buf) {
        free(mdi->log_buf);
        mdi->log_buf = NULL;
    }
}

// ---------------------------------------------------------------------------
// Public API — runtime hooks
// ---------------------------------------------------------------------------

bool mdi_handler_on_log(mdi_handler_t *mdi, const char *message)
{
    if (!mdi || !message) return false;

    /* Suppress noisy "Error: Bad command:" lines from the MDI view.
     * This string may appear with different capitalization for 'command',
     * so check both common variants. These lines are not useful in the
     * MDI log and only add clutter. Return false so other handlers can
     * still decide whether to show a toast/banner.
     */
    if (strncmp(message, "Error: Bad command:", 19) == 0) {
        LOGI(TAG, "Skipped log entry: %s", message);
        return false;
    }

    /* Append to the log ring-buffer (no LVGL calls allowed here). */
    _log_append(mdi, message);
    mdi->log_dirty = true;

    /* Check for a command acknowledgement if a command is in-flight. */
    if (mdi->is_busy) {
        const char *m = message;
        /* Skip leading whitespace. */
        while (*m == ' ' || *m == '\t') m++;
        /* Accept "ok" or "OK" (case-insensitive for the first two chars)
         * followed by end-of-string or whitespace. */
        bool is_ok = (m[0] == 'o' || m[0] == 'O') &&
                     (m[1] == 'k' || m[1] == 'K') &&
                     (m[2] == '\0' || m[2] == '\r' || m[2] == '\n' || m[2] == ' ');
        if (is_ok) {
            mdi->response_received = true;
        }
    }

    /* Return false: let the toast / other log handlers see the message too. */
    return false;
}

void mdi_handler_tick(mdi_handler_t *mdi)
{
    if (!mdi) return;

    /* Flush log buffer to the UI data-binding observer. */
    if (mdi->log_dirty) {
        mdi->log_dirty = false;
        if (mdi->log_buf) {
            data_binding_notify_state_changed(
                "MDI.log_changed",
                (binding_value_t){.type = BINDING_TYPE_STRING,
                                  .as.s_val = mdi->log_buf});
        }
    }

    /* Un-busy when the machine acknowledged the in-flight command. */
    if (mdi->response_received) {
        mdi->response_received = false;
        if (mdi->is_busy) {
            _set_busy(mdi, false);
        }
    }
}

void mdi_handle_action(mdi_handler_t *mdi, const char *action_name,
                       binding_value_t value)
{
    (void)value;

    // -----------------------------------------------------------------------
    // MDI.send — read the input textarea, send the command, clear the field.
    // -----------------------------------------------------------------------
    if (strcmp(action_name, "MDI.send") == 0) {
        lv_obj_t *ta = mdi->input_field;
        if (!ta) ta = (lv_obj_t *)obj_registry_get("mdi_input_field");
        if (ta) {
            const char *text = lv_textarea_get_text(ta);
            if (text && text[0]) {
                _send_gcode(mdi, text);
                lv_textarea_set_text(ta, "");
            }
        }
        return;
    }

    // -----------------------------------------------------------------------
    // MDI.input — fires on LV_EVENT_VALUE_CHANGED (each keystroke).
    // No-op here; Enter/Confirm is handled by the LV_EVENT_READY callback
    // registered in mdi_handler_init().
    // -----------------------------------------------------------------------
    if (strcmp(action_name, "MDI.input") == 0) {
        return;
    }

    // -----------------------------------------------------------------------
    // MDI.gcode.<KEY> — send a predefined G-code shortcut.
    // -----------------------------------------------------------------------
    if (strncmp(action_name, "MDI.gcode.", 10) == 0) {
        const char *key = action_name + 10;
        for (size_t i = 0; i < GCODE_TABLE_SIZE; i++) {
            if (strcmp(s_gcode_table[i].key, key) == 0) {
                _send_gcode(mdi, s_gcode_table[i].gcode);
                return;
            }
        }
        LOGW(TAG, "MDI: unknown gcode shortcut key '%s'", key);
        return;
    }

    LOGW(TAG, "MDI: unhandled action '%s'", action_name);
}
