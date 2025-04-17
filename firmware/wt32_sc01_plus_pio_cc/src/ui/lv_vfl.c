#include "lv_vfl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h> // For LV_COORD_MAX check if needed

static const char *TAG = "ui/lv_vfl";

// Override LVGL debug functions.
#if 1

#include "debug.h"

#undef LV_LOG_INFO
#undef LV_LOG_WARN
#undef LV_LOG_ERROR

#define LV_LOG_INFO(fmt, ...) LOGI(TAG, fmt __VA_OPT__(, )##__VA_ARGS__)
#define LV_LOG_ERROR(fmt, ...) LOGE(TAG, fmt __VA_OPT__(, )##__VA_ARGS__)
#define LV_LOG_WARN(fmt, ...) LOGW(TAG, fmt __VA_OPT__(, )##__VA_ARGS__)

#endif


// --- Parameter Map Lookup ---
static lv_coord_t find_param_value(const char *name, size_t name_len, const lv_vfl_param_map_t *param_map, size_t map_count) {
    if (!name || !param_map) {
        return LV_COORD_MIN; // Indicate not found with an invalid coord value
    }
    for (size_t i = 0; i < map_count; ++i) {
        if (param_map[i].name && strncmp(param_map[i].name, name, name_len) == 0 && param_map[i].name[name_len] == '\0') {
            return param_map[i].value;
        }
    }
    LV_LOG_WARN("VFL: Parameter '%.*s' not found in param_map.", (int)name_len, name);
    return LV_COORD_MIN; // Not found
}


// --- View Map Lookup (existing) ---
static lv_obj_t* find_view_obj(const char *name, size_t name_len, const lv_vfl_view_map_t *view_map, size_t map_count) {
    // ... (same as before) ...
    if (!name || !view_map) {
        return NULL;
    }
    for (size_t i = 0; i < map_count; ++i) {
        if (view_map[i].name && strncmp(view_map[i].name, name, name_len) == 0 && view_map[i].name[name_len] == '\0') {
            return view_map[i].obj;
        }
    }
    LV_LOG_WARN("VFL: View '%.*s' not found in map.", (int)name_len, name);
    return NULL; // Not found
}

// --- Skip Whitespace (existing) ---
static const char* skip_whitespace(const char *str) {
    // ... (same as before) ...
     while (*str && (isspace((unsigned char)*str) || *str == '\n' || *str == '\r')) { // Include newline skipping
        str++;
    }
    return str;
}

// --- Internal Structure for Parsed Size ---
typedef enum {
    SIZE_TYPE_CONTENT, // Default
    SIZE_TYPE_FIXED,   // Pixels
    SIZE_TYPE_PERCENT, // Percentage
    SIZE_TYPE_GROW,    // Flex grow factor
    SIZE_TYPE_PARAM,   // Named parameter
    SIZE_TYPE_INVALID
} lv_vfl_size_type_t;

typedef struct {
    lv_vfl_size_type_t type;
    lv_coord_t value; // Used for FIXED, PERCENT, GROW, PARAM (after lookup)
} lv_vfl_parsed_size_t;

// --- Internal Parser State ---
// Encapsulate state to pass around if breaking into more functions later
typedef struct {
    lv_obj_t *container;
    const char *p; // Current parser position
    const lv_vfl_view_map_t *view_map;
    size_t view_map_count;
    const lv_vfl_param_map_t *param_map;
    size_t param_map_count;

    // Common attributes
    lv_coord_t gap;
    bool gap_set; // Tracks if non-default gap was specified

    // Flex specific
    bool is_vertical;
    bool start_edge;
    bool end_edge;

    // Grid specific
    uint16_t grid_rows;
    uint16_t grid_cols;
    uint16_t grid_max_cols; // Max columns found so far
    // Temp storage for grid cell info if needed for 2-pass approach
    // struct grid_item { lv_obj_t* obj; uint16_t r, c; } grid_items[...];

} lv_vfl_parser_state_t;


