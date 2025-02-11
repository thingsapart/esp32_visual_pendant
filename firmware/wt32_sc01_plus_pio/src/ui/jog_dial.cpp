#include <Arduino.h>
#include "lvgl.h"

#include "ui/jog_dial.hpp"
#include "ui/machine_position.hpp"
#include "ui/modals.hpp"
#include "ui/lv_style.hpp"
#include "ui/event_handler.hpp"

// Define static members
const std::vector<std::tuple<std::string, float>> JogDial::FEEDS = {
    {"100%", 1.0},
    {"25%", 0.25},
    {"10%", 0.1},
    {"1%", 0.01}
};

static const char * axes_btn_map[] = { "X", "\n", "Y", "\n", "Z", "\n", "Off", nullptr };
const std::vector<std::string> JogDial::AXES_OPTIONS = {"X", "Y", "Z", "Off"};
const std::vector<std::string> JogDial::AXES = {"X", "Y", "Z"};
const std::map<std::string, int> JogDial::AXIS_IDS = {
    {"X", 0},
    {"Y", 1},
    {"Z", 2},
    {"Off", -1}
};

JogDial::JogDial(lv_obj_t* parent, Interface* interface) :
    prev(0),
    parent(parent),
    interface(interface),
    feed(1.0),
    axis(AXES_OPTIONS.back()),
    axis_id(AXIS_IDS.at(AXES_OPTIONS.back())),
    last_rotary_pos(0) {

    this->interface->registerStateChangeCb([this](MachineInterface *itf) { this->_machineStateUpdated(itf); });

    lv_obj_center(parent);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* btns = lv_buttonmatrix_create(parent);

    lv_buttonmatrix_set_map(btns, axes_btn_map);
    lv_buttonmatrix_set_button_ctrl_all(btns, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_button_ctrl(btns, 0, LV_BUTTONMATRIX_CTRL_CHECKED); //Corrected argument order.
    lv_obj_set_size(btns, 60, LV_PCT(100));
    lv_obj_set_style_pad_ver(btns, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btns, 5, LV_PART_MAIN);
    axis = AXES_OPTIONS.back();
    lv_buttonmatrix_set_one_checked(btns, true);

    lv_obj_align(btns, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_fn(btns, LV_EVENT_VALUE_CHANGED, evt_bind(JogDial::_axisClicked, this));
    axis_btns = btns;

    arc = lv_arc_create(parent);
    lv_obj_t* arc_obj = arc;
    lv_obj_set_size(arc_obj, LV_PCT(100), LV_PCT(100));
    lv_arc_set_rotation(arc_obj, -90);
    lv_arc_set_bg_angles(arc_obj, 0, 360);
    lv_arc_set_angles(arc_obj, 0, 360);
    lv_obj_set_style_opa(arc_obj, LV_OPA_0, LV_PART_INDICATOR);
    lv_arc_set_value(arc_obj, 0);
    lv_obj_center(arc_obj);
    lv_obj_add_flag(arc_obj, LV_OBJ_FLAG_ADV_HITTEST);
    lv_arc_set_mode(arc_obj, LV_ARC_MODE_SYMMETRICAL);
    lv_obj_set_flex_grow(arc_obj, 1);
    // style(arc_obj, { "padding": lv_style_padding_args_t{10, 0, 0, 40} });  // Use lv_style_padding_args_t instead
    lv_obj_add_event_fn(arc_obj, LV_EVENT_VALUE_CHANGED, evt_bind(JogDial::_jogDialValueChangedEventCb, this));

    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_size(label, 38, LV_PCT(100));
    // style(label, { "margin": 0, "padding": 0 });  // Provide lv_style_margin_padding_args_t
    lv_label_set_text(label, "100%");

    lv_obj_t* slider = lv_slider_create(parent);
    lv_slider_set_range(slider, 0, 200);
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    lv_obj_set_size(slider, 15, LV_PCT(100));
    lv_obj_add_event_fn(slider, LV_EVENT_VALUE_CHANGED, evt_bind(JogDial::_feedChanged, this));

    lv_obj_update_layout(parent);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_LAYOUT_1);
    lv_obj_align_to(label, slider, LV_ALIGN_OUT_LEFT_MID, -5, 115);

    feed_label = label;
    feed_slider = slider;

    position = new MachinePositionWCS(arc_obj, AXES, this->interface); // Pass interface
    lv_obj_align_to(position->self, arc_obj, LV_ALIGN_CENTER, -25, 0);

    setAxisVis(AXES_OPTIONS.back());
}

JogDial::~JogDial() {
  if(position != nullptr) delete position;
}

std::string JogDial::currentAxis() {
    return axis;
}

void JogDial::setAxis(const std::string& ax) {
    _df(0, "SET AXIS %s", ax.c_str());
    
    axis = ax;
    auto it = AXIS_IDS.find(ax);
    axis_id = (it != AXIS_IDS.end()) ? it->second : -1;  // Default to -1 if not found

    for (auto& cb : axis_change_cb) {
        cb(axis);
    }
}

void JogDial::setAxisVis(const std::string& ax) {
    setAxis(ax);
    size_t i = 0;
    for (; i < AXES_OPTIONS.size(); ++i) {
        if (AXES_OPTIONS[i] == axis) break;
    }
    lv_buttonmatrix_set_button_ctrl(axis_btns, i, LV_BUTTONMATRIX_CTRL_CHECKED);
}

std::string JogDial::nextAxis() {
    size_t i = 0;
    for (; i < AXES_OPTIONS.size(); ++i) {
        if (AXES_OPTIONS[i] == axis) break;
    }
    i = (i + 1) % AXES_OPTIONS.size();
    lv_buttonmatrix_set_button_ctrl(axis_btns, i, LV_BUTTONMATRIX_CTRL_CHECKED);
    setAxis(AXES_OPTIONS[i]);
    return axis;
}

void JogDial::addAxisChangeDb(void (*cb)(const std::string&)) {
    axis_change_cb.push_back(cb);
}

void JogDial::connectionList() {
    // TODO: Implement connection list functionality
}

void JogDial::updatePosLabels(const std::vector<float>& vals) {
  for (size_t i = 0; i < vals.size(); ++i) {
      position->setCoord(i, vals[i]);
  }
}

void JogDial::_machineStateUpdated(MachineInterface* machine) {
    if (!machine->isConnected()) {
        position->coordsUndefined();
    }
    // else:
    //    updatePosLabels(machine->wcs_position);
}

void JogDial::_axisClicked(lv_event_t* evt) {
    _d(0, "AXIS CLICKED");
    lv_obj_t* tgt = static_cast<lv_obj_t *>(lv_event_get_target(evt));
    int id = lv_buttonmatrix_get_selected_button(tgt);
    const char* txt = lv_buttonmatrix_get_button_text(tgt, id);
    setAxis(txt);
}

void JogDial::_feedClicked(lv_event_t* evt) {
  lv_obj_t* tgt = static_cast<lv_obj_t *>(lv_event_get_target(evt));
  int id = lv_buttonmatrix_get_selected_button(tgt);
  feed = std::get<1>(FEEDS[id]);
  lv_label_set_text(feed_label, (std::string(std::get<0>(FEEDS[id]))).c_str());
}

void JogDial::_feedChanged(lv_event_t* evt) {
  lv_obj_t* tgt = static_cast<lv_obj_t *>(lv_event_get_target(evt));
  int v = lv_slider_get_value(tgt);
  feed = static_cast<float>(v) / 100.0f;
  std::string feedText = std::to_string(v) + "%";
  lv_label_set_text(feed_label, feedText.c_str());
}

void JogDial::setValue(int v) {
    lv_arc_set_value(arc, v);
    lv_obj_send_event(arc, LV_EVENT_VALUE_CHANGED, nullptr);
}

bool JogDial::axisSelected() {
    return axis_id != -1; // Compare with -1 instead of None
}

void JogDial::applyDiff(int diff) {
    int val = lv_arc_get_value(arc) + diff;
    _df(0, "val %d, diff %d, res %d", val, diff, 100 - val % 100);
    if (val < 0) {
        val = 100 + val % 100;
    } else {
        val = val % 100;
    }
    lv_arc_set_value(arc, val);
}
void JogDial::inc() {
    applyDiff(1);
}

void JogDial::dec() {
    applyDiff(-1);
}

void JogDial::_jogDialValueChangedEventCb(lv_event_t* evt) {
  lv_obj_t* tgt = static_cast<lv_obj_t *>(lv_event_get_target(evt));
  int val = lv_arc_get_value(tgt);

  if (val > 99 && prev < val) {
    lv_arc_set_value(arc, 0);
  }
  if (val < 1 && prev > val) {
    lv_arc_set_value(arc, 99);
  }
  prev = val;
}