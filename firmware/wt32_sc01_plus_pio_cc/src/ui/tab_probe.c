// tab_probe.c
#include "tab_probe.h"
#include "ui/ui_helpers.h" // Your LVGL helper macros
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// --- Constants and Macros ---

#include "ui/interface.h"
#include "ui/tab_jog.h"
#include "ui/modals.h"

#include "debug.h"

#define PRB_Z "{global.mosMI[2] - $Z}"  // Example, adjust to your needs
#define PRB_J "{global.mosMI[0]}"
#define PRB_K "{global.mosMI[1]}"
#define PRB_L "{global.mosMI[2]}"

// --- Static Data (Probe Actions) ---
// Define your probe actions as static const data.
// --- probing settings structure ---

typedef struct {
    const char *label;
    float default_value;
    float min_value;
    float max_value;
    const char param_name; // "H", "I", "Z", etc.
    lv_obj_t* slider; // will point to corresponding slider.
    bool is_slider;
} probe_setting_t;

// Define the settings as a static const array
static probe_setting_t probe_settings[] = {
    {"Width / Dia", 50.0f, 1.0f, 200.0f, 'H', NULL, true},
    {"Length", 100.0f, 1.0f, 200.0f, 'I', NULL, true},
    {"Depth / Dist", 2.0f, 0.0f, 10.0f, 'Z', NULL, true},
    {"Surf Clear", 5.0f, 0.0f, 20.0f, 'T', NULL, true},
    {"Corner Clear", 5.0f, 0.0f, 20.0f, 'C', NULL, true},
    {"Overtravel", 2.0f, 0.0f, 10.0f, 'O', NULL, true},
    {"WCS", 0, 0, 0, 'W', NULL, false},        // Special case: WCS is handled differently
    {"Quick Mode", 0, 0, 0, 'Q', NULL, false} // Special case: Quick Mode (0 or 1)
};
static const size_t num_probe_settings = sizeof(probe_settings) / sizeof(probe_settings[0]);

#define IMG_USE_FS 0

#if (POSIX || IMG_USE_FS)
// Placeholder image paths. Replace with your actual paths.
#define img_arr_s_data "S:/img/img_arr_s.png"
#define img_arr_e_data "S:/img/img_arr_e.png"
#define img_arr_w_data "S:/img/img_arr_w.png"
#define img_arr_n_data "S:/img/img_arr_n.png"
#define img_ref_sfc_data "S:/img/img_ref_sfc.png"
#define img_arr_se_data "S:/img/img_arr_se.png" //Back-left
#define img_arr_sw_data "S:/img/img_arr_sw.png" //Back-right
#define img_arr_ne_data "S:/img/img_arr_ne.png" //Front-Left
#define img_arr_nw_data "S:/img/img_arr_nw.png" //Front-right
#define img_pkt_in_data "S:/img/img_pkt_in.png"  //Outside Rect
#define img_ctr1_boss_data "S:/img/img_center_boss.png" //Outside Boss
#define img_pkt_out_data "S:/img/img_pkt_out.png"  //Inside Pocket
#define img_ctr1_bore_data "S:/img/img_center_bore.png" //Inside Bore
#else
LV_IMG_DECLARE(img_arr_s)
LV_IMG_DECLARE(img_arr_e)
LV_IMG_DECLARE(img_arr_w)
LV_IMG_DECLARE(img_arr_n)
LV_IMG_DECLARE(img_ref_sfc)
LV_IMG_DECLARE(img_arr_se) //Back-left
LV_IMG_DECLARE(img_arr_sw) //Back-right
LV_IMG_DECLARE(img_arr_ne) //Front-Left
LV_IMG_DECLARE(img_arr_nw) //Front-right
LV_IMG_DECLARE(img_pkt_in)  //Outside Rect
LV_IMG_DECLARE(img_ctr_boss) //Outside Boss
LV_IMG_DECLARE(img_pkt_out)  //Inside Pocket
LV_IMG_DECLARE(img_ctr_bore) //Inside Bore

#define img_arr_s_data &img_arr_s
#define img_arr_e_data &img_arr_e
#define img_arr_w_data &img_arr_w
#define img_arr_n_data &img_arr_n
#define img_ref_sfc_data &img_ref_sfc
#define img_arr_se_data &img_arr_se //Back-left
#define img_arr_sw_data &img_arr_sw //Back-right
#define img_arr_ne_data &img_arr_ne //Front-Left
#define img_arr_nw_data &img_arr_nw //Front-right
#define img_pkt_in_data &img_pkt_in  //Outside Rect
#define img_ctr1_boss_data &img_ctr_boss //Outside Boss
#define img_pkt_out_data &img_pkt_out  //Inside Pocket
#define img_ctr1_bore_data &img_ctr_bore //Inside Bore
#endif