// --- Helper: Parse Size Constraint ---
static lv_vfl_result_t parse_size_constraint(lv_vfl_parser_state_t *state, lv_vfl_parsed_size_t *parsed_size) {
    parsed_size->type = SIZE_TYPE_CONTENT; // Default
    parsed_size->value = LV_SIZE_CONTENT;
    const char* start = state->p;

    if (*(state->p) == '(') {
        state->p++; // Skip '('
        const char *val_start = state->p;

        if (*(state->p) == ':') { // Flex Grow :N
            state->p++; // Skip ':'
             if (isdigit((unsigned char)*(state->p))) {
                char *endptr;
                long num_grow = strtol(state->p, &endptr, 10);
                if (state->p != endptr && num_grow > 0) {
                    parsed_size->type = SIZE_TYPE_GROW;
                    parsed_size->value = (lv_coord_t)num_grow;
                    state->p = endptr;
                } else {
                    LV_LOG_ERROR("VFL: Invalid flex grow value after ':'.");
                    return LV_VFL_ERR_INVALID_CONSTRAINT;
                }
            } else {
                LV_LOG_ERROR("VFL: Expected number after flex grow ':'.");
                return LV_VFL_ERR_INVALID_CONSTRAINT;
            }
        } else if (isdigit((unsigned char)*(state->p))) { // Numeric or Percent
            char *endptr;
            long num_size = strtol(state->p, &endptr, 10);
            if (state->p != endptr && num_size >= 0) {
                state->p = endptr;
                if (*(state->p) == '%') { // Percent N%
                    parsed_size->type = SIZE_TYPE_PERCENT;
                    parsed_size->value = (lv_coord_t)num_size;
                    state->p++; // Skip '%'
                } else { // Fixed numeric N
                    parsed_size->type = SIZE_TYPE_FIXED;
                    parsed_size->value = (lv_coord_t)num_size;
                }
            } else {
                 LV_LOG_ERROR("VFL: Invalid numeric size value inside '()'.");
                 return LV_VFL_ERR_INVALID_CONSTRAINT;
            }
        } else if (isalpha((unsigned char)*(state->p))) { // Named parameter
             const char* name_start = state->p;
             while (isalnum((unsigned char)*(state->p)) || *(state->p) == '_') {
                state->p++;
             }
             size_t name_len = state->p - name_start;
             if (name_len > 0) {
                 lv_coord_t param_val = find_param_value(name_start, name_len, state->param_map, state->param_map_count);
                 if (param_val != LV_COORD_MIN) {
                     parsed_size->type = SIZE_TYPE_PARAM; // Type is PARAM, value holds the looked-up coord
                     parsed_size->value = param_val;
                 } else {
                      return LV_VFL_ERR_UNKNOWN_PARAM; // Error logged by find_param_value
                 }
             } else {
                 LV_LOG_ERROR("VFL: Empty parameter name inside '()'.");
                 return LV_VFL_ERR_INVALID_CONSTRAINT;
             }
        } else {
            LV_LOG_ERROR("VFL: Unexpected character '%c' inside size '()'.", *(state->p));
            return LV_VFL_ERR_INVALID_CONSTRAINT;
        }

        // Expect closing ')'
        state->p = skip_whitespace(state->p); // Allow space before ')'
        if (*(state->p) == ')') {
            state->p++; // Skip ')'
        } else {
            LV_LOG_ERROR("VFL: Missing closing ')' for size constraint near '%.*s'.", (int)(state->p-start), start);
            return LV_VFL_ERR_INVALID_FORMAT;
        }
    }
    // If no '(', parsed_size remains CONTENT/default
    return LV_VFL_OK;
}

