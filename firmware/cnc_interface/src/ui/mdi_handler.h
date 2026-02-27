// mdi_handler.h  —  MDI (Manual Data Input) backend
//
// Manages the MDI view's backend:
//   - A PSRAM-backed (or heap-backed) 1 KB log ring-buffer.
//   - Sending G-code commands via machine_interface and short-circuit
//     blocking/unblocking of the MDI UI while a response is in-flight
//     (1-second timeout).
//   - Predefined G-code shortcut button dispatch (MDI.gcode.*).
//   - LV_EVENT_READY hook on the input textarea so the LVGL keyboard's
//     Enter/Confirm key sends the current command.
//
// Thread model
// -----------
//   mdi_handler_on_log()  — called from the machine/hub receive thread;
//                           ONLY writes to the ring-buffer and sets volatile
//                           flags; no LVGL calls.
//   mdi_handler_tick()    — called from the LVGL task (interface_tick());
//                           flushes flags to data-binding observers.
//   mdi_handle_action()   — called from the LVGL task action-handler;
//                           sends G-code and manipulates LVGL widgets.
//
// Integration
// ----------
//   1. Embed `mdi_handler_t mdi;` in interface_t.
//   2. Call mdi_handler_init() after create_ui() so widget IDs are resolved.
//   3. Call mdi_handler_tick() from interface_tick() every frame.
//   4. In app_action_handler(), forward any action whose name starts with
//      "MDI." to mdi_handle_action().
//   5. In on_log_message(), call mdi_handler_on_log() before other handling.

#ifndef MDI_HANDLER_H
#define MDI_HANDLER_H

#include <stdbool.h>
#include "lvgl.h"
#include "machine/machine_interface.h"
#include "data_binding.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Compile-time tunables
// ---------------------------------------------------------------------------

/// Log ring-buffer size in bytes.  Override at compile time with
/// -D MDI_LOG_BUF_SIZE=<n> or by defining it before including this header.
#ifndef MDI_LOG_BUF_SIZE
#define MDI_LOG_BUF_SIZE  1024
#endif

/// Busy-timeout in milliseconds: re-enables the MDI UI if no "ok" is received.
#ifndef MDI_BUSY_TIMEOUT_MS
#define MDI_BUSY_TIMEOUT_MS  1000
#endif

// ---------------------------------------------------------------------------
// State structure
// ---------------------------------------------------------------------------

typedef struct {
    machine_interface_t *machine;

    // ---- Log ring buffer ----
    // Allocated in PSRAM if BOARD_HAS_PSRAM is defined on ESP32_HW,
    // otherwise from the default heap.
    char  *log_buf;   ///< Pointer to allocated buffer
    int    log_len;   ///< Number of valid bytes (excludes NUL terminator)

    // ---- In-flight command state ----
    // `is_busy`            : set while waiting for a response from the machine.
    // `response_received`  : set by the machine-thread log callback when the
    //                        response contains "ok" / "OK".  Cleared by tick().
    // `log_dirty`          : set when new log content has been appended.
    //                        Cleared by tick() after notifying data-binding.
    bool           is_busy;
    volatile bool  response_received;
    volatile bool  log_dirty;
    lv_timer_t    *busy_timeout_timer;

    // ---- Cached widget references (resolved in mdi_handler_init) ----
    lv_obj_t *input_field;  ///< mdi_input_field textarea
    lv_obj_t *log_area;     ///< mdi_log_area textarea (informational)
} mdi_handler_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Initialise the MDI handler.
 *
 * Allocates the log buffer, resolves widget IDs from the obj_registry,
 * and registers the LV_EVENT_READY callback on the input textarea.
 *
 * Must be called from the LVGL task, after create_ui() has run.
 */
void mdi_handler_init(mdi_handler_t *mdi, machine_interface_t *machine);

/**
 * @brief Release all resources owned by the handler.
 */
void mdi_handler_deinit(mdi_handler_t *mdi);

// ---------------------------------------------------------------------------
// Runtime hooks (called from other parts of the system)
// ---------------------------------------------------------------------------

/**
 * @brief Machine-thread log callback.
 *
 * Appends @p message to the ring-buffer (dropping the oldest characters if
 * the buffer would overflow) and sets the log_dirty / response_received
 * volatile flags.  Does NOT call any LVGL API.
 *
 * @return false — the message is not consumed so the global toast logic can
 *         decide whether to show it separately.
 */
bool mdi_handler_on_log(mdi_handler_t *mdi, const char *message);

/**
 * @brief LVGL-task tick — flush pending flags to data-binding observers.
 *
 * Must be called from interface_tick() (LVGL task context).
 * Notifies MDI.log_changed when log_dirty is set, and clears MDI.is_busy
 * when response_received is set (also cancels the busy-timeout timer).
 */
void mdi_handler_tick(mdi_handler_t *mdi);

/**
 * @brief Dispatch an MDI action from the UI action handler.
 *
 * Handles:
 *   MDI.send        — reads the input textarea, sends G-code, clears the field.
 *   MDI.input       — keystroke notification (no-op; Enter is handled by the
 *                     LV_EVENT_READY callback registered in mdi_handler_init).
 *   MDI.gcode.<KEY> — sends the predefined G-code for the given shortcut key.
 *
 * Must be called from the LVGL task.
 */
void mdi_handle_action(mdi_handler_t *mdi, const char *action_name,
                       binding_value_t value);

#ifdef __cplusplus
}
#endif

#endif  // MDI_HANDLER_H
