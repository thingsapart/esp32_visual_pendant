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
   if (float_btn_label)
   {
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
    if(btn && msm)
    {
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
static void mat_dd_change(lv_event_t *e)
{
   // lv_obj_t* dd = (lv_obj_t*) lv_event_get_target(e);
    machine_status_meter_t * msm = (machine_status_meter_t*) lv_event_get_user_data(e);
    if (msm) {
        size_t selected_index = lv_dropdown_get_selected(msm->material_dd);
        _df(0, "%zu => %s\n\n", selected_index, mill_map[selected_index]);
        fflush(stdout);
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
    if(!tm->interface->tab_jog)
    {
        LV_LOG_ERROR("tab jog not initialized");
        return; //TODO: should we fail here instead?
    }

    tm->float_btn = lv_button_create(tm->tab);
    if(!tm->float_btn){
        LV_LOG_ERROR("Failed to create float_btn");
        return;
    }
    _size(tm->float_btn, 45, 45);
    _flag(tm->float_btn, LV_OBJ_FLAG_FLOATING, true);
    lv_obj_align(tm->float_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *label = lv_label_create(tm->float_btn);
    if (!label)
    {
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
    tm->mach_meter = machine_status_meter_create(tab, tm->interface, 16000, 6000);
    if (!tm->mach_meter) {
        LV_LOG_ERROR("Failed to create machine status meter");
        return; // Or handle the error
    }
    _size(tm->mach_meter->container, lv_pct(100), lv_pct(100));
}

void tab_machine_init_tab_jobs(tab_machine_t *tm, lv_obj_t *tab) {
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

    msm->container = lv_obj_create(parent);
    if (!msm->container) {
        LV_LOG_ERROR("Failed to create container for machine_status_meter_t");
        free(msm);
        return NULL;
    }
    _size(msm->container, LV_PCT(100), LV_PCT(100));

    _style_local(msm->container, border_width, LV_PART_MAIN, 5);
    _style_local(msm->container, pad_all, LV_PART_MAIN, 0);
    _style_local(msm->container, margin_all, LV_PART_MAIN, 0);

    _flex_flow(msm->container, LV_FLEX_FLOW_ROW);
    _pad_row(msm->container, 0);
    _pad_column(msm->container, 0);

    msm->left_side = container_col(msm->container);
    if(!msm->left_side) {
         LV_LOG_ERROR("Failed to create msm->left_side");
         machine_status_meter_destroy(msm);
        return NULL;
    }
    lv_obj_set_flex_grow(msm->left_side, 5);
    _height(msm->left_side, LV_SIZE_CONTENT);
    _style_local(msm->left_side, bg_opa, LV_PART_MAIN, LV_OPA_TRANSP);
    _style_local(msm->left_side, border_width, LV_PART_MAIN, 0);
    _style_local(msm->left_side, pad_top, LV_PART_MAIN, 5);
    _style_local(msm->left_side, pad_bottom, LV_PART_MAIN, 0);
    _style_local(msm->left_side, pad_left, LV_PART_MAIN, 0);
    _style_local(msm->left_side, pad_right, LV_PART_MAIN, 0);
    lv_obj_set_style_text_font(msm->left_side, &lv_font_montserrat_12, LV_PART_MAIN);

    msm->right_side = lv_obj_create(msm->container);
    if(!msm->right_side) {
        LV_LOG_ERROR("Failed to create msm->right_side");
        machine_status_meter_destroy(msm);
        return NULL;
    }
    _height(msm->right_side, lv_pct(100));
    _width(msm->right_side, LV_SIZE_CONTENT);

    _flex_flow(msm->right_side, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(msm->right_side, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    _style_local(msm->right_side, bg_opa, LV_PART_MAIN, LV_OPA_TRANSP);
    _style_local(msm->right_side, border_width, LV_PART_MAIN, 0);
    _style_local(msm->right_side, pad_top, LV_PART_MAIN, 0);
    _style_local(msm->right_side, pad_bottom, LV_PART_MAIN, 0);
    _style_local(msm->right_side, pad_left, LV_PART_MAIN, 0);
    _style_local(msm->right_side, pad_right, LV_PART_MAIN, 10);
    _style_local(msm->right_side, margin_all, LV_PART_MAIN, 0);

    lv_obj_set_style_text_font(msm->right_side, &lv_font_montserrat_12, LV_PART_MAIN);
    _flag(msm->right_side, LV_OBJ_FLAG_SCROLLABLE, false);

    // --- Feed Section ---

    lv_obj_t *label_feed = lv_label_create(msm->left_side);
     if (!label_feed) {
        LV_LOG_ERROR("Failed to create label_feed");
        machine_status_meter_destroy(msm); // Clean up
        return NULL;
    }
    _label_text(label_feed, "Feed (mm/min):");

    msm->bar_feed = lv_bar_create(msm->left_side);
    if (!msm->bar_feed) {
        LV_LOG_ERROR("Failed to create bar_feed");
        machine_status_meter_destroy(msm); // Clean up
        return NULL;
    }
    lv_bar_set_range(msm->bar_feed, 0, msm->max_feed);
     _style_local(msm->bar_feed, margin_all, LV_PART_MAIN, 0);
    _style_local(msm->bar_feed, pad_top, LV_PART_MAIN, 5);
    _style_local(msm->bar_feed, pad_bottom, LV_PART_MAIN, 5);
    _style_local(msm->bar_feed, pad_left, LV_PART_MAIN, 18);
     _style_local(msm->bar_feed, pad_right, LV_PART_MAIN, 18);
    _style_local(msm->bar_feed, bg_opa, LV_PART_MAIN, LV_OPA_TRANSP);

    _height(msm->bar_feed, 18);
    _width(msm->bar_feed, lv_pct(100));
    lv_bar_set_value(msm->bar_feed, 5000, LV_ANIM_ON); // example value.

    //  Define the style for bar indicator
    lv_style_t style_indic;
    lv_style_init(&style_indic);
    lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
    lv_style_set_bg_color(&style_indic, lv_color_hex(0x00DD00)); // Green
    lv_style_set_bg_grad_color(&style_indic, lv_color_hex(0x0000DD)); // Blue
    lv_style_set_bg_grad_dir(&style_indic, LV_GRAD_DIR_HOR);
    lv_style_set_bg_main_stop(&style_indic, 175);
    lv_obj_add_style(msm->bar_feed, &style_indic, LV_PART_INDICATOR);
    lv_style_set_radius(&style_indic, 3);
    _flag(msm->bar_feed, LV_OBJ_FLAG_ADV_HITTEST, true);


    msm->scale_feed = lv_scale_create(msm->left_side);
    if (!msm->scale_feed) {
        LV_LOG_ERROR("Failed to create scale_feed");
        machine_status_meter_destroy(msm);
        return NULL;
    }
    _margin_top(msm->scale_feed, 0, LV_STATE_DEFAULT);
    _style_local(msm->scale_feed, margin_all, LV_PART_MAIN, 0);
    _style_local(msm->scale_feed, margin_left, LV_PART_MAIN, 18);
    _style_local(msm->scale_feed, margin_right, LV_PART_MAIN, 18);
    _style_local(msm->scale_feed, pad_all, LV_PART_MAIN, 0);

    lv_scale_set_mode(msm->scale_feed, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_scale_set_total_tick_count(msm->scale_feed, msm->max_feed / 1000 + 1);
    lv_scale_set_major_tick_every(msm->scale_feed, 1);
    lv_scale_set_label_show(msm->scale_feed, true);
    lv_scale_set_range(msm->scale_feed, 0, msm->max_feed / 1000);
    _height(msm->scale_feed, 30);
    _width(msm->scale_feed, lv_pct(100));

    // --- Spindle RPM Section ---
    lv_obj_t *label_spindle_rpm = lv_label_create(msm->left_side);
     if (!label_spindle_rpm) {
        LV_LOG_ERROR("Failed to create label_spindle_rpm");
        machine_status_meter_destroy(msm);
        return NULL;
    }
    _label_text(label_spindle_rpm, "Spindle (RPM):");
     lv_obj_set_style_text_font(label_spindle_rpm, &lv_font_montserrat_12, LV_PART_MAIN);

    msm->spindle_rpm = lv_bar_create(msm->left_side);
    if (!msm->spindle_rpm) {
        LV_LOG_ERROR("Failed to create spindle_rpm");
        machine_status_meter_destroy(msm); // Clean up
        return NULL;
    }
    lv_bar_set_range(msm->spindle_rpm, 0, msm->spindle_max_rpm);
     _style_local(msm->spindle_rpm, margin_all, LV_PART_MAIN, 0);
    _style_local(msm->spindle_rpm, pad_top, LV_PART_MAIN, 5);
    _style_local(msm->spindle_rpm, pad_bottom, LV_PART_MAIN, 5);
    _style_local(msm->spindle_rpm, pad_left, LV_PART_MAIN, 18);
     _style_local(msm->spindle_rpm, pad_right, LV_PART_MAIN, 18);
    _style_local(msm->spindle_rpm, bg_opa, LV_PART_MAIN, LV_OPA_TRANSP);

    _height(msm->spindle_rpm, 18);
    _width(msm->spindle_rpm, lv_pct(100));
    lv_bar_set_value(msm->spindle_rpm, 5000, LV_ANIM_ON); // example value.

    msm->scale_spindle_rpm = lv_scale_create(msm->left_side);
     if (!msm->scale_spindle_rpm)
     {
        LV_LOG_ERROR("failed to create scale_spindle_rpm");
         machine_status_meter_destroy(msm);
         return NULL;
     }
    lv_scale_set_mode(msm->scale_spindle_rpm, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_scale_set_total_tick_count(msm->scale_spindle_rpm, msm->spindle_max_rpm / 1000 + 1);
    _style_local(msm->scale_spindle_rpm, margin_all, LV_PART_MAIN, 0);
    _style_local(msm->scale_spindle_rpm, margin_left, LV_PART_MAIN, 18);
    _style_local(msm->scale_spindle_rpm, margin_right, LV_PART_MAIN, 18);
    _style_local(msm->scale_spindle_rpm, pad_all, LV_PART_MAIN, 0);

    lv_scale_set_major_tick_every(msm->scale_spindle_rpm, 5);
    lv_scale_set_label_show(msm->scale_spindle_rpm, true);
    lv_scale_set_range(msm->scale_spindle_rpm, 0, msm->spindle_max_rpm / 1000);
    _height(msm->scale_spindle_rpm, 30);
    _width(msm->scale_spindle_rpm, lv_pct(100));


    // --- Material and End Mill Dropdowns ---

    lv_obj_t *mat_end_container = container_row(msm->left_side);
    if(!mat_end_container) {
        LV_LOG_ERROR("failed to create mat_end_container");
         machine_status_meter_destroy(msm);
        return NULL;
    }
    _pad_bottom(mat_end_container, 5, LV_STATE_DEFAULT);

    msm->material_dd = lv_dropdown_create(mat_end_container);
    if (!msm->material_dd)
    {
        LV_LOG_ERROR("material_dd creation failed!");
         machine_status_meter_destroy(msm);
        return NULL;
    }
    lv_dropdown_set_options_static(msm->material_dd, "Aluminium\nH.Wood\nMDF\nS.Wood\nAcrylic\nH.Plastic\nS.Plastic");
    _width(msm->material_dd, lv_pct(40));
    lv_obj_add_event_cb(msm->material_dd, mat_dd_change, LV_EVENT_VALUE_CHANGED, msm);

    msm->mill_dd = lv_dropdown_create(mat_end_container);
     if (!msm->mill_dd)
    {
        LV_LOG_ERROR("mill_dd creation failed!");
         machine_status_meter_destroy(msm);
        return NULL;
    }

    lv_dropdown_set_options_static(msm->mill_dd, "1/8\"\n3mm\n1/4\"\n6mm\n3/8\"\n10mm\n1/2\"\n12mm");
    _width(msm->mill_dd, lv_pct(25));

    // Initialize with options for the default selected material (Aluminium)
    lv_obj_send_event(msm->material_dd, LV_EVENT_VALUE_CHANGED, msm);

    msm->flute_dd = lv_dropdown_create(mat_end_container);
     if (!msm->flute_dd)
    {
        LV_LOG_ERROR("flute_dd creation failed!");
         machine_status_meter_destroy(msm);
        return NULL;
    }
    _width(msm->flute_dd, lv_pct(25));
    lv_dropdown_set_options_static(msm->flute_dd, "1F\n2F\n3F\n4F");

    // --- Chip Load Section ---
    lv_obj_t *chip_ld_container = container_col(msm->left_side);
    if(!chip_ld_container) {
        LV_LOG_ERROR("failed to create chip load container");
         machine_status_meter_destroy(msm);
        return NULL;
    }
    _height(chip_ld_container, 60);
    _flag(chip_ld_container, LV_OBJ_FLAG_SCROLLABLE, false);

    lv_obj_t *label_spindle_chip = lv_label_create(chip_ld_container);
     if (!label_spindle_chip) {
        LV_LOG_ERROR("Failed to create label_spindle_chip");
         machine_status_meter_destroy(msm);
        return NULL;
    }
    _label_text(label_spindle_chip, "Chip Load (x0.1mm)");
    _width(label_spindle_chip, LV_SIZE_CONTENT);

    lv_obj_t *chip_ldmc = container_col(chip_ld_container);
    if(!chip_ldmc){
         LV_LOG_ERROR("Failed to create chip_ldmc");
        machine_status_meter_destroy(msm);
        return NULL;
    }
    lv_obj_set_flex_grow(chip_ldmc, 1);
    _flag(chip_ldmc, LV_OBJ_FLAG_SCROLLABLE, false);
    _style_local(chip_ldmc, pad_top, LV_PART_MAIN, 0);
    _style_local(chip_ldmc, pad_bottom, LV_PART_MAIN, 0);
    _style_local(chip_ldmc, pad_left, LV_PART_MAIN, 18);
    _style_local(chip_ldmc, pad_right, LV_PART_MAIN, 18);

    msm->bar_cl = lv_bar_create(chip_ldmc);
    if (!msm->bar_cl) {
        LV_LOG_ERROR("Failed to create bar_cl");
        machine_status_meter_destroy(msm);
        return NULL;
    }
    lv_bar_set_range(msm->bar_cl, 0, 50);
    _style_local(msm->bar_cl, margin_all, LV_PART_MAIN, 0);
    _style_local(msm->bar_cl, pad_top, LV_PART_MAIN, 5);
    _style_local(msm->bar_cl, pad_bottom, LV_PART_MAIN, 5);
    _style_local(msm->bar_cl, pad_left, LV_PART_MAIN, 0);
     _style_local(msm->bar_cl, pad_right, LV_PART_MAIN, 0);
    _style_local(msm->bar_cl, bg_opa, LV_PART_MAIN, LV_OPA_TRANSP);
    _height(msm->bar_cl, 18);
    _width(msm->bar_cl, lv_pct(100));
    _flag(msm->bar_cl, LV_OBJ_FLAG_ADV_HITTEST, true);
    lv_bar_set_value(msm->bar_cl, 35, LV_ANIM_ON); // example value.

     // Bar color style:
    lv_style_t style_barc;
    lv_style_init(&style_barc);
    lv_style_set_bg_opa(&style_barc, LV_OPA_COVER);
    lv_style_set_bg_color(&style_barc, lv_color_hex(0x00DD00)); // Green
    lv_style_set_bg_grad_color(&style_barc, lv_color_hex(0x0000DD)); // Blue
    lv_style_set_bg_grad_dir(&style_barc, LV_GRAD_DIR_HOR);
    lv_style_set_bg_main_stop(&style_barc, (int)(255 * 0.3f)); // Example stops
    lv_style_set_bg_grad_stop(&style_barc, (int)(255 * 0.5f));
    lv_obj_add_style(msm->bar_cl, &style_barc, LV_PART_INDICATOR); // apply style.
    lv_style_set_radius(&style_barc, 3);

    msm->scale_spindle_chipload = lv_scale_create(chip_ldmc);
    if(!msm->scale_spindle_chipload)
    {
        LV_LOG_ERROR("Failed to create scale_spindle_chipload");
         machine_status_meter_destroy(msm);
         return NULL;
    }
    lv_scale_set_mode(msm->scale_spindle_chipload, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_scale_set_total_tick_count(msm->scale_spindle_chipload, 11);
    lv_scale_set_major_tick_every(msm->scale_spindle_chipload, 5);
    lv_scale_set_label_show(msm->scale_spindle_chipload, true);
    lv_scale_set_range(msm->scale_spindle_chipload, 0, 50);
    _height(msm->scale_spindle_chipload, 30);
    _width(msm->scale_spindle_chipload, lv_pct(100));
    _style_local(msm->scale_spindle_chipload, margin_all, LV_PART_MAIN, 0);
    _style_local(msm->scale_spindle_chipload, pad_all, LV_PART_MAIN, 0);
    lv_obj_add_style(msm->scale_spindle_chipload, &style_barc, LV_PART_INDICATOR); // apply style

    // --- Machine Coordinates ---
    static const char *default_coord_systems[] = {"Mach", "WCS"}; // Use static strings
    static const size_t num_default_coord_systems = sizeof(default_coord_systems) / sizeof(default_coord_systems[0]);

    msm->position = machine_position_wcs_create(msm->right_side, axes, num_axes, msm->interface, 6, default_coord_systems, num_default_coord_systems , 40); // exclude "Move"
    if (!msm->position) {
        LV_LOG_ERROR("Failed to create machine position display");
        _d(2, "Failed to create machine position display");

        return msm;
    }
    // machine_position_wcs_align_to(msm->position, msm->right_side, LV_ALIGN_TOP_MID, 0, 0);
    _margin_top(msm->position->container, 20, LV_STATE_DEFAULT);

    // --- Set WCS Buttons ---
    for (size_t i = 0; i < num_axes; i++) {

          lv_obj_t* btn = lv_button_create(msm->right_side);
          if(!btn) {
            LV_LOG_ERROR("Failed to create set WCS btn");
             machine_status_meter_destroy(msm);
            return NULL;
          }

        lv_obj_t *lbl = lv_label_create(btn);
        if(!lbl) {
             LV_LOG_ERROR("failed to create wcs btn label");
            machine_status_meter_destroy(msm);
            return NULL;
        }
        char btn_text[32];
        lv_label_set_text(lbl, btn_text);
        _size(btn, 150, 25);
        lv_obj_center(lbl);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK); // new track in flex layout.
        msm->position->axis_labels[i] = btn; // store in axis_labels array in mpos.
        lv_obj_add_event_cb(btn, set_wcs_btns_cb, LV_EVENT_CLICKED, msm); // save msm as user data.
    }

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