// --- Helper: Parse Connection/Gap ---
static lv_vfl_result_t parse_connection(lv_vfl_parser_state_t *state) {
    const char* start = state->p;
    if (*(state->p) == '-') {
        state->p++; // Skip '-'

        // Check for numeric gap -N-
        if (isdigit((unsigned char)*(state->p))) {
            char *endptr;
            long num_gap = strtol(state->p, &endptr, 10);
            if (state->p != endptr && num_gap >= 0) {
                if (!state->gap_set) {
                    state->gap = (lv_coord_t)num_gap;
                    state->gap_set = true;
                    LV_LOG_INFO("VFL: Setting gap to %dpx (numeric).", state->gap);
                } else {
                     LV_LOG_WARN("VFL: Ignoring subsequent numeric gap. Using first value: %dpx", state->gap);
                }
                state->p = endptr;
            } else {
                LV_LOG_ERROR("VFL: Invalid numeric gap after '-'.");
                return LV_VFL_ERR_INVALID_FORMAT;
            }
        // Check for named gap -name-
        } else if (isalpha((unsigned char)*(state->p))) {
             const char* name_start = state->p;
             while (isalnum((unsigned char)*(state->p)) || *(state->p) == '_') {
                state->p++;
             }
             size_t name_len = state->p - name_start;
             if (name_len > 0) {
                 lv_coord_t param_val = find_param_value(name_start, name_len, state->param_map, state->param_map_count);
                 if (param_val != LV_COORD_MIN) {
                      if (!state->gap_set) {
                          state->gap = param_val;
                          state->gap_set = true;
                          LV_LOG_INFO("VFL: Setting gap to %d (parameter '%.*s').", state->gap, (int)name_len, name_start);
                      } else {
                         LV_LOG_WARN("VFL: Ignoring subsequent named gap. Using first value: %dpx", state->gap);
                      }
                 } else {
                      return LV_VFL_ERR_UNKNOWN_PARAM; // Error logged by find_param_value
                 }
             } else {
                // Just a single '-' gap, use default
             }
        }
        // Else: just a single '-', default gap applies if not already set by number/param

        // Expect closing '-' for numeric or named gap
        if (isdigit((unsigned char)*start) || isalpha((unsigned char)*start)) { // Check char *after* initial '-'
            if (*(state->p) == '-') {
                state->p++; // Skip closing '-'
            } else {
                 LV_LOG_ERROR("VFL: Numeric or named gap must be enclosed in '-', e.g., '-10-' or '-name-'.");
                 return LV_VFL_ERR_INVALID_FORMAT;
            }
        }

        // Next should be '[' or '|'
        state->p = skip_whitespace(state->p);
        if (*(state->p) != '[' && *(state->p) != '|') {
             LV_LOG_ERROR("VFL: Expected '[' or '|' after '-'.");
             return LV_VFL_ERR_INVALID_FORMAT;
        }
    }
    return LV_VFL_OK;
}

