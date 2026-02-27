#ifndef UI_INTERFACE_H
#define UI_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "machine/machine_interface.h"
#include "probe/mos_probe_handler.h"
#include "ui/mdi_handler.h"

#ifdef DWC_MACHINE_MODE
#include "config/dwc_settings.h"
#endif

// Represents the UI's view of the machine state.
// One or more flags are set by machine-thread callbacks when data changes.
// The UI thread then reads these flags in `interface_tick()` and updates the UI.
typedef enum {
  UI_DIRTY_NONE = 0,
  UI_DIRTY_CONNECTION = (1 << 0),
  UI_DIRTY_MACHINE_STATE = (1 << 1),  // Status, program running/paused
  UI_DIRTY_POSITION = (1 << 2),      // Machine and WCS position
  UI_DIRTY_HOMING = (1 << 3),        // Homing status for each axis
  UI_DIRTY_WCS = (1 << 4),           // Active Work Coordinate System
  UI_DIRTY_OVERRIDES = (1 << 5),     // Feed and spindle overrides
  UI_DIRTY_SPINDLE = (1 << 6),       // Spindle RPM and status
  UI_DIRTY_FEEDRATE = (1 << 7),      // Current feedrate
  UI_DIRTY_DIALOGS = (1 << 8),       // Message boxes
  UI_DIRTY_JOG_STATE = (1 << 9),     // Active jog axis and step values
  UI_DIRTY_FILES_GCODES = (1 << 10),
  UI_DIRTY_FILES_MACROS = (1 << 11),
  UI_DIRTY_LOG_MESSAGE  = (1 << 12),  // Verbatim message from hub to toast
  UI_DIRTY_MDI_LOG      = (1 << 13),  // New MDI log content / busy-state change
  UI_DIRTY_ALL = 0xFFFFFFFF,
} ui_dirty_flags_t;

// The main structure bridging the machine interface and the UI.
typedef struct {
  machine_interface_t *machine;
  volatile uint32_t dirty_flags;
  lv_obj_t *probing_wizard;
  lv_obj_t *current_msgbox;  // Currently shown RRF machine modal, or NULL.
  char log_message_buf[256];  // Latest log/error message pending display as toast.
  lv_obj_t *toast_bar;        // Currently visible toast bar, or NULL.

  mos_probe_handler_t probe_handler;  ///< MOS back-end for cam_positioning probe wizard
  mdi_handler_t mdi;                  ///< MDI (Manual Data Input) back-end

#ifdef DWC_MACHINE_MODE
  dwc_settings_t setup_settings;  // Temporary storage for the DWC setup flow
#endif
} interface_t;

/**
 * @brief Initializes the UI-machine interface layer.
 *
 * Sets up data-binding, creates the LVGL UI, and registers callbacks with the
 * machine interface to listen for state changes.
 *
 * @param interface Pointer to the interface_t struct to initialize.
 * @param machine Pointer to the machine_interface_t object.
 */
void interface_init(interface_t *interface, machine_interface_t *machine);

/**
 * @brief Main tick function for the UI interface.
 *
 * This should be called periodically from the main UI thread/loop. It checks
 * for dirty flags set by machine callbacks and updates the UI accordingly by
 * notifying the data-binding system of state changes.
 *
 * @param interface Pointer to the interface_t struct.
 */
void interface_tick(interface_t *interface);

#ifdef __cplusplus
}
#endif

#endif  // UI_INTERFACE_H
