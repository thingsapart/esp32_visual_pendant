#ifndef LV_PROBING_WIZARD_H
#define LV_PROBING_WIZARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// --- Custom Types ---

/**
 * @brief A structure to hold floating-point coordinates.
 */
typedef struct {
    float x;
    float y;
} lv_probing_wizard_point_float_t;

/**
 * @brief A structure to hold a point and its status.
 */
typedef struct {
    float x;
    float y;
    bool is_set;
} probe_point_t;


// --- Enums for configuration ---

/**
 * @brief Defines the geometric shape to be probed.
 */
typedef enum {
    LV_PROBING_WIZARD_MODE_RECTANGLE,
    LV_PROBING_WIZARD_MODE_CIRCLE,
    LV_PROBING_WIZARD_MODE_CORNER,
} lv_probing_wizard_mode_t;

/**
 * @brief Specifies which corner to probe in corner probing mode.
 */
typedef enum {
    LV_PROBING_CORNER_NONE = 0,
    LV_PROBING_CORNER_FRONT_LEFT,
    LV_PROBING_CORNER_FRONT_RIGHT,
    LV_PROBING_CORNER_BACK_LEFT,
    LV_PROBING_CORNER_BACK_RIGHT,
} lv_probing_wizard_corner_t;

/**
 * @brief A bitmask to specify which parts of the canvas visualization should be highlighted.
 */
typedef enum {
    HIGHLIGHT_NONE          = 0,
    HIGHLIGHT_CORNER_BL     = 1 << 0,
    HIGHLIGHT_CORNER_BR     = 1 << 1,
    HIGHLIGHT_CORNER_FL     = 1 << 2,
    HIGHLIGHT_CORNER_FR     = 1 << 3,
    HIGHLIGHT_PROBE_POINT_0 = 1 << 4,
    HIGHLIGHT_PROBE_POINT_1 = 1 << 5,
    HIGHLIGHT_PROBE_POINT_2 = 1 << 6,
    HIGHLIGHT_PROBE_POINT_3 = 1 << 7,
    HIGHLIGHT_OUTLINE       = 1 << 8,
    HIGHLIGHT_CENTER        = 1 << 9,
    HIGHLIGHT_Z_PROBE       = 1 << 10,
} highlight_part_t;

/**
 * @brief Defines the type of action for a given step in the probing routine.
 */
typedef enum {
    ACTION_AWAIT_START,     /**< Initial state. Shows "Start Probing" button. */
    ACTION_JOG_AND_CONFIRM, /**< User jogs to a position and clicks a confirm button. */
    ACTION_SELECT_CORNER,   /**< User clicks on a corner on the canvas to select it. */
    ACTION_PROBE_POINT,     /**< Triggers an automated XY probing move. */
    ACTION_PROBE_Z_TOP,     /**< Triggers an automated Z probing move to find the workpiece top. */
    ACTION_MESSAGE,         /**< Displays an informational message, waits for user to click "Next". */
    ACTION_COMPLETE,        /**< The final step of the routine. */
} lv_probing_action_type_t;

/**
 * @brief A structure defining a single step in a probing routine.
 */
typedef struct {
    const char * instruction_text;
    uint32_t highlight_mask;
    lv_probing_action_type_t type;
    union {
        uint8_t setup_point_index; /**< For ACTION_JOG_AND_CONFIRM */
        uint8_t probe_index;       /**< For ACTION_PROBE_POINT */
    } param;
} lv_probing_action_t;

// --- Callbacks ---

/**
 * @brief Callback for the wizard to get the current machine position.
 * @return The current X and Y coordinates.
 */
typedef lv_probing_wizard_point_float_t (*get_current_jogged_position_cb_t)(void);

/**
 * @brief Callback for the wizard to command the machine to execute a probe cycle.
 * @param wizard_obj Pointer to the wizard object, containing all context (setup points, etc.).
 * @param action Pointer to the current action being executed.
 */
typedef void (*execute_probe_cb_t)(lv_obj_t * wizard_obj, const lv_probing_action_t * action);

/**
 * @brief Callback for the wizard to command the machine to set a new Work Coordinate System origin.
 * @param wizard_obj Pointer to the wizard object.
 * @param wcs_index The WCS to set (e.g., 2 for G55, 3 for G56, etc.).
 * @param x The new X origin.
 * @param y The new Y origin.
 * @param z The new Z origin.
 * @param apply_z A boolean indicating if the Z origin should be set.
 */
typedef void (*set_wcs_origin_cb_t)(lv_obj_t * wizard_obj, uint8_t wcs_index, float x, float y, float z, bool apply_z);


// --- Public Functions ---

/**
 * @brief Create a new probing wizard widget.
 * @param parent The parent object for the new widget.
 * @return A pointer to the created widget object.
 */
