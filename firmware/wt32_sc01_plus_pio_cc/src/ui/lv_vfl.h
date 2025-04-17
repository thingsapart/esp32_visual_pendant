#ifndef LV_VFL_H
#define LV_VFL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h> // For size_t

// --- Configuration ---
#ifndef LV_VFL_DEFAULT_GAP
#define LV_VFL_DEFAULT_GAP 10 // Default gap in pixels for '-'
#endif
#ifndef LV_VFL_MAX_GRID_SIZE // Define max grid rows/cols to avoid excessive dynamic alloc
#define LV_VFL_MAX_GRID_COLS 16
#define LV_VFL_MAX_GRID_ROWS 16
#endif


// Structure to map view names (strings) to LVGL object pointers
typedef struct {
    const char *name;
    lv_obj_t *obj;
} lv_vfl_view_map_t;

// Structure for user-defined parameters (gaps, sizes)
typedef struct {
    const char *name;
    lv_coord_t value; // Can represent pixels or percentage (use LV_PCT)
} lv_vfl_param_map_t;

// Error codes
typedef enum {
    LV_VFL_OK = 0,
    LV_VFL_ERR_INVALID_FORMAT,
    LV_VFL_ERR_UNKNOWN_VIEW,
    LV_VFL_ERR_UNKNOWN_PARAM,
    LV_VFL_ERR_INVALID_CONSTRAINT,
    LV_VFL_ERR_MEM_ALLOC, // Will be used for grid arrays now
    LV_VFL_ERR_NULL_ARG,
    LV_VFL_ERR_GRID_TOO_LARGE,
    LV_VFL_ERR_MIXED_LAYOUT_TYPES, // e.g. H: inside G: not supported
} lv_vfl_result_t;

/**
 * @brief Applies a Visual Format Language string to configure LVGL Flex or Grid layout.
 *
 * @param container The parent LVGL object whose children will be arranged.
 * @param vfl_string The VFL string defining the layout.
 * @param view_map Array mapping view names used in vfl_string to lv_obj_t* pointers.
 * @param view_map_count Number of entries in the view_map array.
 * @param param_map Array mapping parameter names (for gaps/sizes) to lv_coord_t values. Can be NULL.
 * @param param_map_count Number of entries in the param_map array.
 * @return LV_VFL_OK on success, or an error code on failure.
 *
 * Supported Format Examples:
 *  --- Flexbox (H: or V:) ---
 *  - "H:[v1]-[v2]"                  // Horizontal, default gap
 *  - "V:[l1]-5-[i1]"                // Vertical, 5px gap
 *  - "H:|[icon(30)]-[text]|"         // Horizontal, fixed edges, icon width 30px
 *  - "V:[item1(100)]"               // Vertical, item1 height 100px
 *  - "H:[label(40%)]-[field]"        // Horizontal, label width 40%
 *  - "H:[button1(:1)]-[button2(:2)]" // Horizontal, btn1 flex-grow 1, btn2 flex-grow 2
 *  - "H:|-margin-[content]-margin-|" // Use named 'margin' gap (defined in param_map)
 *  - "V:[header(h)]-[body(:1)]"      // Use named 'h' height, body fills rest
 *
 *  --- Grid (G:) ---
 *  - "G:|[col1]-[col2]|"             // Simple 1-row, 2-column grid
 *  - "G:|[r1c1]-[r1c2(100)]|\n|[r2c1(30%)]-[r2c2]" // 2x2 grid, size constraints apply to item width
 *                                                // Uses first gap found for row/col gap.
 *                                                // All tracks default to LV_GRID_CONTENT.
 *  - "G:|[b1]-gap-[b2]|\n-gap-|[b3]-gap-[b4]" // Specify named gap
 *
 * Limitations:
 *  - Flex: Only one gap value (first specified non-default) applies to the whole container.
 *  - Grid: Basic implementation. Tracks default to LV_GRID_CONTENT. Uses first gap found for both row/col gaps.
 *          Size constraints `(size)` apply to item width (fixed/pct only). Flex grow `(:n)` is ignored in Grid.
 *  - Views MUST be direct children of the container.
 */
lv_vfl_result_t lv_vfl_apply(lv_obj_t *container,
                             const char *vfl_string,
                             const lv_vfl_view_map_t *view_map,
                             size_t view_map_count,
                             const lv_vfl_param_map_t *param_map, /* NEW */
                             size_t param_map_count);            /* NEW */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* LV_VFL_H */