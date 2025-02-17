// file_list.h
#ifndef FILE_LIST_H
#define FILE_LIST_H

#include "lvgl.h"
#include "machine/machine_interface.h"  // Assuming machine_interface is defined here.

typedef struct file_list_t file_list_t;

// Callback function type for when a file is clicked
typedef void (*file_click_callback_t)(file_list_t *fl, const char *filename);

struct file_list_t {
    lv_obj_t *list;          // The LVGL list object
    const char *path;        // The path to list files from (e.g., "gcodes", "macros")
    machine_interface_t *machine;  // Pointer to the machine interface
    file_click_callback_t on_file_click; // Callback function
    lv_obj_t* refresh_btn; // optional refresh button.
};

file_list_t *file_list_create(lv_obj_t *parent, const char *path,
                               machine_interface_t *machine,
                               file_click_callback_t on_file_click);
void file_list_destroy(file_list_t *fl);
void file_list_refresh(file_list_t *fl);
void file_list_show_files(file_list_t *fl, const char **files);
void file_list_init_empty(file_list_t *fl);

#endif // FILE_LIST_H