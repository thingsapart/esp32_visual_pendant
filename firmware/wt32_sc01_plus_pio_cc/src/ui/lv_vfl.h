#ifndef LV_VFL_H
#define LV_VFL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h> // For variadic grid item function

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR < 2
#warning "lv_vfl requires LVGL version 9.2 or later."
#endif

//------------------------------------------------------------------------------
// Internal Enums, Structs, Constants
//------------------------------------------------------------------------------

// --- Alignment Sentinel ---
// Sentinel value for alignment to inherit from container (for H/V layout)
#define _VFL_ALIGN_INHERIT ((lv_align_t)0xFF) // Internal constant

// --- For Linear Layout ---
typedef enum {
    _VFL_ITEM_OBJ,
    _VFL_ITEM_SPACE
} _vfl_linear_item_type_t;

typedef enum {
    _VFL_SIZE_FIXED,    // Specific pixel value
    _VFL_SIZE_CONTENT,  // Size based on object's content
    _VFL_SIZE_FLEX      // Proportional share of remaining space
} _vfl_size_type_t;

typedef struct {
    _vfl_linear_item_type_t item_type;
    lv_coord_t value;           // Space value, Fixed size value, or Flex factor
    _vfl_size_type_t size_type; // Used only for ITEM_OBJ
    lv_obj_t *obj;              // Used only for ITEM_OBJ
    lv_align_t cross_align;     // Used only for ITEM_OBJ (alignment override)
} _vfl_linear_item_t;


// --- For Grid Layout ---
typedef struct {
    lv_obj_t* obj;
    uint16_t row;
    uint16_t col;
    uint8_t row_span;
    uint8_t col_span;
    lv_grid_align_t h_align;
    lv_grid_align_t v_align;
} _vfl_grid_item_placement_t; // Internal struct for variadic function

// Sentinel for terminating variadic grid item list
#define _VFL_GRID_ITEM_SENTINEL ((_vfl_grid_item_placement_t){.obj=NULL}) // Internal


//------------------------------------------------------------------------------
// Public Macros for Layout Definition (User-Facing API)
//------------------------------------------------------------------------------

// --- Linear Layout (H/V) ---

/**
 * @brief Defines a horizontal layout.
 * @param PARENT The parent lv_obj_t* container.
 * @param ALIGN Default cross-axis alignment (e.g., LV_ALIGN_TOP_MID, LV_ALIGN_CENTER).
 * @param ... Sequence of layout items (_obj, _space).
 * Example:
 *  _layout_h(container, LV_ALIGN_CENTER,
 *      _space(10),
 *      _obj(btn1, _fixed(80)),
 *      _space(), // Default space
 *      _obj(label, _content(), LV_ALIGN_BOTTOM_MID), // Override alignment
 *      _space(),
 *      _obj(slider, _flex(1)),
 *      _space(10)
 *  );
 */
#define _layout_h(PARENT, ALIGN, ...) \
    do { \
        lv_obj_t* _vfl_parent_h = (PARENT); \
        _vfl_linear_item_t _vfl_items_h[] = { __VA_ARGS__ }; \
        size_t _vfl_item_count_h = sizeof(_vfl_items_h) / sizeof(_vfl_items_h[0]); \
        _vfl_do_linear_layout(_vfl_parent_h, _vfl_items_h, _vfl_item_count_h, true, ALIGN); \
    } while (0)

/**
 * @brief Defines a vertical layout.
 * @param PARENT The parent lv_obj_t* container.
 * @param ALIGN Default cross-axis alignment (e.g., LV_ALIGN_LEFT_MID, LV_ALIGN_CENTER).
 * @param ... Sequence of layout items (_obj, _space).
 */
#define _layout_v(PARENT, ALIGN, ...) \
    do { \
        lv_obj_t* _vfl_parent_v = (PARENT); \
        _vfl_linear_item_t _vfl_items_v[] = { __VA_ARGS__ }; \
        size_t _vfl_item_count_v = sizeof(_vfl_items_v) / sizeof(_vfl_items_v[0]); \
        _vfl_do_linear_layout(_vfl_parent_v, _vfl_items_v, _vfl_item_count_v, false, ALIGN); \
    } while (0)


// --- Linear Item Specification Macros ---

/**
 * @brief Represents an object in the linear layout sequence.
 * @param lobj Pointer to the lv_obj_t object.
 * @param size_spec_result The result of _fixed, _content, or _flex macro.
 * @param ... Optional: Alignment override for this object (e.g., LV_ALIGN_TOP_LEFT).
 * Example: _obj(my_button, _fixed(100))
 * Example: _obj(my_label, _content(), LV_ALIGN_BOTTOM_MID)
 */
#define _obj(lobj, size_spec_result, ...) \
    { \
        .item_type = _VFL_ITEM_OBJ, \
        .obj = (lobj), \
        size_spec_result, /* Expands to .size_type=..., .value=... */ \
        .cross_align = _VFL_ALIGN_INHERIT __VA_OPT__(, __VA_ARGS__) /* Override default if arg provided */ \
    }

/** @brief Specifies a fixed size (width for H layout, height for V layout). */
#define _fixed(v)   .size_type = _VFL_SIZE_FIXED,   .value = (lv_coord_t)(v)

/** @brief Specifies that the object's size should be determined by its content. */
#define _content()  .size_type = _VFL_SIZE_CONTENT, .value = 0 /* Value calculated later */

