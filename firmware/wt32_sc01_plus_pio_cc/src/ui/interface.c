// interface.c
#include "interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void interface_init_main_tabs(interface_t *interface) {
    // interface->main_tabs = lv_tabview_create(interface->scr, LV_DIR_TOP, TAB_HEIGHT);
    lv_obj_t *tabv = interface->main_tabs = TABV("itf:main_tabv", interface->scr,
        _tv_bar_pos(obj, LV_DIR_TOP);
        _tv_bar_size(obj, TAB_HEIGHT);
    );

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

    lv_obj_t *tab_jog = lv_tabview_add_tab(tabv, "Jog");
    lv_obj_t *tab_probe = lv_tabview_add_tab(tabv, "Probe");
    lv_obj_t *tab_machine = lv_tabview_add_tab(tabv, "Machine");
    interface->tab_job_gcode = lv_tabview_add_tab(tabv, "Status");
    interface->tab_tool = lv_tabview_add_tab(tabv, "Tools");
    interface->tab_cam = lv_tabview_add_tab(tabv, "CAM");

    interface->tab_jog = tab_jog_create(tabv, interface, tab_jog);
    interface->tab_probe = tab_probe_create(tabv, interface, tab_probe);
    interface->tab_machine = tab_machine_create(tabv, interface, tab_machine);
}


void interface_register_state_change_cb(interface_t *interface, machine_state_change_cb_t cb, void* user_data) {
    if (!interface) return;
    interface->machine_change_cb = cb;
    interface->machine_change_user_data = user_data;
}

void interface_update_machine_state(interface_t *interface, machine_interface_t *machine) {
    if (!interface) return;

    // TODO: Update global displays, if any.

    // Call the registered callback
    if (interface->machine_change_cb) {
        interface->machine_change_cb(machine, interface->machine_change_user_data);
    }
}