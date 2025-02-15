#include "machine_position.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ui_helpers.h"
#include "ui/interface.h"

#define FONT_WIDTH 11
#define FONT_HEIGHT 22

// --- Static Data (Coordinate Systems) ---
// Using pointers to string literals makes this ROM-resident
static const char *default_coord_systems[] = {"Mach", "WCS", "Move"};
static const size_t num_default_coord_systems = sizeof(default_coord_systems) / sizeof(default_coord_systems[0]);

// --- Event Handlers ---

static void label_home_clicked_event_handler(lv_event_t *e) {
  lv_obj_t *label = lv_event_get_target(e);
  machine_position_wcs_t *mp = (machine_position_wcs_t *)lv_event_get_user_data(e);

  if (mp) {
      // Find which axis label was clicked based on pointer comparison.
        for (size_t i = 0; i < mp->num_coords; i++) {
            if (label == mp->axis_labels[i]) {
                // Convert the axis name to a single character (X, Y, Z).
                char axis_char = mp->coords[i][0];  // Get the first character (e.g., 'X')
                mp->interface->machine->home(mp->interface->machine, (const char *)&axis_char); // Pass as string.
                break;
            }
        }
    }
}

static void wcs_label_clicked_event_handler(lv_event_t *e) {
    // machine_position_wcs_t *mp = (machine_position_wcs_t *)lv_event_get_user_data(e); // not used.
     lv_obj_t *label = lv_event_get_target(e);
     if (label) // avoid calling interface method if no interface defined yet.
     {
          // go to next wcs:
          machine_interface_t * iface = (machine_interface_t*) lv_event_get_user_data(e); // get interface from the event.
          iface->next_wcs(iface);
     }
}

// --- Callback Functions ---

static void pos_updated_callback(machine_interface_t *mach, void *ud) {
    // Find the machine_position_wcs_t associated with this interface.
    // This is more complex; you might need a lookup table if you have multiple
    // instances.  For this example, I'll assume a single global instance.
     machine_position_wcs_t *mp = (machine_position_wcs_t *) ud;

    if (!mp) return; // Early exit if no valid machine pointer

    for (size_t i = 0; i < mp->num_coords; i++) {
        machine_position_wcs_set_coord(mp, i, mach->position[i], mp->coord_systems[0]); // "Mach"
    }

    if (mp->num_coord_systems > 1) {
        for (size_t i = 0; i < mp->num_coords; i++) {
            machine_position_wcs_set_coord(mp, i, mach->wcs_position[i], mp->coord_systems[1]); // "WCS"
        }
        if(mp->num_coord_systems > 2) {
            for (size_t i = 0; i < mp->num_coords; i++)
            {
                float pos = mach->position[i];
                float wcs_pos = mach->wcs_position[i];

                if (!isnan(pos) && !isnan(wcs_pos)) // Check for valid numbers
                {
                     machine_position_wcs_set_coord(mp, i, wcs_pos - pos, mp->coord_systems[2]); // "Move"
                }
            }
        }
    }
}

static void home_updated_callback(machine_interface_t *mach, void *user_data) {
     machine_position_wcs_t *mp = (machine_position_wcs_t *) user_data;
    if (!mp) return;

     for (size_t i = 0; i < mp->num_coords; i++) {
        lv_color_t colr = mach->axes_homed[i] ? lv_color_hex(0x0000FF) : lv_color_hex(0xFF0000); // Blue : Red
        _bg_color(mp->axis_labels[i], colr, LV_PART_MAIN);
    }
    pos_updated_callback(mach, user_data); // Update positions after homing
}

static void wcs_updated_callback(machine_interface_t *mach, void *user_data) {
     machine_position_wcs_t *mp = (machine_position_wcs_t *) user_data;
      if (!mp) return;

    if (mp->num_coord_systems > 1) {
        _label_text(mp->coord_sys_labels[1], machine_interface_get_wcs_str(mach, mach->wcs));
    }
}