// --- Main VFL Apply Function ---
lv_vfl_result_t lv_vfl_apply(lv_obj_t *container,
                             const char *vfl_string,
                             const lv_vfl_view_map_t *view_map,
                             size_t view_map_count,
                             const lv_vfl_param_map_t *param_map,
                             size_t param_map_count)
{
    if (!container || !vfl_string || !view_map) {
        return LV_VFL_ERR_NULL_ARG;
    }

    lv_vfl_parser_state_t state = {
        .container = container,
        .p = vfl_string,
        .view_map = view_map,
        .view_map_count = view_map_count,
        .param_map = param_map,
        .param_map_count = param_map_count,
        .gap = LV_VFL_DEFAULT_GAP,
        .gap_set = false,
        .is_vertical = false,
        .start_edge = false,
        .end_edge = false,
        .grid_rows = 0,
        .grid_cols = 0,
        .grid_max_cols = 0,
    };

    lv_layout_t layout_type;

    // --- 1. Determine Layout Type & Initial Setup ---
    state.p = skip_whitespace(state.p);
    if (strncmp(state.p, "H:", 2) == 0) {
        layout_type = LV_LAYOUT_FLEX;
        state.is_vertical = false;
        state.p += 2;
        lv_obj_set_layout(container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
        LV_LOG_INFO("VFL: Using FLEX ROW layout.");
    } else if (strncmp(state.p, "V:", 2) == 0) {
        layout_type = LV_LAYOUT_FLEX;
        state.is_vertical = true;
        state.p += 2;
        lv_obj_set_layout(container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_grow(container, LV_FLEX_FLOW_COLUMN);
        LV_LOG_INFO("VFL: Using FLEX COLUMN layout.");
    } else if (strncmp(state.p, "G:", 2) == 0) {
        layout_type = LV_LAYOUT_GRID;
        state.p += 2;
        lv_obj_set_layout(container, LV_LAYOUT_GRID);
         LV_LOG_INFO("VFL: Using GRID layout.");
         // Grid requires 2 passes: 1 to find dims/gap, 2 to place items
    } else {
        LV_LOG_ERROR("VFL: String must start with 'H:', 'V:', or 'G:'.");
        return LV_VFL_ERR_INVALID_FORMAT;
    }

    // --- 2. First Pass (Common parsing, Grid dimensions) ---
    const char* p_start_pass1 = state.p; // Save start for grid's second pass
    uint16_t current_col_count = 0;
    state.grid_rows = 1; // Start with 1 row

    state.p = skip_whitespace(state.p);
    if (*state.p == '|') {
        state.start_edge = true; // Relevant for Flex, maybe Grid alignment later
        state.p++;
        state.p = skip_whitespace(state.p);
    }

    while (*state.p != '\0') {
        // --- 2.a. Handle Connection/Gap ---
        if (*state.p == '-') {
            lv_vfl_result_t res = parse_connection(&state);
            if (res != LV_VFL_OK) return res;
        } else if (current_col_count > 0 && *state.p != '[' && *state.p != '|' && *state.p != '\n') {
            // Implicit default gap between views? Or require '-'? Require '-' for clarity.
             LV_LOG_ERROR("VFL: Expected '-' connection between views.");
             return LV_VFL_ERR_INVALID_FORMAT;
        } else if (current_col_count == 0 && *state.p != '[' && !state.start_edge) {
             LV_LOG_ERROR("VFL: Format error. Expected '|' or '[' at start or after connection.");
             return LV_VFL_ERR_INVALID_FORMAT;
        }

        // --- 2.b. Handle View ---
        if (*state.p == '[') {
            const char* view_start_ptr = state.p; // For logging errors
            state.p++; // Skip '['
            const char *name_start = state.p;
            const char *name_end = NULL;

            while (*state.p != '\0' && *state.p != ']' && *state.p != '(') {
                state.p++;
            }
            name_end = state.p;
            size_t name_len = name_end - name_start;

            if (name_len == 0) {
                 LV_LOG_ERROR("VFL: Empty view name '[]'.");
                 return LV_VFL_ERR_INVALID_FORMAT;
            }

            // Find object (only needed in second pass for grid, but check validity now)
            lv_obj_t *view_obj = find_view_obj(name_start, name_len, view_map, view_map_count);
            if (!view_obj) {
                return LV_VFL_ERR_UNKNOWN_VIEW;
            }
             // Parent check
             if (lv_obj_get_parent(view_obj) != container) {
                 LV_LOG_WARN("VFL: View '%.*s' is not a direct child of the container.", (int)name_len, name_start);
             }


            // Parse Size Constraint - Store temporarily, apply in pass 2 for grid
            lv_vfl_parsed_size_t parsed_size;
            lv_vfl_result_t size_res = parse_size_constraint(&state, &parsed_size); // state.p advances past ')'
            if (size_res != LV_VFL_OK) return size_res;

            // Expect closing ']'
            state.p = skip_whitespace(state.p); // Allow space before ']'
            if (*state.p == ']') {
                state.p++; // Skip ']'
            } else {
                LV_LOG_ERROR("VFL: Missing closing ']' for view '%.*s'.", (int)name_len, name_start);
                return LV_VFL_ERR_INVALID_FORMAT;
            }

            // --- Apply constraints NOW for FLEXBOX ---
            if (layout_type == LV_LAYOUT_FLEX) {
                switch(parsed_size.type) {
                    case SIZE_TYPE_FIXED:
                    case SIZE_TYPE_PARAM: // Param value is already resolved to fixed/pct
                         if (state.is_vertical) lv_obj_set_height(view_obj, parsed_size.value);
                         else lv_obj_set_width(view_obj, parsed_size.value);
                         LV_LOG_INFO("VFL: Set %s of '%.*s' to %d (Fixed/Param)", state.is_vertical?"height":"width", (int)name_len, name_start, parsed_size.value);
                         break;
                    case SIZE_TYPE_PERCENT:
                         if (state.is_vertical) lv_obj_set_height(view_obj, LV_PCT(parsed_size.value));
                         else lv_obj_set_width(view_obj, LV_PCT(parsed_size.value));
                         LV_LOG_INFO("VFL: Set %s of '%.*s' to %d%% (Percent)", state.is_vertical?"height":"width", (int)name_len, name_start, parsed_size.value);
                         break;
                    case SIZE_TYPE_GROW:
                         lv_obj_set_flex_grow(view_obj, parsed_size.value);
                         // Ensure the dimension can grow (e.g., set base size to 0 or CONTENT?)
                         // Setting to 0 allows pure proportional growth.
                         if (state.is_vertical) lv_obj_set_height(view_obj, 0);
                         else lv_obj_set_width(view_obj, 0);
                         LV_LOG_INFO("VFL: Set flex grow of '%.*s' to %d", (int)name_len, name_start, parsed_size.value);
                         break;
                    case SIZE_TYPE_CONTENT:
                         // Ensure content size if no constraint specified
                         if (state.is_vertical) lv_obj_set_height(view_obj, LV_SIZE_CONTENT);
                         else lv_obj_set_width(view_obj, LV_SIZE_CONTENT);
                         break;
                    default: break; // Should not happen
                }
            } // End Flex apply

            current_col_count++;

        // --- 2.c. Handle End Edge ---
        } else if (*state.p == '|') {
            const char* next_non_space = skip_whitespace(state.p + 1);
            if (*next_non_space == '\0' || *next_non_space == '\n') { // End edge or end of grid row
                 state.end_edge = true; // Mark end for this row/flex line
                 state.p++;
                 // Continue if newline follows, else break/handle newline below
            } else {
                 LV_LOG_ERROR("VFL: Unexpected characters after trailing '|'.");
                 return LV_VFL_ERR_INVALID_FORMAT;
            }
        // --- 2.d. Handle Grid New Row ---
        } else if (*state.p == '\n' && layout_type == LV_LAYOUT_GRID) {
             state.p++; // Consume newline
             state.p = skip_whitespace(state.p); // Skip potential whitespace after newline

             // Update max columns seen so far
             if (current_col_count > state.grid_max_cols) {
                 state.grid_max_cols = current_col_count;
             }
             // Reset for next row
             current_col_count = 0;
             state.grid_rows++;
             state.start_edge = false; // Reset edge markers for the new row
             state.end_edge = false;
              if (*state.p == '|') { // Check for edge marker at start of new row
                 state.start_edge = true;
                 state.p++;
                 state.p = skip_whitespace(state.p);
             }
             if (*state.p == '\0') break; // End of string after newline
             continue; // Continue parsing next row

        } else {
             LV_LOG_ERROR("VFL: Unexpected character '%c' in format string.", *state.p);
             return LV_VFL_ERR_INVALID_FORMAT;
        }
        state.p = skip_whitespace(state.p);

    } // End while loop (Pass 1)

    // Final column count update for grid
    if (layout_type == LV_LAYOUT_GRID && current_col_count > state.grid_max_cols) {
         state.grid_max_cols = current_col_count;
    }

    // --- 3. Final Container & Item Configuration ---

    if (layout_type == LV_LAYOUT_FLEX) {
        // Set Gap
        if (state.is_vertical) {
            lv_obj_set_style_pad_row(container, state.gap, LV_PART_MAIN);
            lv_obj_set_style_pad_column(container, 0, LV_PART_MAIN);
        } else {
            lv_obj_set_style_pad_column(container, state.gap, LV_PART_MAIN);
            lv_obj_set_style_pad_row(container, 0, LV_PART_MAIN);
        }

        // Set Alignment
        lv_flex_align_t main_place = LV_FLEX_ALIGN_START;
        if (state.start_edge && state.end_edge) main_place = LV_FLEX_ALIGN_SPACE_BETWEEN;
        else if (state.start_edge) main_place = LV_FLEX_ALIGN_START;
        else if (state.end_edge) main_place = LV_FLEX_ALIGN_END;
        lv_obj_set_style_flex_main_place(container, main_place, LV_PART_MAIN);
        lv_obj_set_style_flex_cross_place(container, LV_FLEX_ALIGN_START, LV_PART_MAIN); // Default cross alignment
        lv_obj_set_style_flex_track_place(container, LV_FLEX_ALIGN_START, LV_PART_MAIN); // Default track alignment
        LV_LOG_INFO("VFL: Flex main place: %d, gap: %d", main_place, state.gap);

    } else if (layout_type == LV_LAYOUT_GRID) {
         // --- Grid Pass 2: Setup Grid Descriptors & Place Items ---
         if (state.grid_max_cols == 0 || state.grid_rows == 0) {
             LV_LOG_WARN("VFL: Grid has no items or rows.");
             return LV_VFL_OK; // Or error? Let's say OK for empty grid.
         }
         if (state.grid_max_cols > LV_VFL_MAX_GRID_COLS || state.grid_rows > LV_VFL_MAX_GRID_ROWS) {
             LV_LOG_ERROR("VFL: Grid dimensions (%dx%d) exceed maximum (%dx%d).",
                          state.grid_max_cols, state.grid_rows, LV_VFL_MAX_GRID_COLS, LV_VFL_MAX_GRID_ROWS);
             return LV_VFL_ERR_GRID_TOO_LARGE;
         }

         LV_LOG_INFO("VFL: Grid dimensions: %d rows, %d cols. Gap: %d", state.grid_rows, state.grid_max_cols, state.gap);

         // Allocate descriptor arrays (LVGL makes copies, so stack is fine)
         lv_coord_t col_dsc[LV_VFL_MAX_GRID_COLS];
         lv_coord_t row_dsc[LV_VFL_MAX_GRID_ROWS];

         // Initialize tracks (Using CONTENT allows natural sizing)
         for(uint16_t i = 0; i < state.grid_max_cols; ++i) col_dsc[i] = LV_GRID_CONTENT;
         for(uint16_t i = 0; i < state.grid_rows; ++i) row_dsc[i] = LV_GRID_CONTENT;
         // Add terminating values for LVGL arrays
         //col_dsc[state.grid_max_cols] = LV_GRID_TEMPLATE_LAST; // Not needed, lv_obj_set_style_... takes size
         //row_dsc[state.grid_rows] = LV_GRID_TEMPLATE_LAST;

         // Set grid template and gaps
         lv_obj_set_style_grid_column_dsc_array(container, col_dsc, state.grid_max_cols);
         lv_obj_set_style_grid_row_dsc_array(container, row_dsc, state.grid_rows);
         lv_obj_set_style_pad_column(container, state.gap, LV_PART_MAIN);
         lv_obj_set_style_pad_row(container, state.gap, LV_PART_MAIN);


         // --- Grid Pass 2: Re-parse to place items ---
         state.p = p_start_pass1; // Reset parser to after G:
         uint16_t current_row_idx = 0;
         uint16_t current_col_idx = 0;

         state.p = skip_whitespace(state.p);
         if (*state.p == '|') { state.p++; state.p = skip_whitespace(state.p); } // Skip initial edge

         while (*state.p != '\0' && current_row_idx < state.grid_rows) {
             // Handle Connection (just advances pointer, gap already set)
             if (*state.p == '-') {
                 const char* p_before_conn = state.p;
                 lv_vfl_result_t conn_res = parse_connection(&state);
                 if (conn_res < 0 && conn_res != LV_VFL_ERR_UNKNOWN_PARAM) { // Allow unknown param error here as gap is already set
                      LV_LOG_ERROR("VFL: Grid Pass 2: Error parsing connection after '%.*s'.", (int)(p_before_conn - p_start_pass1), p_start_pass1);
                      return conn_res; // Serious format error
                 } else if (conn_res == LV_VFL_ERR_UNKNOWN_PARAM) {
                      LV_LOG_WARN("VFL: Grid Pass 2: Ignoring unknown gap parameter, using previously set gap: %d", state.gap);
                 }
             }

             // Handle View
             if (*state.p == '[') {
                 if (current_col_idx >= state.grid_max_cols) {
                      LV_LOG_ERROR("VFL: Grid row %d has more items than max columns (%d).", current_row_idx + 1, state.grid_max_cols);
                      return LV_VFL_ERR_INVALID_FORMAT;
                 }
                 state.p++; // Skip '['
                 const char *name_start = state.p;
                 const char *name_end = NULL;
                 while (*state.p != '\0' && *state.p != ']' && *state.p != '(') { state.p++; }
                 name_end = state.p;
                 size_t name_len = name_end - name_start;

                 // Find object (should succeed as checked in pass 1)
                 lv_obj_t *view_obj = find_view_obj(name_start, name_len, view_map, view_map_count);
                 if (!view_obj) return LV_VFL_ERR_UNKNOWN_VIEW; // Should not happen normally

                 // Parse Size Constraint again
                 lv_vfl_parsed_size_t parsed_size;
                 lv_vfl_result_t size_res = parse_size_constraint(&state, &parsed_size); // state.p advances past ')'
                 if (size_res != LV_VFL_OK && size_res != LV_VFL_ERR_UNKNOWN_PARAM) {
                    LV_LOG_ERROR("VFL: Grid Pass 2: Error parsing size for '%.*s'.", (int)name_len, name_start);
                    return size_res;
                 } else if (size_res == LV_VFL_ERR_UNKNOWN_PARAM) {
                      LV_LOG_WARN("VFL: Grid Pass 2: Ignoring size for '%.*s' due to unknown parameter.", (int)name_len, name_start);
                      parsed_size.type = SIZE_TYPE_CONTENT; // Default to content size
                 }


                 // Apply size constraint (Width only for grid items in this simple version)
                 switch(parsed_size.type) {
                    case SIZE_TYPE_FIXED:
                    case SIZE_TYPE_PARAM:
                         lv_obj_set_width(view_obj, parsed_size.value);
                         lv_obj_set_height(view_obj, LV_SIZE_CONTENT); // Default height
                         LV_LOG_INFO("VFL Grid: Set width of '%.*s' to %d (Fixed/Param)", (int)name_len, name_start, parsed_size.value);
                         break;
                    case SIZE_TYPE_PERCENT:
                         lv_obj_set_width(view_obj, LV_PCT(parsed_size.value));
                         lv_obj_set_height(view_obj, LV_SIZE_CONTENT); // Default height
                          LV_LOG_INFO("VFL Grid: Set width of '%.*s' to %d%% (Percent)", (int)name_len, name_start, parsed_size.value);
                         break;
                    case SIZE_TYPE_GROW: // Flex grow ignored in grid
                          LV_LOG_WARN("VFL Grid: Flex grow '(:%d)' ignored for view '%.*s' in Grid layout.", parsed_size.value, (int)name_len, name_start);
                           lv_obj_set_size(view_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Default size
                         break;
                    case SIZE_TYPE_CONTENT:
                    default:
                        lv_obj_set_size(view_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Default size
                         break;
                 }


                 // Place item in grid (default: stretch, 1x1 span)
                 lv_obj_set_grid_cell(view_obj, LV_GRID_ALIGN_STRETCH, current_col_idx, 1,
                                      LV_GRID_ALIGN_STRETCH, current_row_idx, 1);
                 LV_LOG_INFO("VFL Grid: Placed '%.*s' at row %d, col %d", (int)name_len, name_start, current_row_idx, current_col_idx);

                 current_col_idx++;

                 // Expect closing ']'
                 state.p = skip_whitespace(state.p);
                 if (*state.p == ']') {
                    state.p++;
                 } else {
                     LV_LOG_ERROR("VFL: Grid Pass 2: Missing closing ']' for view '%.*s'.", (int)name_len, name_start);
                     return LV_VFL_ERR_INVALID_FORMAT;
                 }

             // Handle End Edge
             } else if (*state.p == '|') {
                 state.p++; // Consume '|'
                 // Alignment hints could be processed here in future
             // Handle New Row
             } else if (*state.p == '\n') {
                 state.p++; // Consume newline
                 current_row_idx++;
                 current_col_idx = 0;
                 state.p = skip_whitespace(state.p);
                 if (*state.p == '|') { state.p++; state.p = skip_whitespace(state.p); } // Skip edge marker on new row
             } else if (*state.p != '\0') {
                 // If not '[', '|', '\n', or '-', it's an error after the first element
                  if (current_col_idx > 0 || *(skip_whitespace(p_start_pass1)) == '[') { // Avoid error if string *starts* unexpectedly
                     LV_LOG_ERROR("VFL: Grid Pass 2: Unexpected character '%c'", *state.p);
                     return LV_VFL_ERR_INVALID_FORMAT;
                  } else {
                      // Allow empty lines maybe? Or just break.
                      break;
                  }
             }
             state.p = skip_whitespace(state.p);

         } // End Grid Pass 2 while loop

    } // End if Grid Layout


    // --- 4. Final Invalidation ---
    lv_obj_invalidate(container);
    // lv_obj_refr_pos(container); // Maybe not needed, invalidate should trigger recalc

    LV_LOG_INFO("VFL: Successfully applied format string.");
    return LV_VFL_OK;
}