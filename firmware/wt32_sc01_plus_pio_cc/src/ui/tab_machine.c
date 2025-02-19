// tab_machine.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ui/tab_machine.h"
#include "ui/ui_helpers.h" // Your LVGL helper macros

#include "debug.h"

// --- Static Data (Chip Load Ranges) ---
// For now, we'll just define simplified placeholders.  A full implementation
// would require a more complex data structure to represent the nested dictionaries.
static const char *materials[] = {"Aluminium", "Hard Wood", "MDF", "Soft Wood", "Acrylic", "Hard Plastic", "Soft Plastic", NULL};
static const char *mills[] = {"1/8\"", "3mm", "1/4\"", "6mm", "3/8\"", "10mm", "1/2\"", "12mm", NULL}; // Example

#define mill_options "1/8\"\n3mm\n1/4\"\n6mm\n3/8\"\n10mm\n1/2\"\n12mm"
static const float mills_dia[] = {3.125, 3.0, 6.3, 6, 9.525, 10, 12.5, 12};

static const char * mill_map[] = { // an array of the above const arrays.
    mill_options,
    mill_options,
    mill_options,
    mill_options,
    mill_options,
    mill_options,
    mill_options
};

static const char *flutes[] = {"1F", "2F", "3F", "4F", NULL};

static const char ** axes = (const char*[]) { "X", "Y", "Z", NULL };
static const size_t num_axes = 3;

// --- Helper Functions ---

static lv_obj_t *container_col(lv_obj_t *parent) {
    lv_obj_t *cont = lv_obj_create(parent);
     if (!cont) {
        LV_LOG_ERROR("container_col: Failed to create container");
        return NULL;
    }
    _size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    _flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    return cont;
}

static lv_obj_t *container_row(lv_obj_t *parent) {
    lv_obj_t *cont = lv_obj_create(parent);
    if (!cont) {
        LV_LOG_ERROR("container_row: Failed to create container");
        return NULL;
    }
    _size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    _flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    return cont;
}

