// tab_status.c
#include "tab_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/lv_vfl.h"
#include "ui/lv_views.h"

#include "interface.h"
#include "debug.h"
#include "ui/components/lv_axis_position_display.h"

static const char *TAG = "ui/tab_status";

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides" // If needed for view/layout macros

tab_status_t *tab_status_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab) {
    tab_status_t *ts = malloc(sizeof(tab_status_t));
    if (!ts) { LOGE(TAG, "Failed to alloc tab_status_t"); return NULL; }

    memset(ts, 0, sizeof(*ts));
    ts->tabv = tabv;
    ts->tab = tab;
    ts->interface = interface; // Keep if needed
    _maximize_client_area(tab); // Assuming these are helper macros/functions
    __scrollable(tab, false);

    // --- Create the main container using your existing structure ---
    lv_obj_t *grid_cont = lv_obj_create(tab);

    ts->x_axis_display = lv_axis_position_display_create(grid_cont, "X");
    ts->y_axis_display = lv_axis_position_display_create(grid_cont, "Y");
    ts->z_axis_display = lv_axis_position_display_create(grid_cont, "Z");

    view(
        grid_cont,
        __expand_client_area(), // Assuming these modify the parent `grid_cont`

        sub_view(ts->x_axis_display,
            __pad_left(20),
            __pad_right(20),
            selector(LV_PART_POSITION, __text_font(interface->font_kode_24)),
            selector(LV_PART_AXIS_LABEL, __text_font(interface->font_kode_24))
        ),
        sub_view(ts->y_axis_display,
            __pad_left(20),
            __pad_right(20)
            selector(LV_PART_POSITION, __text_font(interface->font_kode_24)),
            selector(LV_PART_AXIS_LABEL, __text_font(interface->font_kode_24))
        ),
        sub_view(ts->z_axis_display,
            __pad_left(20),
            __pad_right(20),
            selector(LV_PART_POSITION, __text_font(interface->font_kode_24)),
            selector(LV_PART_AXIS_LABEL, __text_font(interface->font_kode_24))
        )
    );

    _layout_v(grid_cont, LV_FLEX_ALIGN_CENTER,
        __width(grid_cont, lv_pct(50)), // Set width of the container holding the axis displays

        _sized(ts->x_axis_display, lv_pct(100), LV_SIZE_CONTENT),
        _sized(ts->y_axis_display, lv_pct(100), LV_SIZE_CONTENT),
        _sized(ts->z_axis_display, lv_pct(100), LV_SIZE_CONTENT)
    );

    return ts;
}

void tab_status_destroy(tab_status_t *ts) {
    free(ts);
}

void tab_status_set_x_wcs_pos(tab_status_t *ts, float pos, int wcs_index) {
    if (ts && ts->x_axis_display) {
        lv_axis_position_display_set_wcs_pos(ts->x_axis_display, pos, wcs_index);
    }
}
void tab_status_set_y_wcs_pos(tab_status_t *ts, float pos, int wcs_index) {
    if (ts && ts->y_axis_display) {
        lv_axis_position_display_set_wcs_pos(ts->y_axis_display, pos, wcs_index);
    }
}
void tab_status_set_z_wcs_pos(tab_status_t *ts, float pos, int wcs_index) {
    if (ts && ts->z_axis_display) {
        lv_axis_position_display_set_wcs_pos(ts->z_axis_display, pos, wcs_index);
    }
}

void tab_status_set_x_abs_pos(tab_status_t *ts, float pos) {
    if (ts && ts->x_axis_display) {
        lv_axis_position_display_set_abs_pos(ts->x_axis_display, pos);
    }
}
void tab_status_set_y_abs_pos(tab_status_t *ts, float pos) {
    if (ts && ts->y_axis_display) {
        lv_axis_position_display_set_abs_pos(ts->y_axis_display, pos);
    }
}
void tab_status_set_z_abs_pos(tab_status_t *ts, float pos) {
    if (ts && ts->z_axis_display) {
        lv_axis_position_display_set_abs_pos(ts->z_axis_display, pos);
    }
}

void tab_status_set_x_dist_to_move(tab_status_t *ts, float delta) {
    if (ts && ts->x_axis_display) {
        lv_axis_position_display_set_dist_to_move(ts->x_axis_display, delta);
    }
}
void tab_status_set_y_dist_to_move(tab_status_t *ts, float delta) {
    if (ts && ts->y_axis_display) {
        lv_axis_position_display_set_dist_to_move(ts->y_axis_display, delta);
    }
}
void tab_status_set_z_dist_to_move(tab_status_t *ts, float delta) {
    if (ts && ts->z_axis_display) {
        lv_axis_position_display_set_dist_to_move(ts->z_axis_display, delta);
    }
}

bool tab_status_is_x_selected(tab_status_t *ts) {
     return (ts && ts->x_axis_display) ? lv_axis_position_display_is_selected(ts->x_axis_display) : false;
}

bool tab_status_is_y_selected(tab_status_t *ts) {
     return (ts && ts->y_axis_display) ? lv_axis_position_display_is_selected(ts->y_axis_display) : false;
}

bool tab_status_is_z_selected(tab_status_t *ts) {
     return (ts && ts->z_axis_display) ? lv_axis_position_display_is_selected(ts->z_axis_display) : false;
}
