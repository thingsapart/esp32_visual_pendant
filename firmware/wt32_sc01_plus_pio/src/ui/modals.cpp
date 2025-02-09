#include "modals.hpp"
#include "ui/dialogs.hpp" // Assuming this is where your ui.dialogs.button_dialog function resides

// Global variable definition
bool active_modal = false;

bool modal_active() {
    return active_modal;
}

void close_modal(lv_obj_t* m) {
    // lv_obj_del_async(m); //If close_async is needed
    active_modal = false;
}

lv_obj_t* home_modal(Interface* interface) {
    active_modal = true;
    const char* title = "Machine not homed";
    const char* text = "\nHome machine now?\n";
    std::vector<const char*> btns = {"Home All", "Cancel"};

    // Define callbacks using std::function.
    std::vector<std::function<void(lv_obj_t*)>> cbs = {
        [interface](lv_obj_t* mbox) {
            close_modal(mbox);
            interface->machine->homeAll();
        },
        [](lv_obj_t* mbox) {
            close_modal(mbox);
        }
    };

    lv_obj_t* mbox = button_dialog(title, text, false, btns, cbs);
    return mbox;
}

lv_obj_t* home_modal_(Interface* interface) {
    lv_obj_t* mbox = lv_msgbox_create(lv_screen_active());
    active_modal = true;

    lv_msgbox_add_title(mbox, "Machine not home");
    lv_msgbox_add_text(mbox, "\nHome machine now?\n");

    lv_obj_t* btn_home = lv_msgbox_add_footer_button(mbox, "Home All");
    lv_obj_add_event_cb(btn_home, [](lv_event_t* e) {
        lv_obj_t* mbox = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        Interface* interface = (Interface*)lv_obj_get_user_data(mbox);
        interface->machine->homeAll();
        close_modal(mbox);
    }, LV_EVENT_CLICKED, interface); // Store interface pointer in user_data

    lv_obj_t* btn_cancel = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
        lv_obj_t* mbox = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        close_modal(mbox);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_center(mbox);

    return mbox;
}