// tab_status.h
#ifndef __TAB_STATUS_H__
#define __TAB_STATUS_H__

#include "lvgl.h"
#include "ui/interface.h"
#include "ui/components/lv_axis_position_display.h"
#include "ui/gen_views/feed_rate_view.h"

struct tab_status_t {
  lv_obj_t *tabv;
  lv_obj_t *tab;
  interface_t *interface;

  lv_obj_t *x_axis_display;
  lv_obj_t *y_axis_display;
  lv_obj_t *z_axis_display;

  feed_rate_view_t *spindle_bar_view;
  feed_rate_view_t *feed_bar_view;
};

typedef struct tab_status_t tab_status_t;

tab_status_t *tab_status_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab);

void tab_status_set_x_wcs_pos(tab_status_t *ts, float pos, int wcs_index);
void tab_status_set_y_wcs_pos(tab_status_t *ts, float pos, int wcs_index);
void tab_status_set_z_wcs_pos(tab_status_t *ts, float pos, int wcs_index);

void tab_status_set_x_abs_pos(tab_status_t *ts, float pos);
void tab_status_set_y_abs_pos(tab_status_t *ts, float pos);
void tab_status_set_z_abs_pos(tab_status_t *ts, float pos);

void tab_status_set_x_dist_to_move(tab_status_t *ts, float delta);
void tab_status_set_y_dist_to_move(tab_status_t *ts, float delta);
void tab_status_set_z_dist_to_move(tab_status_t *ts, float delta);

bool tab_status_is_x_selected(tab_status_t *ts);
bool tab_status_is_y_selected(tab_status_t *ts);
bool tab_status_is_z_selected(tab_status_t *ts);

void tab_status_destroy(tab_status_t *ts);

#endif // __TAB_STATUS_H__