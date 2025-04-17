// tab_status.c

#include "tab_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/lv_vfl.h"
#include "ui/lv_views.h"

#include "interface.h"

#include "debug.h"

static const char *TAG = "ui/tab_status";

tab_status_t *tab_status_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab) {
    tab_status_t *ts = malloc(sizeof(tab_status_t));
    if (!ts) { LOGE(TAG, "Failed to alloc tab_status_t"); return NULL; }

    memset(ts, 0, sizeof(ts));
    ts->tabv = tabv;
    ts->tab = tab;
    ts->interface = interface;
    
    lv_vfl_view_map_t grid_view_map[LV_DV_MAX_WIDGETS];
    size_t grid_view_map_count = 0;
    lv_obj_t *main_container = lv_obj_create(tabv);

    LV_VIEW(
        grid_view_map, grid_view_map_count,
        main_container,
        PARENT_SIZE(LV_PCT(100), LV_PCT(100)),
        PARENT_ALIGN(LV_ALIGN_CENTER, 0, 0),
        PARENT_STYLE_PAD_ALL(5),
        DIV(div_x,
            LABEL(label_x, "X", TEXT_COLOR(lv_color_white()), TEXT_FONT(interface->font_kode_20)),
            LABEL(label_x_pos, "0000.00", TEXT_COLOR(lv_color_white()), TEXT_FONT(interface->font_kode_20)),
            LABEL(spacer_x, NULL),
            LABEL(label_x_pos_g, "0000.00", TEXT_COLOR(lv_color_white()), TEXT_FONT(&lv_font_montserrat_14)),
        ),
        DIV(div_y,
            LABEL(label_y, "Y", TEXT_COLOR(lv_color_white()), TEXT_FONT(interface->font_kode_20)),
            LABEL(label_y_pos, "0000.00", TEXT_COLOR(lv_color_white()), TEXT_FONT(interface->font_kode_20)),
            LABEL(spacer_y, NULL),
            LABEL(label_y_pos_g, "0000.00", TEXT_COLOR(lv_color_white()), TEXT_FONT(&lv_font_montserrat_14)),
        ),
        DIV(div_z,
            LABEL(label_z, "Z", TEXT_COLOR(lv_color_white()), TEXT_FONT(interface->font_kode_20))
            LABEL(label_z_pos, "0000.00", TEXT_COLOR(lv_color_white()), TEXT_FONT(interface->font_kode_20)),
            LABEL(spacer_z, NULL),
            LABEL(label_z_pos_g, "0000.00", TEXT_COLOR(lv_color_white()), TEXT_FONT(&lv_font_montserrat_14)),
        )
    );

    const char *row_layout = 
        "G:|[label_x(50)]-[label_x_pos]|\n"
        "|[spacer_x]-[label_x_pos_g]|\n"
        
        "G:|[label_y(50)]-[label_y_pos]|\n"
        "|[spacer_y]-[label_y_pos_g]|\n"

        "G:|[label_z(50)]-[label_z_pos]|\n"
        "|[spacer_z]-[label_z_pos_g]|\n";

    lv_vfl_result_t res = lv_vfl_apply(main_container, row_layout, grid_view_map, grid_view_map_count, NULL, 0);
    if (res != LV_VFL_OK) {
        LOGE(TAG, "Failed to create tab_status layout: %d", res);
    }

    return ts;
}