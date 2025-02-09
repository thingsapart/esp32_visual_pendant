// dialogs.cpp
#include "ui/dialogs.hpp"
#include "ui/event_handler.hpp"

#include <iostream> //Used for Serial out
#include <map>

lv_obj_t* button_dialog(const char* title, const char* text, bool add_close_btn,
                       const std::vector<std::string>& btns,
                       const std::vector<evt_handler_t>& btn_cbs) {
    lv_obj_t* mbox = lv_msgbox_create(lv_screen_active());
    lv_msgbox_add_title(mbox, title);
    lv_msgbox_add_text(mbox, text);
    if (add_close_btn) lv_msgbox_add_close_button(mbox);

    for (size_t i = 0; i < btns.size(); ++i) {
        const auto btn = btns[i];
        lv_obj_t* lbtn = lv_msgbox_add_footer_button(mbox, btn.c_str());
        //lv_obj_add_event_cb(lbtn, invoke_event_handler, LV_EVENT_CLICKED, (void *) cb);
        lv_obj_add_event_fn(lbtn, LV_EVENT_CLICKED, btn_cbs[i]);
    }

    lv_obj_center(mbox);
    return mbox;
}