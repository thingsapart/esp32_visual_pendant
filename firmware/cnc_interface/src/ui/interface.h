#ifndef UI_INTERFACE_H
#define UI_INTERFACE_H

#include "lvgl.h"

LV_FONT_DECLARE(font_kode_40)
LV_FONT_DECLARE(font_kode_34)
LV_FONT_DECLARE(font_kode_30)
LV_FONT_DECLARE(font_kode_24)
LV_FONT_DECLARE(font_kode_20)
LV_FONT_DECLARE(font_kode_14)

#include "machine/machine_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A structure to hold the state of the UI interface, primarily
 *        a pointer to the machine it is controlling and observing.
 */
typedef struct {
    machine_interface_t* machine;
} interface_t;

/**
 * @brief Initializes the UI.
 *
 * This function sets up the LVGL UI by calling the generated `create_ui` function,
 * and it establishes the data binding between the UI and the machine interface.
 * It registers the necessary callbacks and the central action handler.
 *
 * @param interface A pointer to the interface_t structure to initialize.
 * @param machine A pointer to the machine_interface_t that the UI will interact with.
 */
void interface_init(interface_t* interface, machine_interface_t* machine);

/**
 * @brief Ticks the UI interface.
 *
 * This function is called periodically from the main LVGL task loop. It's used
 * to update UI elements that are not driven by machine state events, such as
 * the global 'time' variable used for animations.
 *
 * @param interface A pointer to the initialized interface_t structure.
 */
void interface_tick(interface_t* interface);

#ifdef __cplusplus
}
#endif

#endif // UI_INTERFACE_H
