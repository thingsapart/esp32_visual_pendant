#ifndef __MODALS_H__
#define __MODALS_H__

#include "lvgl.h"

#include "machine/machine_interface.h"

lv_obj_t *button_modal(const char *title, const char *text, const char *buttons[], void (*btn_cbs[])(lv_event_t *e), void *modal_user_data);

lv_obj_t *home_modal(machine_interface_t *mach);

void modal_close_handler(lv_event_t *e);

#endif // __MODALS_H__