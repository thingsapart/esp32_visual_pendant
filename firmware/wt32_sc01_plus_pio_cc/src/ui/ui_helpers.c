#include "ui_helpers.h"
#include <string.h>

typedef struct {
    char id[MAX_ID_LENGTH];
    lv_obj_t *obj;
} object_entry_t;

static object_entry_t object_registry[MAX_OBJECTS];
static bool registry_initialized = false;
static uint32_t object_count = 0;

void init_object_registry(void) {
    if (!registry_initialized) {
        memset(object_registry, 0, sizeof(object_registry));
        registry_initialized = true;
        object_count = 0;
    }
}

lv_obj_t *get_object_by_id(const char *id) {
    if (!registry_initialized) {
        LV_LOG_WARN("Object registry not initialized!");
        return NULL;
    }

    for (uint32_t i = 0; i < object_count; i++) {
        if (strncmp(object_registry[i].id, id, MAX_ID_LENGTH) == 0) {
            return object_registry[i].obj;
        }
    }

    LV_LOG_WARN("Object not found: %s", id);
    return NULL;
}

bool register_object(const char *id, lv_obj_t *obj) {
    if (id == NULL || obj == NULL) {
        return false;
    }

    if (!registry_initialized) {
        LV_LOG_ERROR("Object registry not initialized!");
        return false;
    }

    if (object_count >= MAX_OBJECTS) {
        LV_LOG_ERROR("Object registry full!");
        return false;
    }

    if (strlen(id) >= MAX_ID_LENGTH) {
        LV_LOG_ERROR("Object ID too long: %s", id);
        return false;
    }

    // Check for duplicate IDs
    for (uint32_t i = 0; i < object_count; i++) {
        if (strncmp(object_registry[i].id, id, MAX_ID_LENGTH) == 0) {
            LV_LOG_ERROR("Duplicate object ID: %s", id);
            return false;
        }
    }

    strncpy(object_registry[object_count].id, id, MAX_ID_LENGTH - 1);
    object_registry[object_count].id[MAX_ID_LENGTH - 1] = '\0'; // Ensure null termination
    object_registry[object_count].obj = obj;
    object_count++;
    return true;
}

lv_obj_t *_maximize_client_area(lv_obj_t *obj) {
    _margin(obj, 0);
    _pad(obj, 0);
    _border_width(obj, 0, LV_PART_MAIN);
    return obj;
}

void dbg_layout(lv_obj_t *obj) {
    _bg_color(obj, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    _border_color(obj, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    _border_width(obj, 1, LV_PART_MAIN);
}