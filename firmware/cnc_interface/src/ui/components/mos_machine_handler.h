#ifndef MOS_MACHINE_HANDLER_H
#define MOS_MACHINE_HANDLER_H

#include "lvgl.h"
#include "machine/machine_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers a set of callbacks with the probing wizard to control a 
 *        Reprap Firmware machine running the MillenniumOS G-Code extensions.
 *
 * This function connects the wizard's actions (e.g., "probe point") to the
 * corresponding MillenniumOS macros (e.g., G6503.1 for a block probe). It handles
 * the construction of G-Code commands and the asynchronous communication flow
 * required to get results back from the machine.
 *
 * @param wizard_obj Pointer to the probing wizard object.
 * @param machine Pointer to the machine_interface_t instance for the target machine.
 */
void lv_probing_wizard_register_mos_callbacks(lv_obj_t* wizard_obj, machine_interface_t* machine);

#ifdef __cplusplus
}
#endif

#endif // MOS_MACHINE_HANDLER_H