lv_obj_t * lv_probing_wizard_create(lv_obj_t * parent);

/**
 * @brief Set the probing mode and variant (inside/outside). This will reset any current progress.
 * @param obj Pointer to the probing wizard object.
 * @param mode The probing mode (rectangle, circle, corner).
 * @param is_inside true for an internal feature (pocket, bore), false for an external feature (block, boss).
 */
void lv_probing_wizard_set_mode(lv_obj_t * obj, lv_probing_wizard_mode_t mode, bool is_inside);

/**
 * @brief Set the corner type for corner probing mode.
 * @param obj Pointer to the probing wizard object.
 * @param corner The specific corner to be probed.
 */
void lv_probing_wizard_set_corner_type(lv_obj_t * obj, lv_probing_wizard_corner_t corner);

/**
 * @brief Manually set the active step of the wizard. Use with caution.
 * @param obj Pointer to the probing wizard object.
 * @param step_index The 0-based index of the step to set.
 */
void lv_probing_wizard_set_active_step(lv_obj_t * obj, int8_t step_index);

/**
 * @brief Register the necessary machine handler callbacks with the wizard.
 * @param obj Pointer to the probing wizard object.
 * @param get_pos_cb Function to get current machine position.
 * @param exec_probe_cb Function to execute a probe cycle.
 * @param set_wcs_cb Function to set a new WCS origin.
 */
void lv_probing_wizard_register_callbacks(lv_obj_t * obj, get_current_jogged_position_cb_t get_pos_cb, execute_probe_cb_t exec_probe_cb, set_wcs_origin_cb_t set_wcs_cb);

/**
 * @brief Sets the connection status of the machine, updating the UI accordingly.
 * @param obj Pointer to the probing wizard object.
 * @param connected True if the machine is connected, false otherwise.
 */
void lv_probing_wizard_set_connected(lv_obj_t * obj, bool connected);

/**
 * @brief Function for the machine handler to call after a probe is complete to report the result.
 * @param obj Pointer to the probing wizard object.
 * @param probe_index The index of the probe point that was measured.
 * @param x The measured X coordinate.
 * @param y The measured Y coordinate.
 */
void lv_probing_wizard_report_probe_result(lv_obj_t * obj, uint8_t probe_index, float x, float y);

/**
 * @brief Function for the machine handler to call after a full routine (e.g. circle find) to report the final result.
 * This sets the final calculated result and marks it as valid.
 * @param obj Pointer to the probing wizard object.
 * @param x The final calculated X coordinate.
 * @param y The final calculated Y coordinate.
 */
void lv_probing_wizard_report_final_result(lv_obj_t * obj, float x, float y);

/**
 * @brief Function for the machine handler to call to set the measured Z-top of the workpiece.
 * @param obj Pointer to the probing wizard object.
 * @param z_top The measured Z coordinate.
 */
void lv_probing_wizard_set_z_top(lv_obj_t * obj, float z_top);

/**
 * @brief Advances the wizard to the next step. Typically called by the machine handler after completing an action.
 * @param obj Pointer to the probing wizard object.
 */
void lv_probing_wizard_advance_step(lv_obj_t * obj);

/**
 * @brief Gets the array of setup points that have been confirmed by the user.
 * @param obj Pointer to the probing wizard object.
 * @return A const pointer to the array of setup points.
 */
const probe_point_t * lv_probing_wizard_get_setup_points(lv_obj_t * obj);

/**
 * @brief Gets the measured Z-top value.
 * @param obj Pointer to the probing wizard object.
 * @return The Z-top coordinate. Returns 0 if not set.
 */
float lv_probing_wizard_get_z_top(lv_obj_t * obj);

/**
 * @brief Retrieves the final calculated result.
 * @param obj Pointer to the probing wizard object.
 * @return A point containing the calculated center coordinates. Returns {0,0} if result is not yet valid.
 */
lv_probing_wizard_point_float_t lv_probing_wizard_get_result(lv_obj_t * obj);

/**
 * @brief Gets the current probing mode.
 * @param obj Pointer to the probing wizard object.
 * @return The current lv_probing_wizard_mode_t.
 */
lv_probing_wizard_mode_t lv_probing_wizard_get_mode(lv_obj_t * obj);

/**
 * @brief Gets the current probing variant (inside/outside).
 * @param obj Pointer to the probing wizard object.
 * @return True if probing an internal feature, false otherwise.
 */
bool lv_probing_wizard_get_is_inside(lv_obj_t * obj);

/**
 * @brief Gets the currently selected corner for corner probing.
 * @param obj Pointer to the probing wizard object.
 * @return The currently selected lv_probing_wizard_corner_t.
 */
lv_probing_wizard_corner_t lv_probing_wizard_get_corner_type(lv_obj_t * obj);


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PROBING_WIZARD_H*/
