#ifndef UI_ACTION_HANDLER_H
#define UI_ACTION_HANDLER_H

#include "ui/interface.h"

/**
 * @brief Initializes and registers the global UI action handler.
 *
 * This function registers the single callback that will receive all
 * actions dispatched from the UI via the data binding system.
 *
 * @param interface A pointer to the main UI interface structure, which will
 *                  be passed as user_data to the action handler.
 */
void ui_action_handler_init(interface_t* interface);

#endif // UI_ACTION_HANDLER_H
