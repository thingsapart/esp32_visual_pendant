#ifndef UI_SETUP_DWC_H
#define UI_SETUP_DWC_H

#include "ui/interface.h"

#ifdef DWC_MACHINE_MODE

/**
 * @brief Starts the interactive Wi-Fi and DWC host setup process.
 *
 * This function initiates a series of modals on the UI to guide the user
 * through selecting a Wi-Fi network, entering a password, and providing the
 * DWC hostname or IP address.
 *
 * @param interface A pointer to the main UI interface structure, used to store
 *                  temporary settings during the setup process.
 */
void ui_start_dwc_setup_flow(interface_t* interface);

#endif  // DWC_MACHINE_MODE

#endif  // UI_SETUP_DWC_H
