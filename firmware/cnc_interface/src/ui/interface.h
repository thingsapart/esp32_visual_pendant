// interface.h
#ifndef INTERFACE_H
#define INTERFACE_H

#include <stdbool.h>

#include "lvgl.h"
#include "machine/machine_interface.h"
// #include "ui/tab_jog.h"
// #include "ui/tab_probe.h"

// --- Constants ---

#define TAB_HEIGHT 30
#define TAB_WIDTH 70
#define MAX_PATH_LEN 256  // Or whatever size you need

#ifdef __cplusplus
extern "C" {
#endif

typedef struct interface_t interface_t;

struct tab_probe_t;
struct tab_jog_t;
struct tab_machine_t;
struct tab_status_t;

typedef void (*machine_state_change_cb_t)(machine_interface_t *machine,
                                          void *user_data);

typedef struct machine_state_callback_t {
  void *user_data;
  machine_state_change_cb_t cb;
} machine_state_callback_t;

#define MAX_MACHINE_STATE_CBS 32
#ifndef LOAD_BIN_FONT_FS
#define FONT_CONST const
#else
#define FONT_CONST
#endif

struct interface_t {
  lv_obj_t *scr;
  machine_interface_t *machine;  // Pointer to your machine control object
  lv_fs_drv_t fs_drv;
  FONT_CONST lv_font_t *font_lcd;
  FONT_CONST lv_font_t *font_lcd_18;
  FONT_CONST lv_font_t *font_lcd_24;
  FONT_CONST lv_font_t *font_kode_20;
  FONT_CONST lv_font_t *font_kode_24;
  lv_obj_t *main_tabs;
  struct tab_jog_t *tab_jog;
  struct tab_probe_t *tab_probe;
  struct tab_machine_t *tab_machine;
  struct tab_status_t *tab_job_gcode;
  lv_obj_t *tab_tool;
  lv_obj_t *tab_cam;
  machine_state_callback_t machine_change_cbs[MAX_MACHINE_STATE_CBS];
  void *machine_change_user_data;

  // The machine-related state processing is happening in a different
  // thread that can lead to races and crashes when updating UI.
  // Replicate the machine_interface callbacks here, let UI code register
  // here and have machine_interface callbacks just set dirty flags here
  // so that interface_t can call the UI callbacks.
  machine_change_callback_t state_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t pos_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t home_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t wcs_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t feed_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t sensors_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t dialogs_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t spindles_tools_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t connected_changed_cb[MAX_CALLBACKS];
  machine_change_callback_t current_move_axis_changed_cb[MAX_CALLBACKS];
  files_changed_callback_t files_changed_cb[MAX_CALLBACKS];

  struct {
    unsigned int state_changed : 1;
    unsigned int pos_changed : 1;
    unsigned int home_changed : 1;
    unsigned int wcs_changed : 1;
    unsigned int feed_changed : 1;
    unsigned int sensors_changed : 1;
    unsigned int dialogs_changed : 1;
    unsigned int spindles_tools_changed : 1;
    unsigned int connected_changed : 1;
    unsigned int current_move_axis_changed : 1;
  } machine_state_updated;
  bool files_changed[MAX_FILE_LISTS];
};

// Function prototypes
interface_t *interface_create(machine_interface_t *machine);
interface_t *interface_init(interface_t *interface,
                            machine_interface_t *machine);

void interface_destroy(interface_t *interface);
void interface_deinit(interface_t *interface);

void interface_fs_init(interface_t *interface);
void interface_init_fonts(interface_t *interface);
bool interface_process_wheel_tick(interface_t *interface, int diff);
void interface_init_main_tabs(interface_t *interface);
void interface_register_state_change_cb(interface_t *interface,
                                        machine_state_change_cb_t cb,
                                        void *user_data);
void interface_update_machine_state(interface_t *interface,
                                    machine_interface_t *machine);
void interface_tick(interface_t *interface);

add_callback_proto(interface, state_changed);
add_callback_proto(interface, pos_changed);
add_callback_proto(interface, home_changed);
add_callback_proto(interface, wcs_changed);
add_callback_proto(interface, feed_changed);
add_callback_proto(interface, sensors_changed);
add_callback_proto(interface, dialogs_changed);
add_callback_proto(interface, spindles_tools_changed);
add_callback_proto(interface, connected_changed);
add_callback_proto(interface, current_move_axis_changed);
bool interface_add_files_changed_cb(interface_t *self, const char *path,
                                    void *user_data,
                                    files_changed_callback_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif  // INTERFACE_H
