#ifndef __EVENT_HANDLER__
#define __EVENT_HANDLER__

#include <lvgl.h>

#include <functional>
#include <map>

static void evt_invoke_event_handler(lv_event_t *e);

typedef std::function<void (lv_event_t *)> evt_handler_t;

void lv_obj_add_event_fn(lv_obj_t *obj, lv_event_code_t code, const evt_handler_t handler);

#define evt_bind(method, self) (std::bind(&method, self, std::placeholders::_1))
#define evt_data_obj(evt) (static_cast<lv_obj_t *>(lv_event_get_user_data(evt)))
#define evt_target_obj(evt) (static_cast<lv_obj_t *>(lv_event_get_target(evt)))


#endif //__EVENT_HANDLER__