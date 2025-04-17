// interface.c
#include "interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ui/tab_jog.h"
#include "ui/tab_probe.h"
#include "ui/tab_machine.h"
#include "ui/tab_status.h"
#include "ui/modals.h"
#include "ui_helpers.h"

#include "debug.h"

// Forward decls.

const char *TAG = "ui/interface";

void message_box_t_modal(machine_interface_t *mach, void *user_data);
void _mach_state_changed(machine_interface_t *mach, void *user_data);
void _mach_home_changed(machine_interface_t *mach, void *user_data);
void _mach_pos_changed(machine_interface_t *mach, void *user_data);
void _mach_wcs_changed(machine_interface_t *mach, void *user_data);
void _mach_feed_changed(machine_interface_t *mach, void *user_data);
void _mach_sensors_changed(machine_interface_t *mach, void *user_data);
void _mach_dialogs_changed(machine_interface_t *mach, void *user_data);
void _mach_spindles_tools_changed(machine_interface_t *mach, void *user_data);
void _mach_connected_changed(machine_interface_t *mach, void *user_data);
void _mach_current_move_axis_changed(machine_interface_t *mach, void *user_data);

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

    interface_add_dialogs_changed_cb(interface, interface, message_box_t_modal);
    machine_interface_add_state_change_cb(machine, interface, _mach_state_changed);
    machine_interface_add_pos_changed_cb(machine, interface, _mach_pos_changed);
    machine_interface_add_home_changed_cb(machine, interface, _mach_home_changed);
    machine_interface_add_wcs_changed_cb(machine, interface, _mach_wcs_changed);
    machine_interface_add_feed_changed_cb(machine, interface, _mach_feed_changed);
    machine_interface_add_sensors_changed_cb(machine, interface, _mach_sensors_changed);
    machine_interface_add_dialogs_changed_cb(machine, interface, _mach_dialogs_changed);
    machine_interface_add_spindles_tools_changed_cb(machine, interface, _mach_spindles_tools_changed);
    machine_interface_add_connected_changed_cb(machine, interface, _mach_connected_changed);
    machine_interface_add_current_move_axis_changed_cb(machine, interface, _mach_current_move_axis_changed);

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

#ifdef LOAD_BIN_FONT_FS
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
    if (interface->font_kode_20) {
        lv_binfont_destroy(interface->font_kode_20);
    }
