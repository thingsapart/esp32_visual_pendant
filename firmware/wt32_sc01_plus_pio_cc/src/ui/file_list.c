// file_list.c
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "lvgl.h"

#include "file_list.h"
#include "ui_helpers.h" // Your LVGL helper macros

// --- Helper Functions ---

// Capitalize the first letter of the path (for display)
static char *path_capitalize(const char *path, char *buffer, size_t buffer_size) {
    if (!path || !buffer || buffer_size == 0) {
        return NULL; // Handle invalid input
    }

    if (strlen(path) == 0)
    {
        buffer[0] = '\0';
        return buffer;
    }

    strncpy(buffer, path, buffer_size - 1);
    buffer[buffer_size - 1] = '\0'; // Ensure null termination
    buffer[0] = toupper(buffer[0]);   // Capitalize the first letter
    return buffer;
}

// --- Event Handlers ---

static void btn_click_event_handler(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    file_list_t *fl = (file_list_t *)lv_event_get_user_data(e);
    const char *filename = lv_list_get_button_text(fl->list, btn);

    if (fl && fl->on_file_click && filename) {
        fl->on_file_click(fl, filename);
    }
}

// --- Callback Functions (from machine_interface) ---
static void files_changed_cb(machine_interface_t* self, void *user_data, const char* path, const char** files)
{
     file_list_t *fl = (file_list_t *) user_data;

    // Check if this update is for this file_list instance
    if (fl && strcmp(fl->path, path) == 0) {
        file_list_show_files(fl, files);
    }
}

// --- File List Implementation ---

file_list_t *file_list_create(lv_obj_t *parent, const char *path,
                               machine_interface_t *machine,
                               file_click_callback_t on_file_click) {

    if(!parent || !path || !machine) {
        LV_LOG_ERROR("file_list_create: invalid arguments");
        return NULL;
    }

    file_list_t *fl = (file_list_t *)malloc(sizeof(file_list_t));
    if (!fl) {
        LV_LOG_ERROR("Failed to allocate file_list_t");
        return NULL;
    }
    memset(fl, 0, sizeof(file_list_t));

    fl->path = path; // Assume path is a string literal or a dynamically allocated, persistent string
    fl->machine = machine;
    fl->on_file_click = on_file_click;
    fl->list = lv_list_create(parent);
     if (!fl->list) {
        LV_LOG_ERROR("Failed to create list object");
        free(fl);
        return NULL;
    }

    // store file_list_t* in user data:
    lv_obj_set_user_data(fl->list, fl);

    machine_interface_add_files_changed_cb(machine, path, fl, files_changed_cb);

    // initial refresh:
    file_list_init_empty(fl);
    return fl;
}

void file_list_destroy(file_list_t *fl) {
    if (!fl) return;

    // Unregister the callback (important to prevent dangling pointers)
    // Find the matching callback and clear it
    for (int i = 0; i < MAX_CALLBACKS; i++)
    {
        // Use function pointer and path to identify.
        if(fl->machine->files_changed_cb[i].cb_fn == files_changed_cb &&
           strcmp(fl->machine->files_changed_cb[i].path, fl->path) == 0)
        {
             fl->machine->files_changed_cb[i].cb_fn = NULL;
             fl->machine->files_changed_cb[i].path = NULL;
             break; // callback unregistered, exit loop.
        }
    }

     // Delete the LVGL list object
    if (fl->list) {
        lv_obj_del(fl->list);  // This also deletes the buttons
    }
    free(fl);
}

void file_list_refresh(file_list_t *fl) {
    if (!fl) return;
    fl->machine->list_files(fl->machine, fl->path); // list files and trigger callback.
}

void file_list_show_files(file_list_t *fl, const char **files) {
    if (!fl || !files) return;

     // Clear the existing list
    lv_obj_clean(fl->list);

     // Add the title (capitalized path)
    char path_buf[64]; // Buffer for capitalized path
    lv_obj_t *lbl = lv_list_add_text(fl->list, path_capitalize(fl->path, path_buf, sizeof(path_buf)));
    if (!lbl)
    {
        LV_LOG_ERROR("Failed to create title in file list.");
        return;
    }
    _height(lbl, 20);

    // Add buttons for each file
    if(!files) return; // nothing more to do.
    
    int i=0;
    while (files[i] != NULL) {
        lv_obj_t *btn = lv_list_add_btn(fl->list, LV_SYMBOL_FILE, files[i]);
        if(!btn)
        {
             LV_LOG_ERROR("Failed to create file list button for %s", files[i]);
             continue; // skip to next file
        }
        lv_obj_add_event_cb(btn, btn_click_event_handler, LV_EVENT_CLICKED, fl); // Pass fl as user_data
        i++;
    }
}

void file_list_init_empty(file_list_t *fl)
{
    if(!fl) return;
    lv_obj_clean(fl->list); // same as clean();

    char path_buf[64];
    lv_obj_t *lbl = lv_list_add_text(fl->list, path_capitalize(fl->path, path_buf, sizeof(path_buf)));
    if (!lbl) {
        LV_LOG_ERROR("Failed to create title in file list.");
        return;
    }
    _height(lbl, 20);

    lv_obj_t* btn = lv_list_add_btn(fl->list, LV_SYMBOL_REFRESH, "Waiting for connection...");
    if (!btn)
    {
        LV_LOG_ERROR("Failed to create refresh button.");
        return;
    }
    lv_obj_add_event_cb(btn, btn_click_event_handler, LV_EVENT_CLICKED, fl);
}