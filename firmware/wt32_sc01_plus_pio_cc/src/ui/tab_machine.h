// tab_machine.h
#ifndef TAB_MACHINE_H
#define TAB_MACHINE_H

#include "lvgl.h"

#include "ui/interface.h"
#include "ui/tab_jog.h"
#include "ui/file_list.h"
#include "ui/machine_position.h"

typedef struct tab_machine_t tab_machine_t;
typedef struct machine_status_meter_t machine_status_meter_t;

struct tab_machine_t {
    lv_obj_t *tab;
    interface_t *interface;
    lv_obj_t *main_tabs;
    lv_obj_t *tab_status;
    lv_obj_t *tab_macros;
    lv_obj_t *tab_jobs;
    lv_obj_t *float_btn;
    machine_status_meter_t *mach_meter;
    file_list_t *jobs_list; // will need to define this
    file_list_t *macro_list;
};

struct machine_status_meter_t {
    lv_obj_t *container;
    interface_t *interface;
    int spindle_max_rpm;
    int max_feed;
    lv_obj_t *left_side;
    lv_obj_t *right_side;
    lv_obj_t *bar_feed;
    lv_obj_t *scale_feed;
    lv_obj_t *spindle_rpm;
    lv_obj_t *bar_cl; // Chip load
    lv_obj_t *scale_spindle_rpm;
    lv_obj_t *scale_spindle_chipload;
    lv_obj_t* material_dd;
    lv_obj_t* mill_dd;
    lv_obj_t* flute_dd;
    machine_position_wcs_t* position;
};

tab_machine_t *tab_machine_create(lv_obj_t *tabv, interface_t *interface, lv_obj_t *tab);
void tab_machine_destroy(tab_machine_t *tm);
void tab_machine_init_machine_tabv(tab_machine_t *tm, lv_obj_t *parent);
void tab_machine_init_tab_status(tab_machine_t *tm, lv_obj_t *tab);
void tab_machine_init_tab_jobs(tab_machine_t *tm, lv_obj_t *tab);
void tab_machine_init_tab_macros(tab_machine_t *tm, lv_obj_t *tab);
void tab_machine_init_axis_float_btn(tab_machine_t *tm);
machine_status_meter_t *machine_status_meter_create(lv_obj_t *parent,
                                                    interface_t *interface,
                                                    int spindle_max_rpm,
                                                    int max_feed);
void machine_status_meter_destroy(machine_status_meter_t *msm);


#endif // TAB_MACHINE_H