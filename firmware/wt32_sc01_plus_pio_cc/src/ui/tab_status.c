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

    memset(ts, 0, sizeof(*ts));
    ts->tabv = tabv;
    ts->tab = tab;
    ts->interface = interface;
    _maximize_client_area(tab);
    __scrollable(tab, false);
 
    lv_obj_t *grid_cont = lv_obj_create(tab);
    lv_obj_t * header, *sidebar, *content, *footer;

    view(
        grid_cont,
        _parent_size(lv_pct(100), lv_pct(100)),

        label(header, "Header", _height(30)),
        list(sidebar),
        label(content, "Testing the text area....", _align(LV_ALIGN_CENTER, 0, 0)),
        label(footer, "Footer", _height(30))        
    );

    /*
    lv_obj_set_size(grid_cont, lv_pct(100), 240);
    lv_obj_center(grid_cont);

    lv_obj_t *header = lv_label_create(grid_cont); lv_label_set_text(header, "Header");
    lv_obj_t *sidebar = lv_list_create(grid_cont);
    lv_obj_t *content = lv_textarea_create(grid_cont);
    lv_obj_t *footer = lv_label_create(grid_cont); lv_label_set_text(footer, "Footer");
    */

    // Apply Grid Layout
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Winitializer-overrides"

    _layout_grid(grid_cont,
        // Columns: 80px fixed | 1 fraction
        _cols(_px(80), _fr(1)),
        // Rows: 30px fixed | 1 fraction | 25px fixed
        _rows(_px(30), _fr(1), _px(25)),

        // Item Placements:
        _grid_item(.obj = header, .row = 0, .col = 0, .col_span = 2, .h_align = LV_GRID_ALIGN_CENTER),
        _grid_item(.obj = sidebar, .row = 1, .col = 0), // Defaults to stretch
        _grid_item(.obj = content, .row = 1, .col = 1), // Defaults to stretch
        _grid_item(.obj = footer, .row = 2, .col = 0, .col_span = 2, .h_align = LV_GRID_ALIGN_CENTER)
    );

    #pragma clang diagnostic pop

    return ts;
}