// 3-Axis probing operations.
static const char *probe_modes_3d_bl[] = {"Q", "W", "P:$Z", "N:3", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", "C", "O", NULL};
static const char *probe_modes_3d_br[] = {"Q", "W", "P:$Z", "N:2", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", "C", "O", NULL};
static const char *probe_modes_3d_fl[] = {"Q", "W", "P:$Z", "N:0", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", "C", "O", NULL};
static const char *probe_modes_3d_fr[] = {"Q", "W", "P:$Z", "N:1", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", "C", "O", NULL};

static const probe_action_t probe_modes_3d[][2] = {
    {
        {"G6520.1", probe_modes_3d_bl, img_arr_se_data, "back-left vise corner"},       // Back-Left
        {"G6520.1", probe_modes_3d_br, img_arr_sw_data, "back-right vise corner"},     // Back-Right
    },
    {
        {"G6520.1", probe_modes_3d_fl, img_arr_ne_data, "front-left vise corner"},       // Front-Left
        {"G6520.1", probe_modes_3d_fr, img_arr_nw_data, "front-right vise corner"},     // Front-Right
    },
};
static const size_t probe_modes_3d_rows = sizeof(probe_modes_3d) / sizeof(probe_modes_3d[0]);
static const size_t probe_modes_3d_cols = sizeof(probe_modes_3d[0]) / sizeof(probe_action_t);

// 2-axis Probing Operations: outside-towards-inside.
static const char *probe_modes_2d_out_params_bl[] = {"Q", "W", "Z:"PRB_Z, "N:3", "O", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", NULL};
static const char *probe_modes_2d_out_params_br[] = {"Q", "W", "Z:"PRB_Z, "N:2", "O", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", NULL};
static const char *probe_modes_2d_out_params_fl[] = {"Q", "W", "Z:"PRB_Z, "N:0", "O", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", NULL};
static const char *probe_modes_2d_out_params_fr[] = {"Q", "W", "Z:"PRB_Z, "N:1", "O", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", NULL};
static const char *probe_modes_2d_out_params_rect[] = {"Q", "W", "N", "O", "J", "K", "L", "H", "I", "T", NULL};
static const char *probe_modes_2d_out_params_boss[] = {"Q", "W", "N", "O", "J", "K", "L", "H",  NULL};

static const probe_action_t probe_modes_2d_out[][2] = {
    {
        {"G6509.1", probe_modes_2d_out_params_bl, img_arr_se_data, "back-left corner"},       // Back-Left
        {"G6509.1", probe_modes_2d_out_params_br, img_arr_sw_data, "back-right corner"},     // Back-Right
    },
    {
        {"G6509.1", probe_modes_2d_out_params_fl, img_arr_ne_data, "front-left corner"},       // Front-Left
        {"G6509.1", probe_modes_2d_out_params_fr, img_arr_nw_data, "front-right corner"},     // Front-Right
    },
    {
        {"G6503.1", probe_modes_2d_out_params_rect, img_pkt_in_data, "outside rectangle"}, // Outside Rectangle
        {"G6501.1", probe_modes_2d_out_params_boss, img_ctr1_boss_data, "outside boss"},    // Outside Boss
    }
};
static const size_t probe_modes_2d_out_rows = sizeof(probe_modes_2d_out) / sizeof(probe_modes_2d_out[0]);
static const size_t probe_modes_2d_out_cols = sizeof(probe_modes_2d_out[0]) / sizeof(probe_action_t);

// 2-axis Probing Operations: inside-towards-outside.
static const char *probe_modes_2d_in_params_bl[] = {"Q", "W", "Z:"PRB_Z, "N:3", "O", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", NULL};
static const char *probe_modes_2d_in_params_br[] = {"Q", "W", "Z:"PRB_Z, "N:2", "O", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", NULL};
static const char *probe_modes_2d_in_params_fl[] = {"Q", "W", "Z:"PRB_Z, "N:0", "O", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", NULL};
static const char *probe_modes_2d_in_params_fr[] = {"Q", "W", "Z:"PRB_Z, "N:1", "O", "J:"PRB_J, "K:"PRB_K, "L:"PRB_L, "H", "I", "T", NULL};
static const char *probe_modes_2d_in_params_pkt[] = {"Q", "W", "N", "O", "J", "K", "L", "H", "I", "T", NULL};
static const char *probe_modes_2d_in_params_bore[] = {"Q", "W", "N", "O", "J", "K", "L", "H", NULL};

// Create the 2D array (an array of row pointers)
static const probe_action_t probe_modes_2d_in[][2] = {
    {
        {"G6508.1", probe_modes_2d_in_params_bl, img_arr_nw_data, "back-left inside corner"},    // Back-Left
        {"G6508.1", probe_modes_2d_in_params_br, img_arr_ne_data, "back-right inside corner"},   // Back-Right
    },
    {
        {"G6508.1", probe_modes_2d_in_params_fl, img_arr_sw_data, "front-left inside corner"},   // Front-Left
        {"G6508.1", probe_modes_2d_in_params_fr, img_arr_se_data, "front-right inside corner"},  // Front-Right
    },
    {
        {"G6502.1", probe_modes_2d_in_params_pkt, img_pkt_out_data, "inside pocket"},          // Inside Pocket
        {"G6500.1", probe_modes_2d_in_params_bore, img_ctr1_bore_data, "inside bore"},          // Inside Bore
    }
};

static const size_t probe_modes_2d_in_rows = sizeof(probe_modes_2d_in) / sizeof(probe_modes_2d_in[0]);
static const size_t probe_modes_2d_in_cols = sizeof(probe_modes_2d_in[0]) / sizeof(probe_action_t);

// Single-axis, single-surface Probing.

static const char* surface_probe_back_params[] = {"W", "H", "I", "J", "K", "L", NULL};
static const char* surface_probe_left_params[] = {"W", "H", "I", "J", "K", "L", NULL};
static const char* surface_probe_top_params[] = {"W", "H", "I", "J", "K", "L", NULL};
static const char* surface_probe_right_params[] = {"W", "H", "I", "J", "K", "L", NULL};
static const char* surface_probe_front_params[] = {"W", "H", "I", "J", "K", "L", NULL};
static const char* surface_probe_ref_params[] = {NULL}; // Empty - no parameters

static const probe_action_t surface_probe_modes[][3] = {
    {
        {NULL, NULL, NULL, NULL}, // Empty cell
        {"G6520.1", surface_probe_back_params, img_arr_s_data, "back face"}, // Back
        {NULL, NULL, NULL, NULL} // Empty cell
    },
    {
        {"G6520.1", surface_probe_left_params, img_arr_e_data, "left face"}, // Left
        {"G6520.1", surface_probe_top_params, img_ctr1_boss_data, "top surface"},  // Top surface
        {"G6520.1", surface_probe_right_params, img_arr_w_data, "right face"}  // Right
    },
    {
        {NULL, NULL, NULL, NULL},  // Empty cell
        {"G6520.1", surface_probe_front_params, img_arr_n_data, "front face"},  // Front
        {"G6520.1", surface_probe_ref_params, img_ref_sfc_data, "reference surface"} // ref surface
    }
};

static const size_t surface_probe_modes_rows = sizeof(surface_probe_modes) / sizeof(surface_probe_modes[0]);
static const size_t surface_probe_modes_cols = sizeof(surface_probe_modes[0]) / sizeof(probe_action_t);

// --- Helper Functions ---

// Create a string representation of a float, handling NaN
static void float_to_string(float value, char *buffer, size_t buffer_size, int precision) {
    if (isnan(value)) {
        strncpy(buffer, "?", buffer_size - 1);
        buffer[buffer_size - 1] = '\0'; // Ensure null termination
    } else {
        snprintf(buffer, buffer_size, "%.*f", precision, value);
    }
}


#define VSTR(s) STR(s)
#define STR(s) #s
#define STRINGIFY(x) #x
#define EXPAND_STR(x) STRINGIFY(x)

#define GCODE_PREC_FMT "%." EXPAND_STR(GCODE_POSITION_PRECISION) "g"
char *float_to_str(float v) {
    size_t len = snprintf(NULL, 0, GCODE_PREC_FMT, v);
    char *result = malloc(len + 1);
    snprintf(result, len + 1, GCODE_PREC_FMT, v);
    return result;
}

char *int_to_str(int v) {
    size_t len = snprintf(NULL, 0, "%d", v);
    char *result = malloc(len + 1);
    snprintf(result, len + 1, "%d", v);
    return result;
}

// Allocates the return string, free it later.
float setting_from_param(const char param_name, tab_probe_t *tp) {
    if (param_name == 'H') return (tp->settings.width_dia);
    else if (param_name == 'I') return (tp->settings.length);
    else if (param_name == 'Z') return (tp->settings.depth_dist);
    else if (param_name == 'T') return (tp->settings.surf_clear);
    else if (param_name == 'C') return (tp->settings.corner_clear);
    else if (param_name == 'O') return (tp->settings.overtravel);
    else if (param_name == 'W') return (tp->settings.wcs);
    else if (param_name == 'Q') return (tp->settings.quick_mode);
    else return NAN;
}

char *replace_vars(const char *param_desc, tab_probe_t *tp);

char *make_param_value(const char *param_desc, tab_probe_t *tp) {
    char *value = NULL;
    // Check if it's expression with variable replacement.
    printf("'%s' :: len > 2? => %d, %c (desc[1] == ':'? => %d\n", param_desc, strlen(param_desc) > 2, param_desc[1], param_desc[1] == ':');

    if (strlen(param_desc) > 2 && param_desc[1] == ':') {
        printf("REPLACE: ");
        value = replace_vars(&param_desc[2], tp);
        printf("==> %s\n\n", value);
        return value;
    }

    for (size_t j = 0; j < num_probe_settings; j++) {
        if (strlen(param_desc) == 1 && param_desc[0] == probe_settings[j].param_name) {
            printf("VAR: ");
            value = float_to_str(setting_from_param(param_desc[0], tp));
            printf("==> %s\n\n", value);
            return value;
        }
    }

    // Check if it's "complex expression" where we need to recursively
    // replace parts of the descriptor with variables.
    if (!value) {
        printf("ELSE ==> %s\n\n", param_desc);
        return strdup(param_desc);
    }

    return value;
}

#define MAX_PARAM_DESC_LEN 128
char *replace_vars(const char *param_desc, tab_probe_t *tp) {
    char buf[MAX_PARAM_DESC_LEN * 2];
    assert((strlen(param_desc) < MAX_PARAM_DESC_LEN) && "Expected param desc < MAX_PARAM_DESC_LEN");
    size_t len = strlen(param_desc);
    size_t offs = 0, next = len;
    char *cnext = NULL;
    while (*param_desc != '\0' && (cnext = strchr(param_desc, '$')) != NULL) {
        next = cnext - param_desc;

        assert((len > next + 1) && "Expected replacement expr $ to be followed by setting/param.");
        strncpy(buf + offs, param_desc, next);
        offs += next - offs;
        char pdesc[2];
        pdesc[0] = param_desc[next + 1]; pdesc[1] = '\0';

        char *pval = make_param_value(pdesc, tp);
        if (pval) {
            size_t plen = strlen(pval);
            strncpy(buf + offs, pval, plen);
            offs += plen;
            free(pval);
        } else {
            assert(false && "param desc expr not found");
        }
        param_desc = &param_desc[next + 2];
    }
    size_t nlen = strlen(param_desc); 
    strncpy(buf + offs, param_desc, nlen);
    offs += nlen;
    buf[offs] = '\0';

    char *res = malloc(strlen(buf) + 1);
    strcpy(res, buf);
    return res;
}

// Create the G-code string with parameters
static void make_gcode(char *buffer, size_t buffer_size, const char *gcode,
                       const char **params, tab_probe_t *tp) {
     if (!gcode || !params )
     {
        LV_LOG_WARN("make_gcode: gcode or params is NULL!");
        return;
     }

    size_t offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset, "%s ", gcode);

    for (int i = 0; params[i] != NULL; i++) {
        const char *param_desc = params[i];
        char *value = make_param_value(param_desc, tp);

        if (value) {
            offset += snprintf(buffer + offset, buffer_size - offset, "%c%s ", param_desc[0], value);
            free(value);
        } else {
            // Handle the case where the parameter is not found
            LV_LOG_WARN("make_gcode: Parameter '%s' not found", param_desc);
            offset += snprintf(buffer + offset, buffer_size - offset, "%s? ", param_desc);
        }
    }
}

static char *probe_gcode = NULL;

void probe_msg_box_close(lv_event_t *e) {
    if (probe_gcode) {
        free(probe_gcode);
        probe_gcode = NULL;
    }
    modal_close_handler(e);
}

void probe_msg_box_probe(lv_event_t *e) {
    lv_obj_t *mbox = (lv_obj_t *) lv_event_get_user_data(e);
    probe_btn_matrix_t *pbm = (probe_btn_matrix_t *) lv_obj_get_user_data(mbox);
    machine_interface_t *machine = pbm->tab_probe->interface->machine;
    if (machine) {
        machine->send_gcode(machine, probe_gcode, MACHINE_POSITION);
    }
    probe_msg_box_close(e);
}

// --- Event Handlers for ProbeBtnMatrix ---
// Shared handler with user_data pointing to the probe_action_t
static void probe_btn_matrix_click_handler(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    probe_btn_matrix_t *pbm = (probe_btn_matrix_t *) lv_obj_get_user_data(btn);
    probe_action_t *action = (probe_action_t *) lv_event_get_user_data(e);

    if (!pbm || !pbm->tab_probe || !pbm->tab_probe->interface) {
        LV_LOG_WARN("probe_btn_matrix_click_handler: Invalid pbm or interface");
        return;
    }

    // Check if the machine is homed before proceeding
    // if (!pbm->tab_probe->interface->machine->is_connected(pbm->tab_probe->interface->machine)) {
    //   Abort?
    // }
    if (!machine_interface_is_homed(pbm->tab_probe->interface->machine, "XYZ")) {
        // ui.modals.home_modal(pbm->tab_probe->interface); // You'll need a C version of this
        LV_LOG_WARN("Machine is not homed or not connected."); // Placeholder: Show a modal
        machine_interface_t *mach = pbm->tab_probe->interface->machine;
        char *di = mach->debug_print(mach);
        _df(0, "HOMED: %d\n", machine_interface_is_homed(pbm->tab_probe->interface->machine, "XYZ"));
        _d(0, di);
        free(di);
        home_modal(mach);

        return;
    }

    // --- Create confirmation modal ---
    char gcode_buffer[256]; // Buffer for the generated G-code
    make_gcode(gcode_buffer, sizeof(gcode_buffer), action->gcode, action->params, pbm->tab_probe);
    probe_gcode = strdup(gcode_buffer);

    char modal_text[512];
    char *buf = modal_text;
    buf += snprintf(buf, sizeof(modal_text) - 1, "Probing %s with:\n\n", action->description) - 1;

    for (int i = 0; action->params[i] != NULL; i++) {
        const char *p = action->params[i];
        if (strlen(p) == 1) {
            for (int j = 0; j < num_probe_settings; j++) {
                if (p[0] == probe_settings[j].param_name) {
                    char *v = float_to_str(setting_from_param(p[0], pbm->tab_probe));
                    buf += snprintf(buf,
                                    sizeof(modal_text) - (buf - modal_text) - 1,
                                    "%s: %s\n", 
                                    probe_settings[j].label,
                                    v);
                    free(v);
                    break;
                }
            }
        }
    }
    buf += snprintf(buf,
                    sizeof(modal_text) - (buf - modal_text) - 1,
                    "\n\nG-Code: %s\n\n", 
                    probe_gcode);
    *(buf + 1) = '\0';

    char title[128];
    snprintf(title, sizeof(title), "Probe %s", action->description);

    const char *buttons[] = { "Start Probe", "Cancel", NULL };
    void (*btn_cbs[])(lv_event_t *e) = { probe_msg_box_probe, modal_close_handler, NULL };
    button_modal(title, modal_text, buttons, btn_cbs, pbm);
}

static void quick_mode_cb(lv_event_t *e)
{
     lv_obj_t *sw = lv_event_get_target(e);
    probe_btn_matrix_t *pbm = (probe_btn_matrix_t *)lv_event_get_user_data(e);
     if (pbm && pbm->tab_probe) {
        pbm->tab_probe->settings.quick_mode = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    }
}


// --- ProbeBtnMatrix Implementation ---

void style_probe_obj(lv_obj_t *obj, float w_percent, float h_percent) {
    _style_gradient(obj, lv_color_hex(0xCCCCCC55), lv_color_hex(0x77777755), 
                LV_GRAD_DIR_VER, 200, 2, lv_palette_lighten(LV_PALETTE_AMBER, 3), 5, 
                lv_color_hex(0x77777755), lv_palette_lighten(LV_PALETTE_BLUE, 3), 5);

    lv_coord_t cw = width_(obj), ch = height_(obj);
    lv_coord_t w = round(cw * w_percent), h = round(ch * h_percent);
    lv_coord_t l = (cw - w) / 2, t = (ch - h) / 2;

    _pads(obj, l, t, l, t);
    _size(obj, w, h);
}

probe_btn_matrix_t *probe_btn_matrix_create(lv_obj_t *parent,
                                             tab_probe_t *tab_probe,
                                             const probe_action_t (*actions_)[],
                                             size_t num_rows,
                                             size_t num_cols,
                                             float rect_width_percent,
                                             float rect_height_percent,
                                             bool quick) {
    probe_btn_matrix_t *pbm = (probe_btn_matrix_t *)malloc(sizeof(probe_btn_matrix_t));
    if (!pbm) {
        LV_LOG_ERROR("Failed to allocate probe_btn_matrix_t");
        return NULL;
    }
    memset(pbm, 0, sizeof(probe_btn_matrix_t));
    const probe_action_t (*actions)[num_cols] = actions_;

    pbm->tab_probe = tab_probe;
    pbm->actions_ = actions;
    pbm->num_rows = num_rows;
    pbm->num_cols = num_cols; // This will be ignored.

    _width(parent, lv_pct(100));
    _height(parent, LV_SIZE_CONTENT);
    _style_local(parent, margin_all, LV_PART_MAIN, 0);
    _style_local(parent, pad_top, LV_PART_MAIN, 0);
    _style_local(parent, pad_bottom, LV_PART_MAIN, 5);
    _style_local(parent, pad_left, LV_PART_MAIN, 0);
    _style_local(parent, pad_right, LV_PART_MAIN, 0);

    pbm->container = lv_obj_create(parent);
    if (!pbm->container) {
        LV_LOG_ERROR("container alloc failed");
        probe_btn_matrix_destroy(pbm);
        return NULL;
    }

    _width(pbm->container, lv_pct(100));
    _height(pbm->container, LV_SIZE_CONTENT);
    _maximize_client_area(pbm->container);
    _bg_opa(pbm->container, LV_OPA_0, _M);
    _pads(pbm->container, 10, 20, 10, 20);
    _margin_top(pbm->container, 15, LV_STATE_DEFAULT);

    _flex_flow(pbm->container, LV_FLEX_FLOW_COLUMN);

    for (size_t j = 0; j < num_rows; j++) {
        lv_obj_t *row_container = lv_obj_create(pbm->container);
        if (!row_container) {
            LV_LOG_ERROR("row container alloc failed");
             probe_btn_matrix_destroy(pbm);
            return NULL;
        }
        _flex_flow(row_container, LV_FLEX_FLOW_ROW);
        _size(row_container, lv_pct(100), LV_SIZE_CONTENT);
        _style_local(row_container, margin_all, LV_PART_MAIN, 0);
        _style_local(row_container, pad_all, LV_PART_MAIN, 2);
        _style_local(row_container, border_width, LV_PART_MAIN, 0);
        _bg_opa(row_container, LV_OPA_0, _M);

        for (size_t i = 0; i < num_cols; i++) {
            lv_obj_t *btn = NULL;
            const probe_action_t *action = &actions[j][i];
            if (action->gcode == NULL)
            {
                // empty cell.
                 btn = lv_label_create(row_container); // Create an empty label
                if (!btn) {
                    LV_LOG_ERROR("Failed to create empty btn");
                    probe_btn_matrix_destroy(pbm); // Clean up
                    return NULL;
                }
                _label_text(btn, "");

            } else {
                // has content.
                btn = lv_button_create(row_container);

                if (!btn) {
                   LV_LOG_ERROR("btn creation failed!");
                   probe_btn_matrix_destroy(pbm);
                    return NULL;
                }

                lv_obj_set_user_data(btn, pbm);
                _height(btn, 38);
                lv_obj_add_event_cb(btn, probe_btn_matrix_click_handler, LV_EVENT_CLICKED, (void *) action);

                lv_obj_t *img = lv_img_create(btn);
                 if (!img) {
                    LV_LOG_ERROR("img creation failed!");
                    probe_btn_matrix_destroy(pbm); // Clean up
                    return NULL;
                 }
                lv_img_set_src(img, actions[j][i].img_path);
                lv_obj_center(img);
                _style_local(btn, border_width, LV_PART_MAIN, 1);
                _style_local(btn, border_color, LV_PART_MAIN, lv_color_hex(0x008080)); // Teal
                _style_local(btn, bg_opa, LV_PART_MAIN, LV_OPA_0);
            }

            lv_obj_set_flex_grow(btn, 1); // Use lv_obj_set_flex_grow
            _style_local(btn, margin_all, LV_PART_MAIN, 0);
            _style_local(btn, pad_all, LV_PART_MAIN, 0);
        }
    }

    if (quick) {
       pbm->quick_mode_chk = lv_checkbox_create(parent);
       if (!pbm->quick_mode_chk)
       {
          LV_LOG_ERROR("Failed to create quick mode checkbox");
          probe_btn_matrix_destroy(pbm);
          return NULL;
       }
        lv_checkbox_set_text(pbm->quick_mode_chk, "Quick Mode");
        lv_obj_center(pbm->quick_mode_chk);
        lv_obj_add_event_cb(pbm->quick_mode_chk, quick_mode_cb, LV_EVENT_VALUE_CHANGED, pbm); // Pass pbm as user_data

    } else {
          pbm->quick_mode_chk = NULL;
    }

    _center(pbm->container);
    _update_layout(pbm->container);

// Draw a background representing the probed box' borders?
#ifdef PROBE_BOX_BG
    mk_container(NULL, pbm->container,
        lv_coord_t w = width_(pbm->container), h = height_(pbm->container);
        
        _size(obj, w, h);
        _maximize_client_area(obj);
        _use_layout(obj, false);
        _update_layout(obj);
        _center(obj);

        style_probe_obj(obj, rect_width_percent, rect_height_percent);

        lv_obj_move_background(obj);
    );
#endif

    return pbm;
}

void probe_btn_matrix_destroy(probe_btn_matrix_t *pbm) {
    if (!pbm) return;

    // Delete the LVGL objects (container and buttons).  The buttons are
    // children of the container, so deleting the container will delete them.
    if (pbm->container) {
        lv_obj_del(pbm->container);
    }
    if (pbm->quick_mode_chk) {
        lv_obj_del(pbm->quick_mode_chk);
    }

    free(pbm);
}

// ------- settings tab --------
static void slider_event_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    tab_probe_t *tp = (tab_probe_t *) lv_event_get_user_data(e);
    lv_obj_t *textbox = (lv_obj_t*) lv_obj_get_user_data(slider);

    int i;
    for (i = 0; i < num_probe_settings; i++) {
        if (slider == probe_settings[i].slider) { break; }
    }
    if (i >= num_probe_settings || !slider) { return; }

    if (textbox && tp) {
        float value = lv_slider_get_value(slider) / 10.0f;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", value);
        lv_textarea_set_text(textbox, buf);

        if (probe_settings[i].param_name == 'H') tp->settings.width_dia = value;
        else if (probe_settings[i].param_name == 'I') tp->settings.length = value;
        else if (probe_settings[i].param_name == 'Z') tp->settings.depth_dist = value;
        else if (probe_settings[i].param_name == 'T') tp->settings.surf_clear = value;
        else if (probe_settings[i].param_name == 'C') tp->settings.corner_clear = value;
        else if (probe_settings[i].param_name == 'O') tp->settings.overtravel = value;
    }
}

// Reset the setting to its default value when the label is clicked
static void label_reset_event_cb(lv_event_t *e) {
    lv_obj_t *label = lv_event_get_target(e);
     tab_probe_t *tp = (tab_probe_t *) lv_event_get_user_data(e);

    // Find the setting based on the label text
    for (size_t i = 0; i < num_probe_settings; i++) {
        if (strcmp(lv_label_get_text(label), probe_settings[i].label) == 0) {
            lv_obj_t* slider = (lv_obj_t*) lv_obj_get_user_data(label);
            if (slider) {
                lv_slider_set_value(slider, (int)(probe_settings[i].default_value * 10.0f), LV_ANIM_OFF);
                lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
                break;
            }
        }
    }
}

// --- TabProbe Functions ---

tab_probe_t *tab_probe_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab) {
    tab_probe_t *tp = (tab_probe_t *)malloc(sizeof(tab_probe_t));
    if (!tp) {
        LV_LOG_ERROR("Failed to allocate tab_probe_t");
        return NULL;
    }
    memset(tp, 0, sizeof(tab_probe_t));

    tp->tab = tab;
    tp->interface = interface;

    // Initialize settings with default values
    tp->settings.width_dia = probe_settings[0].default_value;
    tp->settings.length = probe_settings[1].default_value;
    tp->settings.depth_dist = probe_settings[2].default_value;
    tp->settings.surf_clear = probe_settings[3].default_value;
    tp->settings.corner_clear = probe_settings[4].default_value;
    tp->settings.overtravel = probe_settings[5].default_value;
    tp->settings.wcs = (int) probe_settings[6].default_value; // Cast to int.  This is an INDEX.
    tp->settings.quick_mode = (int) probe_settings[7].default_value; //cast to int

    _flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    tab_probe_init_probe_tabv(tp, tab);
    tab_probe_init_axis_float_btn(tp);

    return tp;
}

void tab_probe_destroy(tab_probe_t *tp) {
    if (!tp) return;

    // Destroy nested objects
    if (tp->btns_2d_in) probe_btn_matrix_destroy(tp->btns_2d_in);
    if (tp->btns_2d_out) probe_btn_matrix_destroy(tp->btns_2d_out);
    if (tp->btns_3d) probe_btn_matrix_destroy(tp->btns_3d);
    if (tp->btns_surf) probe_btn_matrix_destroy(tp->btns_surf);

    // Free settings slider array, if allocated
    //if (tp->settings_sliders) {
    //    free(tp->settings_sliders);
    //}

    // Delete LVGL objects (the tabview and its children)
    if (tp->main_tabs) {
        lv_obj_del(tp->main_tabs); // Deletes child tabs and their contents too
    }
   if (tp->float_btn)
   {
    lv_obj_del(tp->float_btn);
   }
    free(tp);
}

static void tab_probe_update_quick(lv_event_t *e)
{
    lv_obj_t * tabv = lv_event_get_target(e);
     tab_probe_t *tp = (tab_probe_t *)lv_event_get_user_data(e);
     if (!tp) return;

    if (tp->settings.quick_mode == 1) {
         if(tp->btns_2d_in) lv_obj_add_state(tp->btns_2d_in->quick_mode_chk, LV_STATE_CHECKED);
         if(tp->btns_2d_out) lv_obj_add_state(tp->btns_2d_out->quick_mode_chk, LV_STATE_CHECKED);
         if(tp->btns_3d) lv_obj_add_state(tp->btns_3d->quick_mode_chk, LV_STATE_CHECKED); // will add.
    } else {
        if(tp->btns_2d_in) lv_obj_clear_state(tp->btns_2d_in->quick_mode_chk, LV_STATE_CHECKED);
        if(tp->btns_2d_out) lv_obj_clear_state(tp->btns_2d_out->quick_mode_chk, LV_STATE_CHECKED);
        if(tp->btns_3d) lv_obj_clear_state(tp->btns_3d->quick_mode_chk, LV_STATE_CHECKED); // will add.
    }
}

void tab_probe_init_probe_tabv(tab_probe_t *tp, lv_obj_t *parent) {
    _pad_all(parent, 5, LV_STATE_DEFAULT);

    lv_obj_t *tabv = tp->main_tabs = lv_tabview_create(parent);
    if(!tp->main_tabs) {
        LV_LOG_ERROR("Failed to create probe tabview!");
        return;
    }

    _tv_bar_pos(tabv, LV_DIR_LEFT);
    _tv_bar_size(tabv, TAB_WIDTH);

    lv_obj_t *tab_content = lv_tabview_get_content(tp->main_tabs);
    _flag(tab_content, LV_OBJ_FLAG_SCROLLABLE, false);

    tp->tab_settings = lv_tabview_add_tab(tp->main_tabs, "Setup");
    tp->tab_wcs = lv_tabview_add_tab(tp->main_tabs, "WCS");
    tp->tab_probe = lv_tabview_add_tab(tp->main_tabs, "3-axis");
    tp->tab_probe_2d = lv_tabview_add_tab(tp->main_tabs, "2-axis");
    tp->tab_surf = lv_tabview_add_tab(tp->main_tabs, "Surface");

    lv_obj_add_event_cb(tp->main_tabs, tab_probe_update_quick, LV_EVENT_VALUE_CHANGED, tp); // tp as user data

    tab_probe_init_sets_tab(tp, tp->tab_settings);
    tab_probe_init_probe_tab_2d(tp, tp->tab_probe_2d);
    tab_probe_init_probe_tab_3d(tp, tp->tab_probe);
    tab_probe_init_surface_tab(tp, tp->tab_surf);
    tab_probe_init_wcs_tab(tp, tp->tab_wcs);
}

void tab_probe_init_sets_tab(tab_probe_t *tp, lv_obj_t *tab) {
    _pad_all(tab, 2, LV_STATE_DEFAULT);

    lv_obj_t *grid = lv_obj_create(tab);
    if(!grid)
    {
        LV_LOG_ERROR("grid alloc failed");
        return;
    }
    lv_obj_center(grid);
    _size(grid, lv_pct(100), lv_pct(100));
    _flag(grid, LV_OBJ_FLAG_SCROLLABLE, false);

    // Define columns: [label_width, textbox_width, slider_width, LV_GRID_TEMPLATE_LAST]
    lv_coord_t _cols[] = {100, 60, lv_grid_fr(1), LV_GRID_TEMPLATE_LAST};
    // Define rows based on the number of settings
    lv_coord_t *rows = (lv_coord_t *)malloc((num_probe_settings + 1) * sizeof(lv_coord_t));
    if (!rows)
    {
        LV_LOG_ERROR("failed to alloc rows array");
        return;
    }
    for (size_t i = 0; i < num_probe_settings; i++) {
        rows[i] = 31;  // Fixed height for each row
    }
    rows[num_probe_settings] = LV_GRID_TEMPLATE_LAST;

    lv_coord_t *cols = malloc(sizeof(_cols));
    if (!cols) { LV_LOG_ERROR("failed to alloc cols array"); return; }
    memcpy(cols, _cols, sizeof(_cols));

    lv_obj_set_grid_dsc_array(grid, cols, rows);

    _layout(grid, LV_LAYOUT_GRID);
    _style_local(grid, bg_opa, LV_PART_MAIN, LV_OPA_TRANSP);
    _style_local(grid, border_width, LV_PART_MAIN, 0);
    _style_local(grid, outline_width, LV_PART_MAIN, 0);

    tp->sets_grid = grid;

    // Create UI elements for each setting
    for (size_t i = 0; i < num_probe_settings; i++) {
        if (probe_settings[i].default_value != NAN) // Only for numerical settings
        {
            if (!probe_settings[i].is_slider) {
                continue;
            }

            lv_obj_t *label = lv_label_create(grid);
            if(!label) {
                LV_LOG_ERROR("failed to create settings label");
                return;
            }

            _label_text(label, probe_settings[i].label);
            lv_obj_set_grid_cell(label, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, i, 1);
             _flag(label, LV_OBJ_FLAG_CLICKABLE, true);

            lv_obj_t *textbox = lv_textarea_create(grid);
            if(!textbox) {
                LV_LOG_ERROR("Failed to create setting textbox");
                return;
            }
            lv_textarea_set_one_line(textbox, true);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f", probe_settings[i].default_value);
            lv_textarea_set_text(textbox, buf);
             lv_obj_set_grid_cell(textbox, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, i, 1);

            lv_obj_t *slider = lv_slider_create(grid);
            if (!slider) {
                 LV_LOG_ERROR("Failed to create setting slider");
                return;
            }
            lv_slider_set_range(slider, (int)(probe_settings[i].min_value * 10.0f), (int)(probe_settings[i].max_value * 10.0f));
            lv_slider_set_value(slider, (int)(probe_settings[i].default_value * 10.0f), LV_ANIM_OFF);

             lv_obj_set_grid_cell(slider, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_CENTER, i, 1);
            _pad_all(slider, 5, LV_STATE_DEFAULT);

             // store slider pointer for focused()
             lv_obj_set_user_data(slider, textbox);

             // store tab_probe in slider and textbox so we can access the settings and update them.
             lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, tp);

             lv_obj_set_user_data(label, slider);
             lv_obj_add_event_cb(label, label_reset_event_cb, LV_EVENT_CLICKED, tp);

            lv_obj_set_user_data(textbox, slider);

            //store slider:
            probe_settings[i].slider = slider;
        }
    }
}

static void cb_set_wcs(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    tab_probe_t *tp = (tab_probe_t *)lv_event_get_user_data(e);
    if (obj && tp) {
        tp->settings.wcs = lv_btnmatrix_get_selected_btn(obj); // Get selected *index*
        printf("WCS: %d\n\n", tp->settings.wcs);
    }
}

static const char *wcs_map[] = {
        "G54", "G55", "G56", "\n",
        "G57", "G58", "G59", "\n",
        "G59.1", "G59.2", "G59.3", 
        NULL
    };

void tab_probe_init_wcs_tab(tab_probe_t *tp, lv_obj_t *tab) {
    _flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    // Create the button matrix for WCS selection
     tp->wcs_buttons = lv_btnmatrix_create(tab);
    if (!tp->wcs_buttons) {
        LV_LOG_ERROR("Failed to create WCS button matrix");
        return;
    }
    // Create a 2D array of strings for the button matrix map
    lv_btnmatrix_set_map(tp->wcs_buttons, wcs_map);
    lv_btnmatrix_set_one_checked(tp->wcs_buttons, true);
    lv_btnmatrix_set_btn_ctrl_all(tp->wcs_buttons, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_btn_ctrl(tp->wcs_buttons, 0, LV_BTNMATRIX_CTRL_CHECKED); // G54 checked
    lv_obj_add_event_cb(tp->wcs_buttons, cb_set_wcs, LV_EVENT_CLICKED, tp); // Pass tp as user_data

    _update_layout(tp->wcs_buttons);
    _size(tp->wcs_buttons, lv_pct(100), lv_pct(100));
    _center(tp->wcs_buttons);

}

void tab_probe_init_surface_tab(tab_probe_t *tp, lv_obj_t *tab) {
    _style_local(tab, pad_all, LV_PART_MAIN, 5);
    _style_local(tab, margin_all, LV_PART_MAIN, 0);
    _maximize_client_area(tab);
    _size(tab, lv_pct(100), lv_pct(100));
    _bg_opa(tab, 0, _M);

    lv_obj_t *container = lv_obj_create(tab);
     if (!container) {
        LV_LOG_ERROR("surface tab container alloc failed");
        return;
    }
    _maximize_client_area(container);
    _style_local(container, pad_all, LV_PART_MAIN, 5);
    _bg_opa(tab, 0, _M);
    _center(container);

    tp->btns_surf = probe_btn_matrix_create(container, tp, surface_probe_modes,
                                            surface_probe_modes_rows, surface_probe_modes_cols, 0.4, 0.3, false);
    if(!tp->btns_surf)
    {
        LV_LOG_ERROR("Failed to create surface probe matrix");
    }
}

void tab_probe_init_probe_tab_2d(tab_probe_t *tp, lv_obj_t *tab2d) {
    _style_local(tab2d, pad_all, LV_PART_MAIN, 5);
    _style_local(tab2d, margin_all, LV_PART_MAIN, 0);

    lv_obj_t *tabv = lv_tabview_create(tab2d);
    _tv_bar_pos(tabv, LV_DIR_TOP);
    _tv_bar_size(tabv, 40);
     if(!tabv) {
        LV_LOG_ERROR("2d probe tabview alloc failed");
        return;
     }

    lv_obj_t *tab_out = lv_tabview_add_tab(tabv, "Outside -> In");
    _flex_flow(tab_out, LV_FLEX_FLOW_COLUMN);
    tp->btns_2d_out = probe_btn_matrix_create(tab_out, tp, probe_modes_2d_out, probe_modes_2d_out_rows, probe_modes_2d_out_cols, 0.9, 0.75, true);
     if(!tp->btns_2d_out) {
         LV_LOG_ERROR("2d out probe matrix alloc failed");
        return;
     }

    lv_obj_t *tab_in = lv_tabview_add_tab(tabv, "Inside -> Out");
   _flex_flow(tab_in, LV_FLEX_FLOW_COLUMN);
    tp->btns_2d_in = probe_btn_matrix_create(tab_in, tp, probe_modes_2d_in, probe_modes_2d_in_rows, probe_modes_2d_in_cols, 0.5, 0.5, true);
    if(!tp->btns_2d_in) {
        LV_LOG_ERROR("2d in probe matrix alloc failed");
    }

    lv_obj_add_event_cb(tabv, tab_probe_update_quick, LV_EVENT_VALUE_CHANGED, tp); // tp as user data
}

void tab_probe_init_probe_tab_3d(tab_probe_t *tp, lv_obj_t *tab3d) {
    _style_local(tab3d, pad_all, LV_PART_MAIN, 5);
    _style_local(tab3d, margin_all, LV_PART_MAIN, 0);
     lv_obj_t *container = lv_obj_create(tab3d);
     if (!container) {
        LV_LOG_ERROR("surface tab container alloc failed");
        return;
    }
    _style_local(container, pad_all, LV_PART_MAIN, 5);
    _style_local(container, margin_all, LV_PART_MAIN, 0);
    _style_local(container, border_width, LV_PART_MAIN, 0);
    _style_local(container, bg_opa, LV_PART_MAIN, LV_OPA_TRANSP);

    _flex_flow(container, LV_FLEX_FLOW_COLUMN);

    tp->btns_3d = probe_btn_matrix_create(container, tp, probe_modes_3d, probe_modes_3d_rows, probe_modes_3d_cols, 0.5, 0.5, true);
}

static void axis_float_btn_cb(lv_event_t *e)
{
    // tab_probe_t *tp = (tab_probe_t *)lv_event_get_user_data(e); // not used here
     lv_obj_t *label = lv_event_get_target(e);
    if(label)
    {
        lv_obj_t* parent = lv_obj_get_parent(label);
        if (parent)
        {
            // change axis and update button:
            tab_probe_t *tp = (tab_probe_t *)lv_event_get_user_data(e);
            axis_t next_axis = jog_dial_next_axis(tp->interface->tab_jog->jog_dial);
            lv_label_set_text(label, axes_options[next_axis]); // get axis string and set.
        }
    }
}

static void axis_change_cb(axis_t axis, void *user_data)
{
   lv_obj_t * float_btn_label = (lv_obj_t*) user_data;
   if (float_btn_label) {
    lv_label_set_text(float_btn_label, axes_options[axis]); // get current axis.
   }
}

void tab_probe_init_axis_float_btn(tab_probe_t *tp) {

    if(!tp->interface->tab_jog)
    {
        LV_LOG_ERROR("tab jog not initialized");
        return; //TODO: should we fail here instead?
    }

    tp->float_btn = lv_button_create(tp->tab);
    if(!tp->float_btn){
        LV_LOG_ERROR("Failed to create float_btn");
        return;
    }
    _size(tp->float_btn, 45, 45);
    _flag(tp->float_btn, LV_OBJ_FLAG_FLOATING, true);
    lv_obj_align(tp->float_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *label = lv_label_create(tp->float_btn);
    if (!label)
    {
        LV_LOG_ERROR("Failed to create float_btn label");
        lv_obj_del(tp->float_btn);
        return;
    }
    lv_label_set_text(label, axes_options[tp->interface->tab_jog->jog_dial->axis]); // set initial axis.
    lv_obj_center(label);

    lv_obj_add_event_cb(label, axis_float_btn_cb, LV_EVENT_CLICKED, tp); // tp as user data
    jog_dial_add_axis_change_cb(tp->interface->tab_jog->jog_dial, axis_change_cb, label);  // Pass label as user_data
    _style_local(tp->float_btn, radius, LV_PART_MAIN, LV_RADIUS_CIRCLE);
}

// --- Modal stubs (replace with your actual modal implementation) ---

// static void home_modal(interface_t *interface) {
//   // Implement your home modal here (or call your existing modal functions)
//     LV_LOG_WARN("Home modal triggered (placeholder)");
// }