// --- Creation and Destruction ---
// Assumes coords is a static array
// Assumes coord_systems is a static array
machine_position_wcs_t *machine_position_wcs_create(lv_obj_t *parent,
                                                    const char **coords,
                                                    size_t num_coords,
                                                    interface_t *interface,
                                                    int digits,
                                                    const char **coord_systems,
                                                    size_t num_coord_systems,
                                                    int coord_sys_width) {

    // --- Argument Validation ---
    if (!parent || !coords || num_coords == 0 || !interface || digits < 3 ||
        !coord_systems || num_coord_systems == 0) {
        LV_LOG_ERROR("Invalid arguments to machine_position_wcs_create");
        return NULL;
    }

    // --- Memory Allocation ---
    machine_position_wcs_t *mp = (machine_position_wcs_t *)malloc(sizeof(machine_position_wcs_t));
    if (!mp) {
        LV_LOG_ERROR("Failed to allocate machine_position_wcs_t");
        return NULL;
    }
    memset(mp, 0, sizeof(machine_position_wcs_t));

    // Copy basic data
    mp->interface = interface;
    mp->coords = coords;
    mp->num_coords = num_coords;
    mp->digits = digits;
    snprintf(mp->fmt_str, sizeof(mp->fmt_str), "%%.%df", (digits - 2 > 0) ? digits -2 : 0);
    mp->coord_systems = coord_systems;
    mp->num_coord_systems = num_coord_systems;
    mp->coord_sys_width = coord_sys_width;

    // Allocate memory for axis labels
    mp->axis_labels = (lv_obj_t **)malloc(num_coords * sizeof(lv_obj_t *));
    if (!mp->axis_labels) {
        LV_LOG_ERROR("Failed to allocate axis_labels array");
        machine_position_wcs_destroy(mp); // Clean up
        return NULL;
    }
     memset(mp->axis_labels, 0, num_coords * sizeof(lv_obj_t*));

    // Allocate memory for coord_sys_labels
   mp->coord_sys_labels = (lv_obj_t **)malloc(num_coord_systems * sizeof(lv_obj_t *));
    if (!mp->coord_sys_labels) {
        LV_LOG_ERROR("Failed to allocate coord_sys_labels array");
        machine_position_wcs_destroy(mp); // Clean up
        return NULL;
    }
    memset(mp->coord_sys_labels, 0, num_coord_systems * sizeof(lv_obj_t*));

    // Allocate memory for coord_val_labels (2D array)
    mp->coord_val_labels = (lv_obj_t ***)malloc(num_coord_systems * sizeof(lv_obj_t **));
    if (!mp->coord_val_labels) {
        LV_LOG_ERROR("Failed to allocate coord_val_labels array");
        machine_position_wcs_destroy(mp);
        return NULL;
    }
    memset(mp->coord_val_labels, 0, num_coord_systems * sizeof(lv_obj_t**));

    for (size_t i = 0; i < num_coord_systems; i++) {
        mp->coord_val_labels[i] = (lv_obj_t **)malloc(num_coords * sizeof(lv_obj_t *));
        if (!mp->coord_val_labels[i]) {
            LV_LOG_ERROR("Failed to allocate coord_val_labels[%zu]", i);
            machine_position_wcs_destroy(mp); // Clean up
            return NULL;
        }
         memset(mp->coord_val_labels[i], 0, num_coords * sizeof(lv_obj_t*));
    }

     // Allocate memory for coord_vals (2D array)
    mp->coord_vals = (float **)malloc(num_coord_systems * sizeof(float *));
    if (!mp->coord_vals) {
         LV_LOG_ERROR("Failed to allocate coord_vals");
        machine_position_wcs_destroy(mp);
        return NULL;
    }
    memset(mp->coord_vals, 0, sizeof(float*) * num_coord_systems);

    for (size_t i = 0; i < num_coord_systems; i++)
    {
        mp->coord_vals[i] = (float*) malloc(num_coords * sizeof(float));
        if (!mp->coord_vals[i]) {
            LV_LOG_ERROR("Failed to allocate coord_vals[%zu]", i);
            machine_position_wcs_destroy(mp); // Clean up
            return NULL;
        }
        // Initialize all values to NaN.
        for (size_t j = 0; j < num_coords; j++) {
            mp->coord_vals[i][j] = NAN;
        }
    }


    // --- Create UI Elements ---
    mp->container = lv_obj_create(parent);
    if (!mp->container) {
        LV_LOG_ERROR("Failed to create container");
        machine_position_wcs_destroy(mp);
        return NULL;
    }
    // style_container_blank(mp->container); // Assuming you have this helper
    _style_local(mp->container, bg_opa, LV_PART_MAIN, LV_OPA_TRANSP); // No background.

    _pad_column(mp->container, 2);
    _pad_row(mp->container, 2);
    _layout(mp->container, LV_LAYOUT_GRID);
    _flag(mp->container, LV_OBJ_FLAG_SCROLLABLE, false);
     _style_local(mp->container, margin_all, LV_PART_MAIN, 0);
    _style_local(mp->container, pad_all, LV_PART_MAIN, 0);
    _style_local(mp->container, border_width, LV_PART_MAIN, 0);

    const lv_font_t *font = interface->font_lcd_24 ? interface->font_lcd_24 : lv_font_default(); // Assuming you have this font

    int lblc_width = (digits + 1) * FONT_WIDTH;

    // Define grid columns:  [coord_sys_width] [lblc_width] [lblc_width] ... [LV_GRID_TEMPLATE_LAST]
    mp->col_dsc = (lv_coord_t *)malloc((1 + num_coord_systems + 1) * sizeof(lv_coord_t));
     if (!mp->col_dsc) {
        LV_LOG_ERROR("Failed to allocate col_dsc");
        machine_position_wcs_destroy(mp);
        return NULL;
    }
    mp->col_dsc[0] = coord_sys_width;
    for (size_t i = 0; i < num_coord_systems; i++) {
        mp->col_dsc[i + 1] = lblc_width;
    }
    mp->col_dsc[num_coord_systems + 1] = LV_GRID_TEMPLATE_LAST;

    // Define grid rows: [FONT_HEIGHT + 2] [FONT_HEIGHT + 2] ... [LV_GRID_TEMPLATE_LAST]
    mp->row_dsc = (lv_coord_t *)malloc((num_coords + 1 + 1) * sizeof(lv_coord_t));
    if (!mp->row_dsc)
    {
        LV_LOG_ERROR("Failed to allocate row_dsc");
        free(mp->col_dsc); // clean up col_dsc as well.
        machine_position_wcs_destroy(mp);
        return NULL;

    }
    for (size_t i = 0; i < num_coords + 1; i++) {
        mp->row_dsc[i] = FONT_HEIGHT + 2;
    }
    mp->row_dsc[num_coords + 1] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_grid_dsc_array(mp->container, mp->col_dsc, mp->row_dsc);
    _width(mp->container, coord_sys_width + num_coord_systems * 2 + lblc_width * num_coord_systems + 2);
    _height(mp->container, LV_SIZE_CONTENT);

    // Create Axis labels.
    mp->axis_label_ids = malloc(num_coords * sizeof(size_t));
     if (!mp->axis_label_ids) {
        LV_LOG_ERROR("Failed to allocate axis_label_ids");
        machine_position_wcs_destroy(mp);
        return NULL;
    }
     memset(mp->axis_label_ids, 0, sizeof(size_t) * num_coords);

    for (size_t i = 0; i < num_coords; i++) {
        mp->axis_labels[i] = lv_label_create(mp->container);
        if (!mp->axis_labels[i]) {
            LV_LOG_ERROR("axis label alloc failed!");
            machine_position_wcs_destroy(mp);
            return NULL;
        }
        char label_text[10]; // " X"
        snprintf(label_text, sizeof(label_text), "%s %s", LV_SYMBOL_HOME, coords[i]); // Prepend symbol
        _label_text(mp->axis_labels[i], label_text);
        _style_local(mp->axis_labels[i], bg_opa, LV_PART_MAIN, 100);
        _style_local(mp->axis_labels[i], margin_all, LV_PART_MAIN, 1);
         _style_local(mp->axis_labels[i], pad_all, LV_PART_MAIN, 0);
         _style_local(mp->axis_labels[i], text_align, LV_PART_MAIN, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_grid_cell(mp->axis_labels[i], LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, i + 1, 1);
        lv_obj_add_event_cb(mp->axis_labels[i], label_home_clicked_event_handler, LV_EVENT_CLICKED, mp);
        _flag(mp->axis_labels[i], LV_OBJ_FLAG_CLICKABLE, true);
        mp->axis_label_ids[i] = i;  // Store integer index.

        // Set initial color based on homed state
        lv_color_t colr = interface->machine->axes_homed[i] ? lv_color_hex(0x0000FF) : lv_color_hex(0xFF0000);
        _bg_color(mp->axis_labels[i], colr, LV_PART_MAIN);
    }

     // Coordinate system labels.
    for (size_t i = 0; i < num_coord_systems; i++) {
       mp->coord_sys_labels[i] = lv_label_create(mp->container);
        if (!mp->coord_sys_labels[i]) {
            LV_LOG_ERROR("Failed to create coord sys labels!");
             machine_position_wcs_destroy(mp);
            return NULL;
        }
        _label_text(mp->coord_sys_labels[i], coord_systems[i]); // Set text from array
         _style_local(mp->coord_sys_labels[i], bg_color, LV_PART_MAIN, lv_color_hex(0x0000FF));
        _style_local(mp->coord_sys_labels[i], bg_opa, LV_PART_MAIN, 100);
         _style_local(mp->coord_sys_labels[i], margin_all, LV_PART_MAIN, 1);
        _style_local(mp->coord_sys_labels[i], pad_all, LV_PART_MAIN, 0);
        _style_local(mp->coord_sys_labels[i], text_align, LV_PART_MAIN, LV_TEXT_ALIGN_CENTER);

        lv_obj_set_grid_cell(mp->coord_sys_labels[i], LV_GRID_ALIGN_STRETCH, i + 1, 1,
                              LV_GRID_ALIGN_STRETCH, 0, 1);

         // Special case: Second entry (index 1) is used to switch WCS.
        if (i == 1) {
            lv_obj_add_event_cb(mp->coord_sys_labels[i], wcs_label_clicked_event_handler, LV_EVENT_CLICKED, interface); // note, pass *interface* as userdata.
            _flag(mp->coord_sys_labels[i], LV_OBJ_FLAG_CLICKABLE, true);
        }
    }

     // Coordinate value labels (2D array).
    for (size_t i = 0; i < num_coord_systems; i++) {
        for (size_t j = 0; j < num_coords; j++) {
            mp->coord_val_labels[i][j] = lv_label_create(mp->container);
             if (!mp->coord_val_labels[i][j]) {
                LV_LOG_ERROR("failed to create coord val label!");
                machine_position_wcs_destroy(mp);
                return NULL;
            }
            _label_text(mp->coord_val_labels[i][j], "?"); // Initialize with placeholder
            _style_local(mp->coord_val_labels[i][j], bg_color, LV_PART_MAIN, lv_color_hex(0x00FF00));
            _style_local(mp->coord_val_labels[i][j], bg_opa, LV_PART_MAIN, 60);
            _style_local(mp->coord_val_labels[i][j], margin_all, LV_PART_MAIN, 0);
             _style_local(mp->coord_val_labels[i][j], pad_all, LV_PART_MAIN, 2);
            _style_local(mp->coord_val_labels[i][j], text_align, LV_PART_MAIN, LV_TEXT_ALIGN_RIGHT);

            if (font) {
               lv_obj_set_style_text_font(mp->coord_val_labels[i][j], font, LV_PART_MAIN);
            }
            _width(mp->coord_val_labels[i][j], lblc_width);

            lv_obj_set_grid_cell(mp->coord_val_labels[i][j], LV_GRID_ALIGN_STRETCH, i + 1, 1,
                                  LV_GRID_ALIGN_STRETCH, j + 1, 1);
        }
    }

    machine_interface_t *mach = interface->machine;
    machine_interface_add_pos_changed_cb(mach, mp, pos_updated_callback);
    machine_interface_add_pos_changed_cb(mach, mp, pos_updated_callback);
    machine_interface_add_wcs_changed_cb(mach, mp, wcs_updated_callback);

    // Initial update
    wcs_updated_callback(interface->machine, mp);
    machine_position_wcs_coords_undefined(mp); // all undefined to begin with.

    return mp;
}

void machine_position_wcs_destroy(machine_position_wcs_t *mp) {
    if (!mp) return;

    // Free dynamically allocated resources in reverse order of creation

    // 1. Delete LVGL objects (labels, container)
     if (mp->container) {
        lv_obj_del(mp->container); // This will delete all child labels too
        mp->container = NULL; // Set to NULL after deletion
    }

    // 2. Free the 2D arrays (coord_val_labels, coord_vals)
    if (mp->coord_val_labels) {
        for (size_t i = 0; i < mp->num_coord_systems; i++) {
            if (mp->coord_val_labels[i]) {
                free(mp->coord_val_labels[i]);
            }
        }
        free(mp->coord_val_labels);
        mp->coord_val_labels = NULL;
    }

      if (mp->coord_vals) {
        for (size_t i = 0; i < mp->num_coord_systems; i++) {
            if (mp->coord_vals[i]) {
                free(mp->coord_vals[i]);
            }
        }
        free(mp->coord_vals);
        mp->coord_vals = NULL;
    }


    // 3. Free arrays of label pointers (axis_labels, coord_sys_labels)
      if (mp->axis_labels) {
        free(mp->axis_labels);
        mp->axis_labels = NULL;
    }

    if (mp->coord_sys_labels) {
        free(mp->coord_sys_labels);
        mp->coord_sys_labels = NULL;
    }
    if (mp->axis_label_ids)
    {
       free(mp->axis_label_ids);
       mp->axis_label_ids = NULL;
    }
    if (mp->col_dsc) { free(mp->col_dsc); mp->col_dsc = NULL; }
    if (mp->row_dsc) { free(mp->row_dsc); mp->row_dsc = NULL; }

    // Finally, free the main structure
    free(mp);
}

void machine_position_wcs_coords_undefined(machine_position_wcs_t *mp) {
   if (!mp) return;

    for (size_t i = 0; i < mp->num_coord_systems; i++) {
         for (size_t j = 0; j < mp->num_coords; j++) {
            mp->coord_vals[i][j] = NAN; // Set to NaN
            _label_text(mp->coord_val_labels[i][j], "?");
        }
    }
}

void machine_position_wcs_set_coord(machine_position_wcs_t *mp, size_t ax, float v,
                                      const char *coord_system) {
     if (!mp || ax >= mp->num_coords) return;


    // Find the index of the coordinate system
    size_t cs_index = 0;
    for (size_t i = 0; i < mp->num_coord_systems; i++) {
        if (strcmp(coord_system, mp->coord_systems[i]) == 0) {
            cs_index = i;
            break;
        }
    }
    // if we didn't find it, it's probably because we are initializing, so it's ok.
    if (isnan(v))
    {
         mp->coord_vals[cs_index][ax] = NAN; // Set to NaN
        _label_text(mp->coord_val_labels[cs_index][ax], "?");
    } else {
        // Update the value and the label
        if(mp->coord_vals[cs_index][ax] != v) {
             mp->coord_vals[cs_index][ax] = v;
            char buf[32];
            snprintf(buf, sizeof(buf), mp->fmt_str, v);
            _label_text(mp->coord_val_labels[cs_index][ax], buf);
        }
    }
}

void machine_position_wcs_align_to(machine_position_wcs_t* mp, lv_obj_t* target, lv_align_t align, lv_coord_t x_ofs, lv_coord_t y_ofs) {
    if (!mp || !target) return;
    lv_obj_align_to(mp->container, target, align, x_ofs, y_ofs);
}
