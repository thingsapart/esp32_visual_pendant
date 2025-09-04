#ifndef LV_PROBING_WIZARD_STUBS_H
#define LV_PROBING_WIZARD_STUBS_H

#include "lv_probing_wizard.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers a set of stub callbacks with the probing wizard for demonstration and testing.
 *
 * This function connects mock machine handler functions to the wizard. These stubs
 * will automatically "respond" to probe commands with pre-defined coordinates after a
 * short delay (using an lv_timer), allowing the full UI flow to be tested without
 * a connected machine.
 *
 * @param obj Pointer to the probing wizard object.
 */
void lv_probing_wizard_register_stub_callbacks(lv_obj_t * obj);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PROBING_WIZARD_STUBS_H*/
