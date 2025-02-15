// machine_position_wcs.h
#ifndef MACHINE_POSITION_WCS_H
#define MACHINE_POSITION_WCS_H

#include "lvgl.h"
#include "machine/machine_interface.h"

typedef struct machine_position_wcs_t machine_position_wcs_t;
typedef struct interface_t interface_t;

struct machine_position_wcs_t {
    lv_obj_t *container;
    interface_t *interface;
    const char **coords;                // Array of axis names (e.g., "X", "Y", "Z")
    size_t num_coords;
    int digits;
    char fmt_str[16];                   // Format string for displaying coordinates (e.g., "%.2f")
    const char **coord_systems;         // Array of coordinate system names
    size_t num_coord_systems;
    int coord_sys_width;
    lv_obj_t **axis_labels;             // Array of axis labels
    lv_obj_t **coord_sys_labels;        // Array of coordinate system labels
    lv_obj_t ***coord_val_labels;       // 2D array: [coord_system_index][axis_index]
    float **coord_vals;                 // 2D array to store values
    size_t *axis_label_ids;             // store axis index
    lv_coord_t *row_dsc;                // row dims for button grid.
    lv_coord_t *col_dsc;                // col dims for button grid.
};

machine_position_wcs_t *machine_position_wcs_create(lv_obj_t *parent,
                                                    const char **coords,
                                                    size_t num_coords,
                                                    interface_t *interface,
                                                    int digits,
                                                    const char **coord_systems,
                                                    size_t num_coord_systems,
                                                    int coord_sys_width);

void machine_position_wcs_destroy(machine_position_wcs_t *mp);
void machine_position_wcs_coords_undefined(machine_position_wcs_t *mp);
void machine_position_wcs_set_coord(machine_position_wcs_t *mp, size_t ax, float v,
                                      const char *coord_system);
void machine_position_wcs_align_to(machine_position_wcs_t* mp, lv_obj_t* target, lv_align_t align, lv_coord_t x_ofs, lv_coord_t y_ofs);

#endif // MACHINE_POSITION_WCS_H