#endif
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
            _df(-1, "%s\n", buf);
        }
        lv_fs_dir_close(&dir);
    } else {
        _df(-1, "FAILED to open S: %d\n", res);
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
    LV_FONT_DECLARE(kode_20);

    #define LV_FONT_ASSIGN(dest, font) \
        do { const lv_font_t *font_addr = &font; memcpy(&dest, &font_addr, sizeof(lv_font_t *)); } while (0);
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

    snprintf(font_path, sizeof(font_path), "%skode_20.bin", base_path);
    interface->font_kode_20 = lv_binfont_create(font_path);
    if (!interface->font_kode_20) {
        LV_LOG_WARN("Failed to load font: %s", font_path);
    }
#else
    LV_FONT_ASSIGN(interface->font_lcd_24, lcd_7_segment_24);
    LV_FONT_ASSIGN(interface->font_kode_20, kode_20);
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
    lv_obj_t *tab_status = lv_tabview_add_tab(tabv, "Status");
    interface->tab_tool = lv_tabview_add_tab(tabv, "Tools");
    interface->tab_cam = lv_tabview_add_tab(tabv, "CAM");

    _maximize_client_area(tab_jog);
    _maximize_client_area(tab_probe);
    _maximize_client_area(tab_machine);
    _maximize_client_area(tab_status);
    _maximize_client_area(interface->tab_tool);
    _maximize_client_area(interface->tab_cam);

    interface->tab_jog = tab_jog_create(tabv, interface, tab_jog);
    interface->tab_probe = tab_probe_create(tabv, interface, tab_probe);
    interface->tab_machine = tab_machine_create(tabv, interface, tab_machine);
    interface->tab_job_gcode = tab_status_create(tabv, interface, tab_status);

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

void _mach_state_changed(machine_interface_t *mach, void *user_data) {
    _d(-1, "STATE CHANGED");
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.state_changed = true;
}

void _mach_pos_changed(machine_interface_t *mach, void *user_data) {
    LOGI(TAG, "POS CHANGED");
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.pos_changed = true;
}

void _mach_home_changed(machine_interface_t *mach, void *user_data) {
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.home_changed = true;
}

void _mach_wcs_changed(machine_interface_t *mach, void *user_data) {
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.wcs_changed = true;
}

void _mach_feed_changed(machine_interface_t *mach, void *user_data) {
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.feed_changed = true;
}

void _mach_sensors_changed(machine_interface_t *mach, void *user_data) {
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.sensors_changed = true;
}

void _mach_dialogs_changed(machine_interface_t *mach, void *user_data) {
    _d(-1, "STATE CHANGED");
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.dialogs_changed = true;
}

void _mach_spindles_tools_changed(machine_interface_t *mach, void *user_data) {
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.spindles_tools_changed = true;
}

void _mach_connected_changed(machine_interface_t *mach, void *user_data) {
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.connected_changed = true;
}

void _mach_current_move_axis_changed(machine_interface_t *mach, void *user_data) {
    interface_t *interface = (interface_t *)user_data;
    interface->machine_state_udated.current_move_axis_changed = true;
}

void _mach_files_changed(machine_interface_t *mach, void *user_data, const char *path, char **files) {
    interface_t *interface = (interface_t *)user_data;
    size_t idx = MAX_FILE_LISTS;
    for (size_t i = 0; i < MAX_FILE_LISTS; i++) {
        if (strcmp(interface->machine->filelists[i].fdir, path) == 0) {
            idx = i;
            break;
        }
    }
    if (idx >= MAX_FILE_LISTS) { return; }
    interface->files_changed[idx] = true;
}

#define CONCAT(x, y) x ## y

#define CHECK_AND_CALL_CBS(cb_name) \
  do { \
    _d(-1, #cb_name " CHECK/CALL:"); \
    if (interface->machine_state_udated.cb_name##_changed) { \
        for (size_t i = 0; i < sizeof(interface->cb_name##_changed_cb) / sizeof(interface->cb_name##_changed_cb[0]); ++i) { \
             _df(-1, #cb_name " CHECKING %d (%p)", i, interface->cb_name##_changed_cb[i].cb_fn); \
            if (interface->cb_name##_changed_cb[i].cb_fn) { \
                _df(-1, #cb_name " CALLING %d (%p)", i, interface->cb_name##_changed_cb[i].cb_fn); \
               interface->cb_name##_changed_cb[i].cb_fn(interface->machine, interface->cb_name##_changed_cb[i].user_data); \
            } \
        } \
        interface->machine_state_udated.cb_name##_changed = false; \
    } \
  } while (0)

void interface_tick(interface_t *interface) {
    CHECK_AND_CALL_CBS(state);
    CHECK_AND_CALL_CBS(pos);
    CHECK_AND_CALL_CBS(home);
    CHECK_AND_CALL_CBS(wcs);
    CHECK_AND_CALL_CBS(feed);
    CHECK_AND_CALL_CBS(sensors);
    CHECK_AND_CALL_CBS(dialogs);
    CHECK_AND_CALL_CBS(spindles_tools);
    CHECK_AND_CALL_CBS(connected);
    CHECK_AND_CALL_CBS(current_move_axis);

    const machine_interface_t *mach = interface->machine;

    for (size_t i = 0; i < MAX_FILE_LISTS; ++i) {
        if (interface->files_changed[i]) {
            const char *fdir = mach->filelists[i].fdir;
            for (size_t j = 0; j < sizeof(interface->files_changed_cb) / sizeof(interface->files_changed_cb[0]); ++j) {
                if (interface->files_changed_cb[j].cb_fn && strcmp(fdir, interface->files_changed_cb[j].path) == 0) {
                    interface->files_changed_cb[j].cb_fn(interface->machine, interface->files_changed_cb[j].user_data, mach->filelists[i].fdir, mach->filelists[i].files);
                }
            }
            interface->files_changed[i] = false;
        }
    }
}


// Dialog-related functions

void modal_delete_cb(lv_event_t *e) {
    message_box_t *m = (message_box_t *) lv_event_get_user_data(e);
    lv_obj_t *mbox = (lv_obj_t *) m->user_data;

    machine_interface_modal_cancel(m->machine, m->seq);

    m->user_data = NULL;
    lv_msgbox_close(mbox);
}

void machine_cancel_cb(lv_event_t *e) {
    // message_box_t *m = (message_box_t *) lv_event_get_user_data(e);
    // lv_obj_t *mbox = (lv_obj_t *) m->user_data;
    lv_obj_t *mbox = (lv_obj_t *) lv_event_get_user_data(e);
    message_box_t *m = (message_box_t *) lv_obj_get_user_data(mbox);

    machine_interface_modal_cancel(m->machine, m->seq);

    //lv_obj_remove_event_cb(mbox, modal_delete_cb);
    m->user_data = NULL;
    lv_msgbox_close(mbox);
}

void machine_ok_cb(lv_event_t *e) {
    lv_obj_t *mbox = (lv_obj_t *) lv_event_get_user_data(e);
    message_box_t *m = (message_box_t *) lv_obj_get_user_data(mbox);
    _df(-1, "OK MBOX %s, %s => %d", m->title, m->text, m->seq);

    m->user_data = NULL;
    modal_close_handler(e);

    machine_interface_modal_ok(m->machine, m->seq);

    //lv_obj_remove_event_cb(mbox, modal_delete_cb);
}

void machine_choice_cb(lv_event_t *e) {
    lv_obj_t *mbox = (lv_obj_t *) lv_event_get_user_data(e);
    message_box_t *m = (message_box_t *) lv_obj_get_user_data(mbox);
    // message_box_t *m = (message_box_t *) lv_event_get_user_data(e);
    // lv_obj_t *mbox = (lv_obj_t *) m->user_data;

    lv_obj_t *tgt = lv_event_get_target(e);
    int choice = (int) lv_obj_get_user_data(tgt);

    m->user_data = NULL;
    lv_msgbox_close(mbox);
    lv_obj_del_async(mbox);

    _d(0, "MSG BOX CLOSED!");

    machine_interface_modal_choice(m->machine, choice, m->seq);

    // lv_obj_remove_event_cb(mbox, modal_delete_cb);
}

const size_t recolor_html_text_len(const char *s) {
    size_t len = strlen(s);
    size_t nesting = 0;
    size_t offs = 0;
    char stack[30];

    for (size_t i = 0; i < len; ++i) {
        if (s[i] == '<') {
            size_t oi = i;
            if (i == len - 1) { break; }
            ++i;
            if (s[i] == '/') {
                const size_t inc = stack[nesting] == '#' ? 2 : 0;
                if (nesting > 1) { --nesting; }
                while (++i < len && s[i] != '>') {}
                offs += oi - i + inc; // dec by length of closing tag plus closing of formatting ("# ").
            } else {
                const char *sp = &s[i];
                assert(nesting < (sizeof(stack) / sizeof(stack[0])) - 1);
                ++nesting;

                if (strncmp(sp, "b>", 2) == 0) {
                    offs += 8 - 3; // "<b>"" => "#aabbcc " (3 chars to 8 chars)
                    stack[nesting] = '#';
                    i+= 2;
                } else if (strncmp(sp, "br/>", 4) == 0) {
                    offs += 1 - 4; // "<br/>" => "\n"
                    stack[nesting] = '\n';
                    i+= 4;
                } else if (strncmp(sp, "br>", 3) == 0) {
                    offs += 1 - 4; // "<br>" => "\n"
                    stack[nesting] = '\n';
                    i+= 3;
                } else if (strncmp(sp, "i>", 3) == 0) {
                    offs += 8 - 3; // "<i>" => "#aabbcc "
                    stack[nesting] = '#';
                    i+= 3;
                } else if (strncmp(sp, "u>", 3) == 0) {
                    offs += 8 - 3; // "<u>" => "#aabbcc "
                    stack[nesting] = '#';
                    i+= 3;
                }
            }
        }
    }
    return len + offs;
}

const char *recolor_html_text(const char *s, char *res) {
    size_t len = strlen(s);
    size_t nesting = 0;
    size_t offs = 0;
    char stack[30];
    size_t o = 0;

#define INSERT(str) do { const char *colors = str; const size_t ll = strlen(colors); for (size_t jj = 0; jj < ll; ++jj) { res[o++] = colors[jj]; } } while(0)

    for (size_t i = 0; i < len; ++i) {
        if (s[i] == '<') {
            if (i == len - 1) { break; }
            ++i;
            if (s[i] == '/') {
                if (stack[nesting] == '#') { INSERT("# "); }

                assert(nesting > 0);
                --nesting;

                while (++i < len && s[i] != '>') {}
            } else {
                const char *sp = &s[i];
                assert(nesting < (sizeof(stack) / sizeof(stack[0])) - 1);
                ++nesting;

                if (strncmp(sp, "b>", 2) == 0) {
                    stack[nesting] = '#';
                    INSERT("#ff0000");
                    i += 2;
                } else if (strncmp(sp, "br/>", 4) == 0) {
                    stack[nesting] = '\n';
                    INSERT("\n");
                    i += 4;
                } else if (strncmp(sp, "br>", 3) == 0) {
                    stack[nesting] = '\n';
                    INSERT("\n");
                    i += 3;
                } else if (strncmp(sp, "i>", 3) == 0) {
                    stack[nesting] = '#';
                    INSERT("#00ff00");
                    i += 3;
                } else if (strncmp(sp, "u>", 3) == 0) {
                    stack[nesting] = '#';
                    INSERT("#0000ff");
                    i += 3;
                }
            }
        }
    }
    res[o] = '\0';

    return res;
}

// <b></b> #ffffff # 
void message_box_t_modal(machine_interface_t *mach, void *user_data) {
    interface_t *interface = (interface_t *)user_data;
    message_box_t *m = (message_box_t *) interface->machine->message_box;
    if (!m) { return; }
    LOGI(TAG, "UPDATE MBOX %s, %s => %d", m->title, m->text, m->seq);

    char title[64];
    snprintf(title, sizeof(title), "%s (%d)", m->title, m->seq);

    close_curr_modal();

#ifdef RECOLOR_HTML_TEXT
    size_t ftlen = recolor_html_text_len(m->text);
    char ftext[ftlen + 1];
    recolor_html_text(m->text, ftext);
#else
    char *ftext = m->text;
#endif

    switch (m->mode) {
    case MESSAGE_INFO:
    case MESSAGE:
        do {
            const char *buttons[] = { "Close", NULL };
            const modal_button_cb_t cbs[] = { machine_ok_cb, NULL };
            lv_obj_t * res = button_modal(title, ftext, buttons, cbs, m);
            m->user_data = res;
            //lv_obj_add_event_cb(res, modal_delete_cb, LV_EVENT_DELETE, m);
        } while (0);
        break;
    case MESSAGE_OK:
        do {
            const char *buttons[] = { "OK", NULL };
            const modal_button_cb_t cbs[] = { machine_ok_cb, NULL };
            lv_obj_t *res = button_modal(title, ftext, buttons, cbs, m);
            m->user_data = res;
            //lv_obj_add_event_cb(res, modal_delete_cb, LV_EVENT_DELETE, m);
        } while (0);
        break;
    case MESSAGE_OK_CANCEL:
        do {
            const char *buttons[] = { "OK", "Cancel", NULL };
            const modal_button_cb_t cbs[] = { machine_ok_cb, machine_cancel_cb, NULL };
            lv_obj_t *res = button_modal(title, ftext, buttons, cbs, m);
            m->user_data = res;
            //lv_obj_add_event_cb(res, modal_delete_cb, LV_EVENT_DELETE, m);
        } while (0);
        break;
    case MESSAGE_CHOICE:
        do {
            lv_obj_t *res = lv_msgbox_create(lv_scr_act());
            m->user_data = res;
            lv_msgbox_add_title(res, title);
            lv_obj_t *txt = lv_msgbox_add_text(res, ftext);
            lv_label_set_recolor(txt, true);
            lv_obj_set_user_data(res, m);
            set_curr_modal_and_close_prev(res);

            _df(0, "NEW MBOX: %p", res);

            //lv_obj_add_event_cb(res, modal_delete_cb, LV_EVENT_DELETE, m);

            for (size_t i = 0; i < m->num_choices; i++) {
                lv_obj_t *btn = lv_msgbox_add_footer_button(res, m->choices[i]);
                lv_obj_set_user_data(btn, (void*) i);
                lv_obj_add_event_cb(btn, machine_choice_cb, LV_EVENT_CLICKED, res);
            }
        } while (0);
        break;
    default:
        LOGW(TAG, "Unsupported message box type: S%d - implementation TODO [M_INFO = %d, M_OK = %d, M_OK_CANCEL = %d, M_CHOICE = %d].", m->mode, MESSAGE_INFO, MESSAGE_OK, MESSAGE_OK_CANCEL, MESSAGE_CHOICE);
        break;
    }
}

add_callback_fn(interface, state_changed)
add_callback_fn(interface, pos_changed)
add_callback_fn(interface, home_changed)
add_callback_fn(interface, wcs_changed)
add_callback_fn(interface, feed_changed)
add_callback_fn(interface, sensors_changed)
add_callback_fn(interface, dialogs_changed)
add_callback_fn(interface, spindles_tools_changed)
add_callback_fn(interface, connected_changed)
add_callback_fn(interface, current_move_axis_changed)

bool interface_add_files_changed_cb(interface_t *self, const char *path, void *user_data, files_changed_callback_cb_t cb) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!self->files_changed_cb[i].cb_fn) {
            self->files_changed_cb[i].cb_fn = cb;
            self->files_changed_cb[i].user_data = user_data;

            machine_interface_add_files_changed_cb(self->machine, path, self, _mach_files_changed);

            return true;
        }
    }
    assert(0 && "Maximum number of callbacks reached");
    return false;
}
