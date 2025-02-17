#ifndef JOG_UI_H
#define JOG_UI_H

#include "lvgl.h"
#include "ui_helpers.h"

#include "machine_position.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct interface_t interface_t;
typedef struct jog_dial_t jog_dial_t;
typedef struct jog_slider_t jog_slider_t;
typedef struct tab_jog_t tab_jog_t;
typedef struct machine_position_wcs_t machine_position_wcs_t;

// JogDial related structures and enums

extern const char *axes_options[];

typedef enum {
    AXIS_X,
    AXIS_Y,
    AXIS_Z,
    AXIS_OFF
} axis_t;

typedef struct {
    const char *label;
    float value;
} feed_t;

typedef void (*axis_change_cb_t)(axis_t axis, void *user_data);

struct jog_dial_t {
    lv_obj_t *parent;
    interface_t *interface;
    lv_obj_t *container;
    lv_obj_t *axis_btns;
    lv_obj_t *arc;
    lv_obj_t *feed_label;
    lv_obj_t *feed_slider;

    machine_position_wcs_t *position;

    axis_t axis;
    int axis_id;
    float feed;
    int last_rotary_pos;
    int prev;
    char **btn_map;
    axis_change_cb_t axis_change_cb;
    void *axis_change_user_data; 
};

// TabJog related structures (if needed)
struct tab_jog_t {
    lv_obj_t *tabv;
    interface_t *interface;
    lv_obj_t *tab;
    jog_dial_t *jog_dial;
    // jog_slider_t *jog_slider; // if you add it.
};

// Function prototypes
tab_jog_t *tab_jog_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab);
jog_dial_t *jog_dial_create(lv_obj_t *parent, interface_t *interface);
void jog_dial_set_axis_vis(jog_dial_t *jd, axis_t ax);
axis_t jog_dial_next_axis(jog_dial_t *jd);
void jog_dial_add_axis_change_cb(jog_dial_t *jd, axis_change_cb_t cb, void *user_data);
void jog_dial_set_value(jog_dial_t *jd, int v);
bool jog_dial_axis_selected(jog_dial_t *jd);
void jog_dial_inc(jog_dial_t *jd);
void jog_dial_dec(jog_dial_t *jd);
bool jog_dial_apply_diff(jog_dial_t *jd, int diff);

#if 0
// Machine Position (Placeholder - Define this based on your needs)
struct machine_position_wcs_t {
    lv_obj_t *container;
    lv_obj_t *coords[3]; // X, Y, Z labels
    interface_t *interface;
    const char **axis_names; // {"X", "Y", "Z"}
    int digits;
};

machine_position_wcs_t* machine_position_wcs_create(lv_obj_t* parent, const char** axes, interface_t* interface, int digits);
void machine_position_wcs_coords_undefined(machine_position_wcs_t *mp);

#endif

#ifdef __cplusplus
}
#endif

#endif // JOG_UI_H