/** @brief Specifies flexible size based on a factor. */
#define _flex(factor) .size_type = _VFL_SIZE_FLEX,    .value = (lv_coord_t)((factor) > 0 ? (factor) : 1)

/**
 * @brief Represents spacing in the layout sequence.
 * @param ... Optional: Fixed spacing value in pixels. If omitted, uses parent's default spacing.
 * Example: _space() // Default space (pad_row/pad_column)
 * Example: _space(10) // Fixed 10px space
 */
#define _space(...) \
    { \
        .item_type = _VFL_ITEM_SPACE, \
        .value = -1 __VA_OPT__(, __VA_ARGS__), /* Default -1, overridden by arg if present */ \
        .size_type = 0, /* Unused */ \
        .obj = NULL,    /* Not an object */ \
        .cross_align = _VFL_ALIGN_INHERIT /* Unused */ \
    }


// --- Grid Layout ---

/**
 * @brief Defines a grid layout using LVGL's grid engine.
 * @param PARENT The parent lv_obj_t* container.
 * @param COLS_DEF Column definitions using _cols(...) macro.
 * @param ROWS_DEF Row definitions using _rows(...) macro.
 * @param ... Sequence of grid items defined using _grid_item(...).
 * Example:
 *  _layout_grid(container,
 *      _cols(_fr(1), _px(100), _content()),
 *      _rows(_px(50), _fr(1)),
 *      _grid_item(.obj=btn1, .row=0, .col=0),
 *      _grid_item(.obj=label, .row=0, .col=1, .col_span=2, .h_align=LV_GRID_ALIGN_CENTER),
 *      _grid_item(.obj=slider, .row=1, .col=0, .col_span=3, .v_align=LV_GRID_ALIGN_STRETCH)
 *  );
 */
#define _layout_grid(PARENT, COLS_DEF, ROWS_DEF, ...) \
    do { \
        lv_obj_t* _vfl_grid_parent = (PARENT); \
        if(!_vfl_grid_parent) { LV_LOG_WARN("_layout_grid: NULL parent"); break; } \
        lv_obj_set_layout(_vfl_grid_parent, LV_LAYOUT_GRID); \
        const lv_coord_t _vfl_grid_cols[] = COLS_DEF; \
        const lv_coord_t _vfl_grid_rows[] = ROWS_DEF; \
        lv_obj_set_grid_dsc_array(_vfl_grid_parent, _vfl_grid_cols, _vfl_grid_rows); \
        _vfl_place_grid_items(_vfl_grid_parent, ##__VA_ARGS__, _VFL_GRID_ITEM_SENTINEL); \
        lv_obj_update_layout(_vfl_grid_parent); \
    } while(0)

// --- Grid Definition Helpers ---

/** @brief Defines column track sizes for _layout_grid. Use _px, _fr, _content inside. */
#define _cols(...) { __VA_ARGS__, LV_GRID_TEMPLATE_LAST }
/** @brief Defines row track sizes for _layout_grid. Use _px, _fr, _content inside. */
#define _rows(...) { __VA_ARGS__, LV_GRID_TEMPLATE_LAST }

// --- Grid Track Size Constants ---
/** @brief Fixed pixel size for grid track. */
#define _px(x)     (x)
/** @brief Fractional unit size for grid track. */
#define _fr(x)     LV_GRID_FR(x)
/* Note: _content() macro already defined for linear layout is reused for grid tracks */


// --- Grid Item Placement Macro ---

/**
 * @brief Specifies placement and properties of an object within the grid.
 *        Use C99 designated initializers.
 * @param ... Designated initializers for _vfl_grid_item_placement_t struct:
 *            .obj = (lv_obj_t*) [REQUIRED]
 *            .row = (uint16_t) [REQUIRED]
 *            .col = (uint16_t) [REQUIRED]
 *            .row_span = (uint8_t) [Optional, default=1]
 *            .col_span = (uint8_t) [Optional, default=1]
 *            .h_align = (lv_grid_align_t) [Optional, default=LV_GRID_ALIGN_STRETCH]
 *            .v_align = (lv_grid_align_t) [Optional, default=LV_GRID_ALIGN_STRETCH]
 * Example: _grid_item(.obj=my_button, .row=1, .col=2)
 * Example: _grid_item(.obj=my_img, .row=0, .col=0, .row_span=2, .h_align=LV_GRID_ALIGN_CENTER)
 */
#define _grid_item(...) \
    ((_vfl_grid_item_placement_t){ \
        .obj=NULL, .row=0, .col=0, /* Defaults for safety */ \
        .row_span=1, .col_span=1, \
        .h_align=LV_GRID_ALIGN_STRETCH, .v_align=LV_GRID_ALIGN_STRETCH, \
        __VA_ARGS__ /* Apply user-provided initializers */ \
    })


//------------------------------------------------------------------------------
// Internal Core Layout Function Prototypes (Implementation in lv_vfl.c)
//------------------------------------------------------------------------------

/** @brief Internal: Performs linear (H/V) layout. */
void _vfl_do_linear_layout(lv_obj_t *parent,
                           _vfl_linear_item_t items[],
                           size_t item_count,
                           bool is_horizontal,
                           lv_align_t default_cross_align);

/** @brief Internal: Places items onto the LVGL grid. */
void _vfl_place_grid_items(lv_obj_t *parent, ...);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_VFL_H */