// --- Event Handlers ---
static void axis_float_btn_cb(lv_event_t *e)
{
     lv_obj_t *label = lv_event_get_target(e);
    if(label)
    {
        lv_obj_t* parent = lv_obj_get_parent(label);
        if (parent)
        {
              // change axis and update button:
             tab_machine_t *tm = (tab_machine_t *)lv_event_get_user_data(e);
             axis_t next_axis = jog_dial_next_axis(tm->interface->tab_jog->jog_dial);
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

// --- file list click handlers, stubs. ---

// Placeholder.  Replace with your actual file list item clicked handlers.
static void gcode_clicked(file_list_t *filelist, const char *file) {
   // interface_t *interface = (interface_t *)user_data;
    // ui.dialogs.button_dialog(...); // You'll need a C version of this
     LV_LOG_USER("G-code file clicked: %s", file);
     // Example of starting a job (replace with your actual machine interface)
     // if (interface && interface->machine) {
    //   interface->machine->start_job(interface->machine, file);
    // }
}
// Placeholder.  Replace with your actual file list item clicked handlers.
static void macro_clicked(file_list_t *filelist, const char *file) {
    // interface_t *interface = (interface_t *)user_data;
    // ui.dialogs.button_dialog(...); // You'll need a C version of this
    LV_LOG_USER("Macro file clicked: %s", file);
    // Example of running a macro (replace with your actual machine interface)
    // if (interface && interface->machine) {
    //    char macro_path[128];
    //    snprintf(macro_path, sizeof(macro_path), "/macros/%s", file);
    //    interface->machine->run_macro(interface->machine, macro_path);
    // }
}

static void set_wcs_btns_cb(lv_event_t *e)
{
    lv_obj_t * btn = lv_event_get_target(e);
    machine_status_meter_t * msm = (machine_status_meter_t*) lv_event_get_user_data(e);
    if(btn && msm) {
          // Find which button was clicked by comparing pointers.
        for (size_t i = 0; i < num_axes; i++) {
            if (btn == msm->position->axis_labels[i]) {
                // Convert the axis name to a single character (X, Y, Z).
                char axis_char = msm->position->coords[i][0];  // Get the first character (e.g., 'X')
                msm->interface->machine->set_wcs_zero(msm->interface->machine, msm->interface->machine->wcs,  (const char *)&axis_char); // Pass as string.
                break;
            }
        }
    }
}

// Material dropdown changed
static void mat_dd_change(lv_event_t *e) {
   // lv_obj_t* dd = (lv_obj_t*) lv_event_get_target(e);
    machine_status_meter_t * msm = (machine_status_meter_t*) lv_event_get_user_data(e);
    if (msm) {
        size_t selected_index = lv_dropdown_get_selected(msm->material_dd);
        lv_dropdown_set_options_static(msm->mill_dd, mill_map[selected_index]);
    }
}

// mill dropdown change (no handler needed)

// --- TabMachine Functions ---

tab_machine_t *tab_machine_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab) {
    tab_machine_t *tm = (tab_machine_t *)malloc(sizeof(tab_machine_t));
    if (!tm) {
        LV_LOG_ERROR("Failed to allocate tab_machine_t");
        return NULL;
    }
    memset(tm, 0, sizeof(tab_machine_t));

    tm->tab = tab;
    tm->interface = interface;

   _flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    tab_machine_init_machine_tabv(tm, tab);
    tab_machine_init_axis_float_btn(tm);

    return tm;
}

void tab_machine_destroy(tab_machine_t *tm) {
    if (!tm) return;

    // Destroy nested objects (MachineStatusMeter, file_list_t, etc.)
    if (tm->mach_meter) machine_status_meter_destroy(tm->mach_meter);
    if (tm->jobs_list) file_list_destroy(tm->jobs_list); // Implement these
    if (tm->macro_list) file_list_destroy(tm->macro_list);


    // Delete LVGL objects (the tabview and its children)
    if (tm->main_tabs) {
        lv_obj_del(tm->main_tabs); // Deletes child tabs and their contents too
    }
    if (tm->float_btn)
    {
        lv_obj_del(tm->float_btn);
    }

    free(tm);
}

void tab_machine_init_axis_float_btn(tab_machine_t *tm) {
    if(!tm->interface->tab_jog) {
        LV_LOG_ERROR("tab jog not initialized");
        return; //TODO: should we fail here instead?
    }

    tm->float_btn = lv_button_create(tm->tab);
    if(!tm->float_btn) {
        LV_LOG_ERROR("Failed to create float_btn");
        return;
    }
    _size(tm->float_btn, 45, 45);
    _flag(tm->float_btn, LV_OBJ_FLAG_FLOATING, true);
    lv_obj_align(tm->float_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *label = lv_label_create(tm->float_btn);
    if (!label) {
        LV_LOG_ERROR("Failed to create float_btn label");
        lv_obj_del(tm->float_btn);
        return;
    }
    lv_label_set_text(label, axes_options[tm->interface->tab_jog->jog_dial->axis]); // set initial axis.
    lv_obj_center(label);

    lv_obj_add_event_cb(label, axis_float_btn_cb, LV_EVENT_CLICKED, tm); // tm as user data
    jog_dial_add_axis_change_cb(tm->interface->tab_jog->jog_dial, axis_change_cb, label);  // Pass label as user_data
    _style_local(tm->float_btn, radius, LV_PART_MAIN, LV_RADIUS_CIRCLE);
}

void tab_machine_init_machine_tabv(tab_machine_t *tm, lv_obj_t *parent) {
    _pad_all(parent, 5, LV_STATE_DEFAULT);

    tm->main_tabs = lv_tabview_create(parent);
    _tv_bar_pos(tm->main_tabs, LV_DIR_BOTTOM);
    _tv_bar_size(tm->main_tabs, TAB_HEIGHT);
    if (!tm->main_tabs) {
        LV_LOG_ERROR("Failed to create machine tabview");
        return; // Or handle the error appropriately
    }

    lv_obj_t *tab_content = lv_tabview_get_content(tm->main_tabs);
    _flag(tab_content, LV_OBJ_FLAG_SCROLLABLE, false);

    tm->tab_status = lv_tabview_add_tab(tm->main_tabs, "Status");
    tm->tab_macros = lv_tabview_add_tab(tm->main_tabs, "Macros");
    tm->tab_jobs = lv_tabview_add_tab(tm->main_tabs, "Jobs");

    _flag(tm->tab_status, LV_OBJ_FLAG_SCROLLABLE, false);
    _flag(tm->tab_jobs, LV_OBJ_FLAG_SCROLLABLE, false);
    _flag(tm->tab_macros, LV_OBJ_FLAG_SCROLLABLE, false);

    tab_machine_init_tab_status(tm, tm->tab_status);
    tab_machine_init_tab_jobs(tm, tm->tab_jobs);
    tab_machine_init_tab_macros(tm, tm->tab_macros);
}

void tab_machine_init_tab_status(tab_machine_t *tm, lv_obj_t *tab) {
    _maximize_client_area(tab);
    tm->mach_meter = machine_status_meter_create(tab, tm->interface, 16000, 6000);
    if (!tm->mach_meter) {
        LV_LOG_ERROR("Failed to create machine status meter");
        return; // Or handle the error
    }
    _size(tm->mach_meter->container, lv_pct(100), lv_pct(100));
}

void tab_machine_init_tab_jobs(tab_machine_t *tm, lv_obj_t *tab) {
    return;
    tm->jobs_list = file_list_create(tab, "gcodes", tm->interface->machine, gcode_clicked);
    if (!tm->jobs_list) {
        // Handle error
        assert(false && "TODO");
    }
    _size(tm->jobs_list->list, lv_pct(100), lv_pct(100)); // Assuming file_list_t has a container
    _style_local(tab, margin_all, LV_PART_MAIN, 0);
    _style_local(tab, pad_all, LV_PART_MAIN, 0);
    _style_local(tm->jobs_list->list, margin_all, LV_PART_MAIN, 0);
    _style_local(tm->jobs_list->list, pad_all, LV_PART_MAIN, 0);
    LV_LOG_WARN("init tab jobs:  not implemented");
}

void tab_machine_init_tab_macros(tab_machine_t *tm, lv_obj_t *tab) {
    return;
    tm->macro_list = file_list_create(tab, "macros", tm->interface->machine, macro_clicked);
    if (!tm->macro_list) {
       // Handle error
    }
    _size(tm->macro_list->list, lv_pct(100), lv_pct(100));  // Assuming file_list_t has a container
    _style_local(tab, margin_all, LV_PART_MAIN, 0);
    _style_local(tab, pad_all, LV_PART_MAIN, 0);
    _style_local(tm->macro_list->list, margin_all, LV_PART_MAIN, 0);
    _style_local(tm->macro_list->list, pad_all, LV_PART_MAIN, 0);

    LV_LOG_WARN("init tab macros:  not implemented");
}

// --- MachineStatusMeter Implementation ---

machine_status_meter_t *machine_status_meter_create(lv_obj_t *parent,
                                                    interface_t *interface,
                                                    int spindle_max_rpm,
                                                    int max_feed) {
    machine_status_meter_t *msm = (machine_status_meter_t *)malloc(sizeof(machine_status_meter_t));
     if (!msm) {
        LV_LOG_ERROR("Failed to allocate machine_status_meter_t");
        return NULL;
    }
    memset(msm, 0, sizeof(machine_status_meter_t));

    msm->interface = interface;
    msm->spindle_max_rpm = (spindle_max_rpm / 100) * 100; // Round down to nearest 100
    msm->max_feed = (max_feed / 100) * 100; // Round down to nearest 100

    msm->container = mk_container("msm:container", parent,
        _size(obj, lv_pct(100), lv_pct(100));
        _maximize_client_area(obj);
        _flex_flow(obj, LV_FLEX_FLOW_ROW);
        _pad_row(obj, 0);
        _pad_column(obj, 0);

        msm->left_side = mk_container("msm:left", outer_obj,            
            _flex_grow(obj, 1);
            _size(obj, lv_pct(60), lv_pct(100));
            _bg_opa(obj, LV_OPA_TRANSP, _M);
            _maximize_client_area(obj);
            _style_local(obj, pad_top, LV_PART_MAIN, 5);
            _text_font(obj, &lv_font_montserrat_12, _M);
            _flex_flow(obj, LV_FLEX_FLOW_COLUMN);

            mk_label(NULL, outer_obj,
                _maximize_client_area(obj);
                _label_text(obj, "Feed (mm/min):");
                _size(obj, LV_SIZE_CONTENT, 16);
            );

            msm->bar_feed = mk_bar("msm:bar_feed", outer_obj,
                lv_bar_set_range(obj, 0, msm->max_feed);
                _maximize_client_area(obj);
                _bg_opa(obj, LV_OPA_TRANSP, _M);
                _pads(obj, 5, 18, 5, 18);
                _size(obj, lv_pct(100), 18);
                lv_bar_set_value(obj, 5000, LV_ANIM_ON);
                //_bar_indicator(obj, bar_feed, LV_OPA_COVER, lv_color_hex(0x00DD00), lv_color_hex(0x0000DD), LV_GRAD_DIR_HOR, 175, 3);
                _flag(obj, LV_OBJ_FLAG_ADV_HITTEST, true);
            );
            msm->scale_feed = mk_scale(NULL, outer_obj,
                _maximize_client_area(obj);
                _margins(obj, -12, 18, 0, 18); 
                _pad_all(obj, 0, _M);
                _size(obj, lv_pct(100), 20);
                lv_scale_set_mode(obj, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
                lv_scale_set_total_tick_count(obj, msm->max_feed / 1000 + 1);
                lv_scale_set_major_tick_every(obj, 1);
                lv_scale_set_label_show(obj, true);
                lv_scale_set_range(obj, 0, msm->max_feed / 1000);
            );

            // --- Spindle RPM Section ---
            mk_label(NULL, outer_obj,
                _label_text(obj, "Spindle (RPM):");
                _maximize_client_area(obj);
                _size(obj, LV_SIZE_CONTENT, 16);
            );

            msm->spindle_rpm = mk_bar("msm:spindle_rpm", outer_obj,
                _maximize_client_area(obj);
                lv_bar_set_range(obj, 0, msm->spindle_max_rpm);
                _pads(obj, 5, 18, 5, 18);
                _size(obj, lv_pct(100), 18);
                _bg_opa(obj, LV_OPA_TRANSP, _M);
                lv_bar_set_value(obj, 5000, LV_ANIM_ON); // example value.
            );
            msm->scale_spindle_rpm = mk_scale(NULL, outer_obj,
                _maximize_client_area(obj);
                _margins(obj, -12, 18, 0, 18); 
                _pad_all(obj, 0, _M);
                _size(obj, lv_pct(100), 20);

                lv_scale_set_mode(obj, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
                lv_scale_set_total_tick_count(obj, msm->spindle_max_rpm / 1000 + 1);
                lv_scale_set_major_tick_every(obj, 5);
                lv_scale_set_label_show(obj, true);
                lv_scale_set_range(obj, 0, msm->spindle_max_rpm / 1000);
            );


            // Chipload Scale + Label container.
            mk_container(NULL, outer_obj,
                _size(obj, lv_pct(100), LV_SIZE_CONTENT);
                _flag(obj, LV_OBJ_FLAG_SCROLLABLE, false);
                _flex_flow(obj, LV_FLEX_FLOW_COLUMN);
                _maximize_client_area(obj);
                _scrollable(obj, false);

                mk_label(NULL, outer_obj,
                    _label_text(obj, "Chip Load (x0.1mm)");
                    _maximize_client_area(obj);
                    _size(obj, LV_SIZE_CONTENT, 16);
                );

                // Chipload scale container.
                mk_container(NULL, outer_obj,
                    _maximize_client_area(obj);
                    _flex_grow(obj, 1);
                    _scrollable(obj, false);
                    _pads(obj, 0, 18, 10, 18);
                    _flex_flow(obj, LV_FLEX_FLOW_COLUMN);
                    _size(obj, lv_pct(100), LV_SIZE_CONTENT);
                    _scrollable(obj, false);

                    msm->bar_cl = mk_bar("msm:bar_spnd_cl", outer_obj,
                        lv_bar_set_range(obj, 0, 50);
                        _margin(obj, 0);
                        _bg_opa(obj, LV_OPA_TRANSP, _M);
                        _pads(obj, 5, 18, 5, 18);
                        _size(obj, lv_pct(100), 18);
                        _flag(obj, LV_OBJ_FLAG_ADV_HITTEST, true);
                        lv_bar_set_value(obj, 35, LV_ANIM_ON);

                        _bar_indicator(obj, spnd_bar_cl, LV_OPA_COVER, lv_color_hex(0x00DDDD), lv_color_hex(0x00DD00), LV_GRAD_DIR_HOR, (int)(255 * 0.3f), 3);
                    );

                    msm->scale_spindle_chipload = mk_scale("msm:scl_spnd_cl", outer_obj,
                        _maximize_client_area(obj);
                        _margins(obj, -12, 18, 0, 18); 
                        _pad_all(obj, 0, _M);
                        _size(obj, lv_pct(100), 20);

                        lv_scale_set_mode(obj, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
                        lv_scale_set_total_tick_count(obj, 11);
                        lv_scale_set_major_tick_every(obj, 5);
                        lv_scale_set_label_show(obj, true);
                        lv_scale_set_range(obj, 0, 50);

                        _bar_indicator(obj, spnd_scale_cl, LV_OPA_COVER, lv_color_hex(0x00DDDD), lv_color_hex(0x00DD00), LV_GRAD_DIR_HOR, (int)(255 * 0.3f), 3);
                    );
                );
            );

            // --- Material and End Mill Dropdowns ---
            mk_container(NULL, outer_obj,
                _bg_opa(obj, LV_OPA_TRANSP, _M);
                _maximize_client_area(obj);
                _pad_bottom(obj, 5, LV_STATE_DEFAULT);
                _flex_flow(obj, LV_FLEX_FLOW_ROW);
                _pad_column(obj, 5);
                _size(obj, lv_pct(100), LV_SIZE_CONTENT);

                msm->material_dd = mk_dropdown("msm:mat_dd", outer_obj,
                    lv_dropdown_set_options_static(obj, "Aluminium\nH.Wood\nMDF\nS.Wood\nAcrylic\nH.Plastic\nS.Plastic");
                    _maximize_client_area(obj);
                    _flex_grow(obj, 1);
                    lv_obj_add_event_cb(obj, mat_dd_change, LV_EVENT_VALUE_CHANGED, msm);
                ); 
                msm->mill_dd = mk_dropdown("msm:mill_dd", outer_obj,
                    lv_dropdown_set_options_static(obj, mill_options);
                    _maximize_client_area(obj);
                    _flex_grow(obj, 1);
                    lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, msm);
                );
                msm->flute_dd = mk_dropdown("msm:flute_dd", outer_obj,
                    lv_dropdown_set_options_static(obj, "1F\n2F\n3F\n4F");
                    _maximize_client_area(obj);
                    _flex_grow(obj, 1);
                );
            );

        );

        msm->right_side = mk_container(NULL, outer_obj,
            _maximize_client_area(obj);
            _flex_flow(obj, LV_FLEX_FLOW_ROW_WRAP);
            _size(obj, 220, lv_pct(100));
            lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
            _bg_opa(obj, LV_OPA_TRANSP, _M);
            _border_width(obj, 0, _M);
            _pad_right(obj, 10, _M);
            _pad_bottom(obj, 10, _M);
            _text_font(obj, &lv_font_montserrat_12, _M);
            _scrollable(obj, false);

            // --- Machine Coordinates ---
            static const char *default_coord_systems[] = {"Mach", "WCS"}; // Use static strings
            static const size_t num_default_coord_systems = sizeof(default_coord_systems) / sizeof(default_coord_systems[0]);

#if !HIDE_MACH_STATUS_METER_POS_WCS
            msm->position = machine_position_wcs_create(outer_obj, axes, num_axes, msm->interface, 6, default_coord_systems, num_default_coord_systems , 40); // exclude "Move"
            if (!msm->position) {
                LV_LOG_ERROR("Failed to create machine position display");
                _d(2, "Failed to create machine position display");

                return msm;
            }
            // machine_position_wcs_align_to(msm->position, msm->right_side, LV_ALIGN_TOP_MID, 0, 0);
            _margin_top(msm->position->container, 20, LV_STATE_DEFAULT);

            for (size_t i = 0; i < num_axes; i++) {
                mk_btn("msm:wcs:x", outer_obj,
                    msm->position->axis_labels[i] = obj;
                    lv_obj_add_flag(obj, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
                    _maximize_client_area(obj);
                    _size(obj, 150, 25);
                    lv_obj_center(obj);
                    lv_obj_add_event_cb(obj, set_wcs_btns_cb, LV_EVENT_CLICKED, msm); // save msm as user data.

                    mk_label(NULL, outer_obj,
                        _maximize_client_area(obj);
                        _center(obj);

                        char buf[65];
                        snprintf (buf, 64, "%s Set WCS origin %s", LV_SYMBOL_REFRESH, axes[i]);
                        _label_text(obj, buf);
                    );
                );
            }
#endif
        );
    );

    return msm;
}

void machine_status_meter_destroy(machine_status_meter_t *msm) {
    if (!msm) return;

     if (msm->position) {
        machine_position_wcs_destroy(msm->position); // Destroy the MachinePositionWCS object
    }

    // Delete the LVGL objects (container and its children)
    if (msm->container) {
        lv_obj_del(msm->container);
    }
     // Finally, free the main structure
    free(msm);
}

