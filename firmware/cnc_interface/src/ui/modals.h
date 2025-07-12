#ifndef __MODALS_H__
#define __MODALS_H__

#include "lvgl.h"
#include "machine/machine_interface.h"

typedef void (*modal_button_cb_t)(lv_event_t *e);

// Close the current modal. One modal at a time.
void close_curr_modal();
// The functions below automatically close the current modal, use
// this function when manually creating a modal, making sure
// to close any pending existing ones.
void set_curr_modal_and_close_prev(lv_obj_t *modal);

lv_obj_t *button_modal(const char *title, const char *text,
                       const char *buttons[], const modal_button_cb_t btn_cbs[],
                       void *modal_user_data);

lv_obj_t *home_modal(machine_interface_t *mach);
lv_obj_t *message_modal(const char *title, const char *text);

void modal_close_handler(lv_event_t *e);

#endif  // __MODALS_H__