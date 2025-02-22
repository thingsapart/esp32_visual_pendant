// interface.c
#include "interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ui/tab_jog.h"
#include "ui/tab_probe.h"
#include "ui/tab_machine.h"
#include "ui_helpers.h"

#include "debug.h"

// #include "fs_driver.h"  // Include your fs_driver implementation

// --- Helper Functions ---

// --- Interface Implementation ---

interface_t *interface_create(machine_interface_t *machine) {
    interface_t *interface = (interface_t *)malloc(sizeof(interface_t));

    return interface_init(interface, machine);
}

interface_t *interface_init(interface_t *interface, machine_interface_t *machine) {
    if (!interface) {
        LV_LOG_ERROR("Failed to allocate interface");
        return NULL;
    }
    memset(interface, 0, sizeof(interface_t));

    interface->scr = lv_screen_active();
    interface->machine = machine;

    interface_fs_init(interface);
    interface_init_fonts(interface);

    interface_init_main_tabs(interface);

    lv_screen_load(interface->scr);

    return interface;
}

void interface_deinit(interface_t *interface) {
    if (!interface) return;

    // Clean up resources:
     if (interface->main_tabs) {
        lv_obj_del(interface->main_tabs); // This will delete child tabs as well
    }

    if (interface->tab_jog) {
        // tab_jog_destroy(interface->tab_jog);  // Implement this in jog_ui.c/.h
    }
    if (interface->tab_probe) {
        tab_probe_destroy(interface->tab_probe);
    }
    if (interface->tab_machine) {
         tab_machine_destroy(interface->tab_machine);
    }

    // 3. Free dynamically allocated fonts (if loaded successfully)
    if (interface->font_lcd) {
        lv_binfont_destroy(interface->font_lcd);
    }
      if (interface->font_lcd_18) {
        lv_binfont_destroy(interface->font_lcd_18);
    }
    if (interface->font_lcd_24) {
        lv_binfont_destroy(interface->font_lcd_24);
    }
}

void interface_destroy(interface_t *interface) {
    if (!interface) return;

    interface_deinit(interface);

    free(interface);
}

void debug_test_fs() {
#if 1
    lv_fs_dir_t dir;
    char buf[200];
    printf("Letters: %s\n", lv_fs_get_letters(buf));
    lv_fs_res_t res;
    if ((res = lv_fs_dir_open(&dir, "S:/")) == LV_FS_RES_OK) {
        while (lv_fs_dir_read(&dir, buf, 200) == LV_FS_RES_OK && buf[0] != '\0') {
            _df(0, "%s\n", buf);
        }
        lv_fs_dir_close(&dir);
    } else {
        _df(0, "FAILED to open S: %d\n", res);
    }
#endif
    lv_fs_file_t fp;
    printf("FILE OPEN: %d\n", lv_fs_open(&fp, "S:/img/arr_s.png", LV_FS_MODE_RD));
    uint32_t read_num;
    uint8_t bufs[0];
    res = lv_fs_read(&fp, bufs, 8, &read_num);
    if(res != LV_FS_RES_OK || read_num != 8) {
        _d(2, "FAILED TO READ FILE\n");
    } else {
        bufs[8] = '\0';
        _df(0, "CONTENTS[8]: %s\n", bufs);
    }
    lv_fs_close(&fp);
}

void interface_fs_init(interface_t *interface) {
    #ifdef ESP32_HW
        //lv_fs_arduino_esp_littlefs_init();
    #elif POSIX
        _d(0, "Loading FS...");
        lv_fs_posix_init();

        _d(0, "[done]");
    #endif

    // debug_test_fs();
}

#ifndef LOAD_BIN_FONT_FS
    LV_FONT_DECLARE(lcd_7_segment_24);
#endif

void interface_init_fonts(interface_t *interface) {
#ifdef LOAD_BIN_FONT_FS
    // Construct the font paths.  This assumes the fonts are in a "font"
    // subdirectory relative to the location of the executable.
    char font_path[MAX_PATH_LEN];

    // Get current working directory or executable path (platform specific)
    // For simplicity, let's assume a fixed location for now.  In a real
    // embedded system, you'd need to get the path to your assets.
    // You MIGHT be able to use a relative path like "../font/...", but that
    // depends on how your build system and file system are set up.

    // const char *base_path = "/path/to/your/assets/font/";  // Replace with your actual path.
    const char *base_path = "../font/";  // Try a relative path first


     snprintf(font_path, sizeof(font_path), "%slcd_7_segment.bin", base_path);
    interface->font_lcd = lv_binfont_create(font_path);
    if (!interface->font_lcd) {
        LV_LOG_WARN("Failed to load font: %s", font_path);
    }

    snprintf(font_path, sizeof(font_path), "%slcd_7_segment_18.bin", base_path);
    interface->font_lcd_18 = lv_binfont_create(font_path);
    if (!interface->font_lcd_18) {
        LV_LOG_WARN("Failed to load font: %s", font_path);
    }

    snprintf(font_path, sizeof(font_path), "%slcd_7_segment_24.bin", base_path);
    interface->font_lcd_24 = lv_binfont_create(font_path);
    if (!interface->font_lcd_24) {
        LV_LOG_WARN("Failed to load font: %s", font_path);
    }
#else
    interface->font_lcd_24 = &lcd_7_segment_24;
#endif
}

