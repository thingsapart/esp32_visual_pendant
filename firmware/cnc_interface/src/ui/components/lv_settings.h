// lv_settings.h — LVGL settings panel widget (lv_menu-based)
//
// Provides a two-panel settings UI built on lv_menu:
//   sidebar (left)  — group selector list; each entry carries an icon and
//                     navigates to that group's detail page on the right.
//   main    (right) — per-group pages with one row per setting:
//                       • numeric (INT32/FLOAT): name label (left) + value
//                         (right-aligned) on the top row, slider below.
//                       • bool: name label + lv_switch on a single row.
//                     A special "Reload Defaults" group contains one entry
//                     that calls app_settings_reset_all() on click.
//
// The main-panel header/back bar is hidden; navigation is sidebar-only.
//
// Encoder support
// ---------------
// When the pendant encoder is in "UI mode" (jog axis = OFF) and the settings
// screen is visible, call lv_settings_encoder_input() with the encoder diff
// each tick.  This adjusts the currently highlighted setting value directly.
//
// Typical integration in pendant.cpp:
//
//   extern lv_obj_t *g_settings_ui;   // created once at setup
//
//   // In the encoder tick / pendant tick:
//   if (settings_screen_is_active && encoder.isUiMode()) {
//       int diff = encoder.readAndReset();
//       lv_settings_encoder_input(g_settings_ui, diff);
//   } else if (!encoder.isUiMode()) {
//       int diff = encoder.readAndReset();
//       jog_accumulator_post_clicks(diff);
//   }

#ifndef LV_SETTINGS_H
#define LV_SETTINGS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Create the settings widget as a child of @p parent.
 *
 * The widget fills @p parent.  Call lv_settings_refresh() to reload values
 * from app_settings after external changes.
 *
 * @param parent    LVGL parent object
 * @return          Root lv_obj_t* of the widget, or NULL on failure
 */
lv_obj_t *lv_settings_create(lv_obj_t *parent);

/**
 * @brief Reload all displayed values from app_settings into the UI rows.
 *
 * Useful after an external settings change (e.g., reset-to-defaults).
 */
void lv_settings_refresh(lv_obj_t *obj);

// ---------------------------------------------------------------------------
// Encoder integration
// ---------------------------------------------------------------------------

/**
 * @brief Feed an encoder delta into the settings widget.
 *
 * Positive delta = increment focused setting; negative = decrement.
 * If no setting row is currently highlighted this scrolls the right panel.
 *
 * Call this instead of (or before) routing encoder diffs to LVGL's indev
 * when the settings screen is the active view.
 *
 * @param obj   Root object returned by lv_settings_create()
 * @param diff  Signed encoder step count (e.g. from encoder.readAndReset())
 */
void lv_settings_encoder_input(lv_obj_t *obj, int32_t diff);

/**
 * @brief Returns true when a setting value row is highlighted (selected for
 *        encoder-driven editing).
 *
 * When this returns true the encoder diffs should go to
 * lv_settings_encoder_input(); when false they can safely be used for other
 * purposes (or also sent here to scroll the list).
 */
bool lv_settings_has_encoder_focus(lv_obj_t *obj);

/**
 * @brief Move encoder highlight to the next / previous setting row.
 *
 * dir > 0 → next; dir < 0 → previous; dir == 0 → no-op.
 * Call this when you want coarse navigation (e.g. group-select button).
 */
void lv_settings_encoder_navigate(lv_obj_t *obj, int32_t dir);

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

/**
 * @brief Called each time a setting value is changed through the UI.
 *
 * @param group  The settings group of the changed value
 * @param key    The key within the group
 * @param val    New value (already applied to app_settings in-RAM cache)
 */
typedef void (*lv_settings_changed_cb_t)(int group, int key,
                                          void *user_data);

/**
 * @brief Register a callback that fires whenever a value changes.
 *
 * Only one callback is supported; pass NULL to clear.
 */
void lv_settings_set_changed_cb(lv_obj_t *obj,
                                  lv_settings_changed_cb_t cb,
                                  void *user_data);

#ifdef __cplusplus
}
#endif

#endif // LV_SETTINGS_H
