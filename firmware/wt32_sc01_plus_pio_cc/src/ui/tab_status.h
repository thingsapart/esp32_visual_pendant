#ifndef __TAB_STATUS_H__
#define __TAB_STATUS_H__

#include "lvgl.h"
#include "interface.h"

struct tab_status_t {
  lv_obj_t *tabv;
  lv_obj_t *tab;
  interface_t *interface;
};

typedef struct tab_status_t tab_status_t;

tab_status_t *tab_status_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab);

#endif // __TAB_STATUS_H__