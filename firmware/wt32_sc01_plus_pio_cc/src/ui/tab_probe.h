// tab_probe.h
#ifndef TAB_PROBE_H
#define TAB_PROBE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GCODE_POSITION_PRECISION 4

typedef struct tab_probe_t tab_probe_t;
typedef struct probe_btn_matrix_t probe_btn_matrix_t;

struct interface_t;

// Forward declarations for nested structs
typedef struct tab_probe_t {
    lv_obj_t *tab;
    struct interface_t *interface;
    lv_obj_t *main_tabs;
    lv_obj_t *tab_settings;
    lv_obj_t *tab_wcs;
    lv_obj_t *tab_probe;
    lv_obj_t *tab_probe_2d;
    lv_obj_t *tab_surf;
    lv_obj_t *float_btn;

    lv_obj_t *sets_grid; // for settings tab.

    // Nested structs
    probe_btn_matrix_t *btns_2d_in;   // Use pointers, as these will be created later
    probe_btn_matrix_t *btns_2d_out;
    probe_btn_matrix_t *btns_3d; // will implement at the very end, let's get 2d working first.
    probe_btn_matrix_t *btns_surf; // 1D surface probing.

    lv_obj_t *wcs_buttons;

    // Settings (store as a struct for easier access)
    struct {
        float width_dia;
        float length;
        float depth_dist;
        float surf_clear;
        float corner_clear;
        float overtravel;
        int wcs;     // Store the *index* of the selected WCS, not a pointer
        int quick_mode; // 0 or 1 (false/true)
    } settings;

    // Keep track of settings UI elements for wheel events:
    //lv_obj_t **settings_sliders; // pointers to the sliders.
    //size_t n_settings_sliders; // size of settings_sliders[].
} tab_probe_t;

typedef struct {
    const char *gcode;
    const char **params; // Array of parameter names (strings).  NULL-terminated.
    const char *img_path;
    const char *description;
} probe_action_t;

struct probe_btn_matrix_t {
    lv_obj_t *container;
    tab_probe_t *tab_probe; // Parent tab
    const probe_action_t (*actions_)[];
    size_t num_rows;
    size_t num_cols;
    lv_obj_t *quick_mode_chk; // Checkbox for quick mode
};



// Function prototypes
tab_probe_t *tab_probe_create(lv_obj_t *tabv, struct interface_t *interface, lv_obj_t *tab);
void tab_probe_destroy(tab_probe_t *tp);
void tab_probe_init_probe_tabv(tab_probe_t *tp, lv_obj_t *parent);
void tab_probe_init_sets_tab(tab_probe_t *tp, lv_obj_t *tab);
void tab_probe_init_wcs_tab(tab_probe_t *tp, lv_obj_t *tab);
void tab_probe_init_surface_tab(tab_probe_t *tp, lv_obj_t *tab);
void tab_probe_init_probe_tab_2d(tab_probe_t *tp, lv_obj_t *tab2d);
void tab_probe_init_probe_tab_3d(tab_probe_t *tp, lv_obj_t *tab3d); // will implement last.
void tab_probe_init_axis_float_btn(tab_probe_t *tp);

probe_btn_matrix_t *probe_btn_matrix_create(lv_obj_t *parent,
                                             tab_probe_t *tab_probe,
                                             const probe_action_t (*actions)[],
                                             size_t num_rows,
                                             size_t num_cols,
                                             bool quick);
void probe_btn_matrix_destroy(probe_btn_matrix_t *pbm);

#if  __cplusplus
}
#endif


#endif // TAB_PROBE_H