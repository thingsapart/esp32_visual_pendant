// jog_ui.c
#include "tab_jog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_helpers.h"

// --- Static Data ---

static const feed_t feeds[] = {
    {"100%", 1.0f},
    {"25%", 0.25f},
    {"10%", 0.1f},
    {"1%", 0.01f}
};
static const size_t num_feeds = sizeof(feeds) / sizeof(feeds[0]);

const char *axes_options[] = {"X", "Y", "Z", "Off"};
static const size_t num_axes_options = sizeof(axes_options) / sizeof(axes_options[0]);
static const char *axes_option_btns[] = {"X", "\n", "Y", "\n", "Z", "\n", "Off", NULL};

static const char *axes[] = {"X", "Y", "Z"}; // For MachinePosition
static const size_t num_axes = sizeof(axes) / sizeof(axes[0]);

static const char *workspaces[] = { "G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3" };
static const size_t num_workspaces = sizeof(workspaces) / sizeof(workspaces[0]);

static const char *coord_systems[] = { "Mach", "WCS", "Move" };
static const size_t num_coord_systems = sizeof(coord_systems) / sizeof(coord_systems[0]);

// --- Helper Functions ---

static axis_t get_axis_from_string(const char *str) {
    for (size_t i = 0; i < num_axes_options; i++) {
        if (strcmp(str, axes_options[i]) == 0) {
            return (axis_t)i;
        }
    }
    return AXIS_OFF; // Default to Off
}

static int get_axis_id(axis_t axis) {
    if (axis >= AXIS_X && axis <= AXIS_Z) {
        return (int)axis;
    }
    return -1; // Indicate "Off"
}