#ifdef POSIX
void jog_plus_cb(lv_event_t *e) {
   interface_t *interface = (interface_t *) lv_event_get_user_data(e); 
   machine_interface_t *mach = interface->machine;
   machine_interface_step_current_axis(mach, 1000.0, 1.0);
}

void jog_minus_cb(lv_event_t *e) {
   interface_t *interface = (interface_t *) lv_event_get_user_data(e); 
   machine_interface_t *mach = interface->machine;
   machine_interface_step_current_axis(mach, 1000.0, -1.0);
}
#endif

void interface_init_main_tabs(interface_t *interface) {
    // interface->main_tabs = lv_tabview_create(interface->scr, LV_DIR_TOP, TAB_HEIGHT);
    lv_obj_t *tabv = interface->main_tabs = TABV("itf:main_tabv", interface->scr,
        _tv_bar_pos(obj, LV_DIR_TOP);
        _tv_bar_size(obj, TAB_HEIGHT);
    );
    _maximize_client_area(interface->main_tabs);

    if (!interface->main_tabs) {
        LV_LOG_ERROR("Failed to create main_tabs");
        return; // Or handle the error as appropriate
    }

    lv_obj_t *tab_content = lv_tabview_get_content(tabv);
     if (!tab_content) {
        LV_LOG_ERROR("tabview get_content() failed");
        return;
     }

    _flag(tab_content, LV_OBJ_FLAG_SCROLLABLE, false);
    _maximize_client_area(tab_content);

    lv_obj_t *tab_jog = lv_tabview_add_tab(tabv, "Jog");
    lv_obj_t *tab_probe = lv_tabview_add_tab(tabv, "Probe");
    lv_obj_t *tab_machine = lv_tabview_add_tab(tabv, "Machine");
    interface->tab_job_gcode = lv_tabview_add_tab(tabv, "Status");
    interface->tab_tool = lv_tabview_add_tab(tabv, "Tools");
    interface->tab_cam = lv_tabview_add_tab(tabv, "CAM");

    _maximize_client_area(tab_jog);
    _maximize_client_area(tab_probe);
    _maximize_client_area(tab_machine);
    _maximize_client_area(interface->tab_job_gcode);
    _maximize_client_area(interface->tab_tool);
    _maximize_client_area(interface->tab_cam);

    interface->tab_jog = tab_jog_create(tabv, interface, tab_jog);
    interface->tab_probe = tab_probe_create(tabv, interface, tab_probe);
    interface->tab_machine = tab_machine_create(tabv, interface, tab_machine);

    #ifdef POSIX
        mk_container(NULL, interface->scr,
            _use_layout(obj, false);
            _size(obj, 100, 30);
            _maximize_client_area(obj);
            _pos(obj, 0, 0);
            _flex_row(obj);

            mk_btn(NULL, outer_obj,
                _maximize_client_area(obj);
                _size(obj, lv_pct(40), lv_pct(100));
                mk_label(NULL, outer_obj,
                    _label_text(obj, "+")
                );
                lv_obj_add_event_cb(obj, jog_plus_cb, LV_EVENT_CLICKED, interface);
            );

            mk_btn(NULL, outer_obj,
                _maximize_client_area(obj);
                _size(obj, lv_pct(40), lv_pct(100));
                mk_label(NULL, outer_obj,
                    _label_text(obj, "-")
                );
                lv_obj_add_event_cb(obj, jog_minus_cb, LV_EVENT_CLICKED, interface);
            );
        );
   #endif
}

void interface_register_state_change_cb(interface_t *interface, machine_state_change_cb_t cb, void* user_data) {
    if (!interface) return;

    for (size_t i = 0; i < sizeof(interface->machine_change_cbs) / sizeof(interface->machine_change_cbs[0]); i++) {
        if (!interface->machine_change_cbs[i].cb) {
            interface->machine_change_cbs[i].cb = cb;
            interface->machine_change_cbs[i].user_data = user_data;
            return;
        }
    }
    assert(false && "Too many machine state update callbacks - update MAX_MACHINE_STATE_CBS");
}

void interface_update_machine_state(interface_t *interface, machine_interface_t *machine) {
    if (!interface) return;

    // Call the registered callback
    for (size_t i = 0; i < sizeof(interface->machine_change_cbs) / sizeof(interface->machine_change_cbs[0]); i++) {
        if (interface->machine_change_cbs[i].cb) {
            interface->machine_change_cbs[i].cb(interface->machine, interface->machine_change_cbs[i].user_data);
            return;
        }
    }
}
