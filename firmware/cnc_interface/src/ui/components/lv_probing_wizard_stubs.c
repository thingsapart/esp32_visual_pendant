#define UI_DEBUG_LOCAL_LEVEL D_VERBOSE
#include "debug.h"

#include "lv_probing_wizard_stubs.h"
#include "lv_probing_wizard.h"

static const char * TAG = "probing-wizard-stubs";

static lv_probing_wizard_point_float_t stub_get_current_jogged_position(void) {
    LOGV(TAG, "STUB: Getting jogged position.");
    return (lv_probing_wizard_point_float_t){ .x = 12.34f, .y = 56.78f };
}

static void stub_execute_probe(lv_obj_t * wizard_obj, const lv_probing_action_t * action) {
    LOGV(TAG, "STUB: Executing probe action: '%s'", action->instruction_text);

    switch(action->type) {
        case ACTION_PROBE_Z_TOP:
            lv_probing_wizard_set_z_top(wizard_obj, -0.5f);
            break;
        case ACTION_PROBE_POINT: {
            uint8_t index = action->param.probe_index;
            float x = 0.0f, y = 0.0f;
            switch(index) {
                case 0: x = 10.0f; y = 50.0f; break;
                case 1: x = 90.0f; y = 10.0f; break;
                case 2: x = 50.0f; y = 90.0f; break;
                case 3: x = 50.0f; y = 10.0f; break;
            }
            LOGV(TAG, "STUB: Reporting probe result for index %d.", index);
            lv_probing_wizard_report_probe_result(wizard_obj, index, x, y);
            break;
        }
        default: break;
    }

    //LOGV(TAG, "STUB: Probe action complete, advancing wizard step.");
    //lv_probing_wizard_advance_step(wizard_obj);
}

static void stub_set_wcs_origin(lv_obj_t * wizard_obj, uint8_t wcs_index, float x, float y, float z, bool apply_z) {
    if (apply_z) {
        LOGV(TAG, "STUB: Setting WCS G%d origin to X:%.2f, Y:%.2f, Z:%.2f", 53 + wcs_index, x, y, z);
    } else {
        LOGV(TAG, "STUB: Setting WCS G%d origin to X:%.2f, Y:%.2f", 53 + wcs_index, x, y);
    }
}

void lv_probing_wizard_register_stub_callbacks(lv_obj_t * obj) {
    lv_probing_wizard_register_callbacks(obj, stub_get_current_jogged_position, stub_execute_probe, stub_set_wcs_origin);
    LOGV(TAG, "Probing wizard is using stub callbacks for demonstration.");
}