// --- Event Handlers ---
static void axis_clicked_event_handler(lv_event_t *e) {
    lv_obj_t *btnm = lv_event_get_target(e);
    jog_dial_t *jd = (jog_dial_t *)lv_event_get_user_data(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(btnm);
    const char *txt = lv_btnmatrix_get_btn_text(btnm, id);

    if (jd && txt) {
        jog_dial_set_axis_vis(jd, get_axis_from_string(txt));
    }
}

static void feed_changed_event_handler(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    jog_dial_t *jd = (jog_dial_t *)lv_event_get_user_data(e);
    int v = lv_slider_get_value(slider);
    if(jd) {
        jd->feed = v / 100.0f;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", v);
        _label_text(jd->feed_label, buf);
        // Re-align the label after changing text
        // lv_obj_align_to(jd->feed_label, jd->feed_slider, LV_ALIGN_OUT_LEFT_MID, -20, 0);
    }
}
static void jog_dial_value_changed_event_handler(lv_event_t *e) {
    lv_obj_t *arc = lv_event_get_target(e);
    jog_dial_t *jd = (jog_dial_t *)lv_event_get_user_data(e);

    if (jd) {
        int val = lv_arc_get_value(arc);
        int prev = jd->prev;
        jd->prev = val;
        if (val > 99 && prev < val) {
            lv_arc_set_value(arc, 0);
        }
        if (val < 1 && prev > val) {
             lv_arc_set_value(arc, 99);
        }

        // diff = val - jd->prev;
        // jd->interface.machine.move(jd->axis, jd->feed, diff); // Placeholder: Call your interface
    }
}

jog_dial_t *jog_dial_create(lv_obj_t *parent, interface_t *interface) {
    jog_dial_t *jd = (jog_dial_t *)malloc(sizeof(jog_dial_t));
    if (!jd) {
        LV_LOG_ERROR("Failed to allocate memory for jog_dial_t");
        return NULL;
    }
    memset(jd, 0, sizeof(jog_dial_t));

    jd->parent = parent;
    jd->interface = interface;
    jd->feed = 1.0f;
    jd->axis = AXIS_OFF;
    jd->axis_id = get_axis_id(jd->axis);
    jd->last_rotary_pos = 0;
    jd->prev = 0;

    _maximize_client_area(parent);
    lv_obj_t * cnt = jd->container = __container(parent);

    // interface->register_state_change_cb(jd->_machine_state_updated);

    _flex_flow(cnt, LV_FLEX_FLOW_ROW);
    _flex_align(cnt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _flag(cnt, LV_OBJ_FLAG_SCROLLABLE, false);

    // Create Axis Button Matrix
    jd->axis_btns = lv_btnmatrix_create(cnt);
    if (!jd->axis_btns) {
        LV_LOG_ERROR("axis_btns alloc failed");
        free(jd);
        return NULL;
    }

    lv_btnmatrix_set_map(jd->axis_btns, axes_option_btns);
    lv_btnmatrix_set_btn_ctrl_all(jd->axis_btns, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_btn_ctrl(jd->axis_btns, num_axes_options - 1, LV_BTNMATRIX_CTRL_CHECKED); //removed default checkmark
    lv_btnmatrix_set_one_checked(jd->axis_btns, true);
    _size(jd->axis_btns, 60, lv_pct(100));
    _style_local(jd->axis_btns, pad_ver, LV_PART_MAIN, 5);
    _style_local(jd->axis_btns, pad_hor, LV_PART_MAIN, 5);

    lv_obj_add_event_cb(jd->axis_btns, axis_clicked_event_handler, LV_EVENT_VALUE_CHANGED, jd);

    // Create Arc
    jd->arc = lv_arc_create(cnt);
    if (!jd->arc)
    {
        LV_LOG_ERROR("Failed to allocate arc");
        lv_obj_del(jd->axis_btns);
        free(jd);
        return NULL;
    }
    
    _size(jd->arc, lv_pct(100), lv_pct(100));
    lv_arc_set_rotation(jd->arc, -90);
    lv_arc_set_bg_angles(jd->arc, 0, 360);
    lv_arc_set_angles(jd->arc, 0, 360);
    //_style_local(jd->arc, opa, LV_PART_INDICATOR, LV_OPA_0);
    lv_arc_set_value(jd->arc, 0);
    _flag(jd->arc, LV_OBJ_FLAG_ADV_HITTEST, true);
    _flex_grow(jd->arc, 1);
    _style_local(jd->arc, pad_all, LV_PART_MAIN, 10);
    _maximize_client_area(jd->arc);
    lv_obj_update_layout(jd->arc);
    lv_coord_t h = height_(jd->arc);
    lv_coord_t w = width_(jd->arc);
    _pad_left(jd->arc, (w - h) / 2, LV_PART_MAIN);
    _pad_right(jd->arc, (w - h) / 2, LV_PART_MAIN);
    _pad_top(jd->arc, 10, LV_PART_MAIN);
    _pad_bottom(jd->arc, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(jd->arc, jog_dial_value_changed_event_handler, LV_EVENT_VALUE_CHANGED, jd);


    // Create Feed Slider
    jd->feed_slider = lv_slider_create(cnt);
    if (!jd->feed_slider)
    {
        LV_LOG_ERROR("feed_slider creation failed!");
        lv_obj_del(jd->feed_label);
        lv_obj_del(jd->arc);
        lv_obj_del(jd->axis_btns);
        free(jd);
        return NULL;
    }
    
    lv_slider_set_range(jd->feed_slider, 0, 200);
    lv_slider_set_value(jd->feed_slider, 100, LV_ANIM_OFF);
    _size(jd->feed_slider, 20, lv_pct(90));
    _margin_all(jd->feed_slider, 10, LV_PART_MAIN);
    _margin_right(jd->feed_slider, 20, LV_PART_MAIN);
    lv_obj_add_event_cb(jd->feed_slider, feed_changed_event_handler, LV_EVENT_VALUE_CHANGED, jd);
    lv_obj_update_layout(jd->feed_slider);

    // Create Feed Label
    jd->feed_label = lv_label_create(cnt);
    if (!jd->feed_label) {
        LV_LOG_ERROR("feed label creation failed!");
        lv_obj_del(jd->arc);
        lv_obj_del(jd->axis_btns);
        free(jd);
        return NULL;
    }
    _size(jd->feed_label, 38, LV_SIZE_CONTENT);
    _flag(jd->feed_label, LV_OBJ_FLAG_IGNORE_LAYOUT, true);
    _style_local(jd->feed_label, pad_all, LV_PART_MAIN, 0); // Remove padding
    _style_local(jd->feed_label, margin_all, LV_PART_MAIN, 0); //Remove margin
    _label_text(jd->feed_label, "100%");


    _flag(jd->feed_label, LV_OBJ_FLAG_IGNORE_LAYOUT, true);
    lv_obj_align_to(jd->feed_label, jd->feed_slider, LV_ALIGN_OUT_LEFT_MID, -20, 0);

    // Position Labels
    jd->position = machine_position_wcs_create(jd->arc, axes, num_axes, interface, 4, coord_systems, num_coord_systems, 60);
    if (!jd->position)
    {
        LV_LOG_ERROR("failed to create position display");
        lv_obj_del(jd->feed_slider);
        lv_obj_del(jd->feed_label);
        lv_obj_del(jd->arc);
        lv_obj_del(jd->axis_btns);
        free(jd);
    }
    
    lv_obj_update_layout(jd->arc);
    _flag(jd->position->container, LV_OBJ_FLAG_IGNORE_LAYOUT, true);
    lv_obj_align_to(jd->position->container, jd->arc, LV_ALIGN_CENTER, 0, 0);
    // dbg_layout(jd->position->container);
    // dbg_layout(jd->arc);

    //Initial state.
    jog_dial_set_axis_vis(jd, AXIS_OFF);

    return jd;
}

void jog_dial_set_axis_vis(jog_dial_t *jd, axis_t ax) {
    if (!jd || !jd->axis_btns) return;

    jd->axis = ax;
    jd->axis_id = get_axis_id(ax);
    size_t i = (size_t)ax;

    // Set the button matrix button as checked.
    if (i < num_axes_options) {
        lv_btnmatrix_set_btn_ctrl(jd->axis_btns, i, LV_BTNMATRIX_CTRL_CHECKED);
    }


    // Call the callback if registered
    if (jd->axis_change_cb) {
        jd->axis_change_cb(jd->axis, jd->axis_change_user_data);
    }
}
axis_t jog_dial_next_axis(jog_dial_t *jd) {
    if (!jd) return AXIS_OFF;

    size_t i = (size_t)jd->axis;
    i = (i + 1) % num_axes_options;
    lv_btnmatrix_set_btn_ctrl(jd->axis_btns, i, LV_BTNMATRIX_CTRL_CHECKED);
    jog_dial_set_axis_vis(jd, (axis_t)i);
    return jd->axis;
}

void jog_dial_add_axis_change_cb(jog_dial_t *jd, axis_change_cb_t cb, void *user_data) {
    if (!jd) return;
    jd->axis_change_cb = cb;
    jd->axis_change_user_data = user_data;
}
void jog_dial_set_value(jog_dial_t *jd, int v) {
    if (!jd) return;
    lv_arc_set_value(jd->arc, v);
    lv_obj_send_event(jd->arc, LV_EVENT_VALUE_CHANGED, NULL); // Send event
}

bool jog_dial_axis_selected(jog_dial_t *jd) {
    if (!jd) return false;
    return jd->axis_id != -1;
}

void jog_dial_inc(jog_dial_t *jd) {
     if (!jd) return;
    lv_arc_set_value(jd->arc, lv_arc_get_value(jd->arc) + 1);
}

void jog_dial_dec(jog_dial_t *jd) {
    if (!jd) return;
    lv_arc_set_value(jd->arc, lv_arc_get_value(jd->arc) - 1);
}

bool jog_dial_apply_diff(jog_dial_t *jd, int diff) {
    if (!jd) { return false; }
    int val = lv_arc_get_value(jd->arc) + diff;
    if (val < 0) {
        val = 100 + (val % 100);
    } else if (val > 99) {
        val = val % 100;
    }
    lv_arc_set_value(jd->arc, val);

    return true;
}

// --- TabJog Functions ---
tab_jog_t *tab_jog_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab)
{
      tab_jog_t *tj = (tab_jog_t *)malloc(sizeof(tab_jog_t));
      if (!tj) {
        LV_LOG_ERROR("Failed to allocate tab_jog_t");
        return NULL;
    }
     memset(tj, 0, sizeof(tab_jog_t));
     tj->tabv = tabv;
     tj->interface = interface;
     tj->tab = tab;
     tj->jog_dial = jog_dial_create(tab, interface);
     if(!tj->jog_dial)
     {
        LV_LOG_ERROR("Failed to create jog dial");
        free(tj); // clean up!
        return NULL;
     }

     // tj->jog_slider = jog_slider_create(tab, interface); // If you implement JogSlider

     return tj;
}