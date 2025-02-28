#include "modals.h"

#include "debug.h"

static lv_obj_t *current_modal;

void set_curr_modal_and_close_prev(lv_obj_t *modal) {
    close_curr_modal();
    current_modal = modal;
} 

void close_curr_modal() {
    _d(0, "Closing modal\n");
    if (current_modal) {
        lv_msgbox_close(current_modal);
        current_modal = NULL;
    }
}

lv_obj_t *create_modal() {
    close_curr_modal();
    current_modal = lv_msgbox_create(lv_scr_act());
    return current_modal;
}

void modal_close_handler(lv_event_t *e) {
    _d(0, "Event Closing modal\n");
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (current_modal == mbox) {
        close_curr_modal();
    } else {
        lv_msgbox_close(mbox);
    }
    lv_obj_del_async(mbox);
}

lv_obj_t *button_modal(const char *title, const char *text, const char *buttons[], const modal_button_cb_t btn_cbs[], void *mbox_user_data) {
    lv_obj_t *mbox = create_modal();
    lv_msgbox_add_title(mbox, title);
    lv_obj_t *txt = lv_msgbox_add_text(mbox, text);

    lv_label_set_recolor(txt, true);

    for (size_t i = 0; buttons[i] != NULL; i++) {
        lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, buttons[i]);
        if (btn_cbs[i] != NULL) {
            lv_obj_add_event_cb(btn, btn_cbs[i], LV_EVENT_CLICKED, mbox);
        }
    }
    lv_obj_add_event_cb(mbox, modal_close_handler, LV_EVENT_CANCEL, mbox);
    lv_obj_set_user_data(mbox, mbox_user_data);
    lv_obj_center(mbox);

    return mbox;
}

static void _home_all(lv_event_t *e) {
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    machine_interface_t *mach = (machine_interface_t *)lv_obj_get_user_data(mbox);
    mach->home_all(mach);

    modal_close_handler(e);
}

lv_obj_t *home_modal(machine_interface_t *mach) {
    const char *buttons[] = { "Home all", "Cancel", NULL };
    void (*btn_cbs[])(lv_event_t *e) = { _home_all, modal_close_handler, NULL };
    lv_obj_t *mbox = button_modal("Home machine?",
        "Need to home all axes first.\n\nHome all axes?",
        buttons,
        btn_cbs,
        mach
    );

    return mbox;
}

lv_obj_t *message_modal(const char *title, const char *text) {
    const char *buttons[] = { "Ok", NULL };
    void (*btn_cbs[])(lv_event_t *e) = { modal_close_handler, NULL };
    lv_obj_t *mbox = button_modal(title,
        text,
        buttons,
        btn_cbs,
        NULL
    );

    return mbox;
}

