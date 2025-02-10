#include "ui/event_handler.hpp"

#include <vector>

#include "Arduino.h"

#include "ui/debug.hpp"

static std::map<lv_obj_t *, std::vector<evt_handler_t>> handlers;

static void evt_invoke_event_handler(lv_event_t *e) {
    Serial.println("Handler invoked");

    evt_handler_t *cb = static_cast<evt_handler_t *>(lv_event_get_user_data(e));
    _df(0, "Calling CB: %x\n", cb);
    (*cb)(e);
}

void obj_deleted(lv_event_t *e) {
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    //lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    handlers.erase(obj);
}

void lv_obj_add_event_fn(lv_obj_t *obj, lv_event_code_t code, const evt_handler_t handler) {
    handlers[obj].push_back(handler);
    evt_handler_t *cb = &handlers[obj].back();
    lv_obj_add_event_cb(obj, evt_invoke_event_handler, LV_EVENT_CLICKED, (void *) cb);
    lv_obj_add_event_cb(obj, obj_deleted, LV_EVENT_DELETE, obj);

    _df(0, "Adding CB: %x\n", cb);
}