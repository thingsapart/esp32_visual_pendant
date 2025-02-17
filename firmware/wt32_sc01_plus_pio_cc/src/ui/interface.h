// interface.h
#ifndef INTERFACE_H
#define INTERFACE_H

#include "lvgl.h"
#include <stdbool.h>

#include "machine/machine_interface.h"
// #include "ui/tab_jog.h"
// #include "ui/tab_probe.h"

// --- Constants ---

#define TAB_HEIGHT 30
#define TAB_WIDTH 70
#define MAX_PATH_LEN 256 // Or whatever size you need

#ifdef __cplusplus
 extern "C" {
#endif

typedef struct interface_t interface_t;

struct tab_probe_t;
struct tab_jog_t;
struct tab_machine_t;

typedef void (*machine_state_change_cb_t)(machine_interface_t *machine, void *user_data);

struct interface_t {
    lv_obj_t *scr;
    machine_interface_t *machine;  // Pointer to your machine control object
    lv_fs_drv_t fs_drv;
    lv_font_t *font_lcd;
    lv_font_t *font_lcd_18;
    lv_font_t *font_lcd_24;
    lv_obj_t *main_tabs;
    struct tab_jog_t *tab_jog;
    struct tab_probe_t *tab_probe;
    struct tab_machine_t *tab_machine;
    lv_obj_t *tab_job_gcode;
    lv_obj_t *tab_tool;
    lv_obj_t *tab_cam;
    machine_state_change_cb_t machine_change_cb;
    void *machine_change_user_data;
};

// Function prototypes
interface_t *interface_create(machine_interface_t *machine);
interface_t *interface_init(interface_t *interface, machine_interface_t *machine);

void interface_destroy(interface_t *interface);
void interface_deinit(interface_t *interface);

void interface_fs_init(interface_t *interface);
void interface_init_fonts(interface_t *interface);
bool interface_process_wheel_tick(interface_t *interface, int diff);
void interface_init_main_tabs(interface_t *interface);
void interface_register_state_change_cb(interface_t *interface, machine_state_change_cb_t cb, void* user_data);
void interface_update_machine_state(interface_t *interface, machine_interface_t *machine);

#ifdef __cplusplus
}
#endif

#endif // INTERFACE_H