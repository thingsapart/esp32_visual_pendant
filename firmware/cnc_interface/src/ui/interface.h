#ifndef UI_INTERFACE_H
#define UI_INTERFACE_H

#include "lvgl.h"
#include "machine/machine_interface.h"
#include "ui/assets.h"

#ifdef DWC_MACHINE_MODE
#include "config/dwc_settings.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bitmask for tracking which parts of the machine state have changed
 *        and need to be reflected in the UI.
 */
typedef enum {
  UI_DIRTY_NONE = 0,
  UI_DIRTY_STATE = (1 << 0),
  UI_DIRTY_POS = (1 << 1),
  UI_DIRTY_HOME = (1 << 2),
  UI_DIRTY_WCS = (1 << 3),
  UI_DIRTY_FEED = (1 << 4),
  UI_DIRTY_SENSORS = (1 << 5),
  UI_DIRTY_DIALOGS = (1 << 6),
  UI_DIRTY_SPINDLES_TOOLS = (1 << 7),
  UI_DIRTY_CONNECTED = (1 << 8),
  UI_DIRTY_MOVE_AXIS = (1 << 9),
  UI_DIRTY_FILES_GCODES = (1 << 10),
  UI_DIRTY_FILES_MACROS = (1 << 11),
} ui_dirty_flags_t;

/**
 * @brief A structure to hold the state of the UI interface, primarily
 *        a pointer to the machine it is controlling and observing.
 */
typedef struct {
  machine_interface_t* machine;
#ifdef DWC_MACHINE_MODE
  // Used to build up settings during the initial setup flow
  dwc_settings_t setup_settings;
#endif
  // Dirty flags are set by machine callbacks (in machine thread)
  // and are processed by interface_tick (in UI thread).
  volatile uint32_t dirty_flags;
  lv_obj_t* probing_wizard;
} interface_t;

/**
 * @brief Initializes the UI.
 *
 * This function sets up the LVGL UI by calling the generated `create_ui`
 * function, and it establishes the data binding between the UI and the machine
 * interface. It registers the necessary callbacks and the central action
 * handler.
 *
 * @param interface A pointer to the interface_t structure to initialize.
 * @param machine A pointer to the machine_interface_t that the UI will interact
 * with.
 */
void interface_init(interface_t* interface, machine_interface_t* machine);

/**
 * @brief Ticks the UI interface.
 *
 * This function is called periodically from the main LVGL task loop. It's used
 * to update UI elements that are not driven by machine state events, such as
 * the global 'time' variable for animations.
 *
 * @param interface A pointer to the initialized interface_t structure.
 */
void interface_tick(interface_t* interface);

#ifdef __cplusplus
}
#endif

#endif  // UI_INTERFACE_H
