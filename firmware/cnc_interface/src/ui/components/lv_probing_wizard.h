#ifndef LV_PROBING_WIZARD_H
#define LV_PROBING_WIZARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// --- Custom Types ---

/**
 * @brief A structure to hold floating-point coordinates, replacing the removed lv_point_float_t from LVGL v8.
 */
typedef struct {
    float x;
    float y;
} lv_probing_wizard_point_float_t;


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
 * @brief Defines the current phase of the probing process.
 */
typedef enum {
    LV_PROBING_WIZARD_PHASE_SETUP,   /**< User is manually setting up initial points. */
    LV_PROBING_WIZARD_PHASE_PROBING, /**< Machine is performing automated probing moves. */
} lv_probing_wizard_phase_t;

/**
 * @brief Specifies which corner to probe in corner probing mode.
 *        Directions correspond to probe travel direction from a safe starting point.
 */
typedef enum {
    LV_PROBING_CORNER_FRONT_LEFT,  /**< Lower-left corner in typical XY plane (-X, -Y probe moves). */
    LV_PROBING_CORNER_FRONT_RIGHT, /**< Lower-right corner (+X, -Y probe moves). */
    LV_PROBING_CORNER_BACK_LEFT,   /**< Upper-left corner (-X, +Y probe moves). */
    LV_PROBING_CORNER_BACK_RIGHT,  /**< Upper-right corner (+X, +Y probe moves). */
} lv_probing_wizard_corner_t;


// --- Public Functions ---

/**
 * @brief Create a new probing wizard widget.
 * @param parent The parent object for the new widget.
 * @return A pointer to the created widget object.
 */
lv_obj_t * lv_probing_wizard_create(lv_obj_t * parent);

/**
 * @brief Set the probing mode and variant (inside/outside).
 * @param obj Pointer to the probing wizard object.
 * @param mode The probing mode (rectangle, circle, corner).
 * @param is_inside true for an internal feature (pocket, bore), false for an external feature (block, boss).
 */
void lv_probing_wizard_set_mode(lv_obj_t * obj, lv_probing_wizard_mode_t mode, bool is_inside);

/**
 * @brief Set the current phase of the probing process.
 * @param obj Pointer to the probing wizard object.
 * @param phase The phase (setup or probing).
 */
void lv_probing_wizard_set_phase(lv_obj_t * obj, lv_probing_wizard_phase_t phase);

/**
 * @brief Set the corner type for corner probing mode.
 * @param obj Pointer to the probing wizard object.
 * @param corner The specific corner to be probed.
 */
void lv_probing_wizard_set_corner_type(lv_obj_t * obj, lv_probing_wizard_corner_t corner);

/**
 * @brief Highlight the currently active step in the process.
 * @param obj Pointer to the probing wizard object.
 * @param step_index The 0-based index of the step to highlight.
 */
void lv_probing_wizard_set_active_step(lv_obj_t * obj, uint8_t step_index);

/**
 * @brief Set the measured coordinates for a completed probe point.
 * This also marks the point as 'done'.
 * @param obj Pointer to the probing wizard object.
 * @param point_index The 0-based index of the point.
 * @param x The measured X coordinate.
 * @param y The measured Y coordinate.
 */
void lv_probing_wizard_set_point_value(lv_obj_t * obj, uint8_t point_index, float x, float y);

/**
 * @brief Reset all probed points to their initial state.
 * @param obj Pointer to the probing wizard object.
 */
void lv_probing_wizard_clear_points(lv_obj_t * obj);

/**
 * @brief Calculate and retrieve the final result (e.g., center point).
 * @param obj Pointer to the probing wizard object.
 * @return A lv_probing_wizard_point_float_t containing the calculated center coordinates. Returns {0,0} if not all points are set.
 */
lv_probing_wizard_point_float_t lv_probing_wizard_get_result(lv_obj_t * obj);


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PROBING_WIZARD_H*/