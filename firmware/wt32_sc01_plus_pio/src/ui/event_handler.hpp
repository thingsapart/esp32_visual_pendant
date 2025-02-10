#ifndef __EVENT_HANDLER__
#define __EVENT_HANDLER__

#include <lvgl.h>

#include <functional>
#include <map>

static void evt_invoke_event_handler(lv_event_t *e);

typedef std::function<void (lv_event_t *)> evt_handler_t;

void lv_obj_add_event_fn(lv_obj_t *obj, lv_event_code_t code, const evt_handler_t handler);

#define evt_bind(method, self) (std::bind(&method, self, std::placeholders::_1))

#endif //__EVENT_HANDLER__