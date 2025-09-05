#ifndef UI_ACTION_HANDLER_H
#define UI_ACTION_HANDLER_H

#include "ui/interface.h"

/**
 * @brief Initializes the UI action handler system.
 *
 * Registers the central action handler callback with the data-binding system.
 * This function should be called once during UI initialization.
 *
 * @param interface Pointer to the main UI interface struct, which contains the
 *                  machine handle and other shared state. This is passed as
 *                  the user_data context to the action handler.
 */
void ui_action_handler_init(interface_t *interface);

#endif  // UI_ACTION_HANDLER_H