// lv_cnc_io_panel.h — CNC I/O Control & Monitor panel widget
//
// Controller-agnostic I/O display driven by the generic mc_io_channel_t model.
// Works with RRF, GRBL, FlexiHAL, Mach4, etc.  Optimised for 480×320 touch.
//
// ┌──────────────────────────────────────────────────────────────────────┐
// │  [INS]  [OUTS]  [SENS]  [ACT]              ← tab bar 34 px          │
// ├──────────────────────────────────────────────────────────────────────┤
// │ Name              │ Status / Control                                 │
// │───────────────────┼──────────────────────────────────────────────    │
// │ X Limit           │  ● OK                          (INS)            │
// │ Z-Probe           │  ○ TRIG   0.97                 (INS)            │
// │ Coolant           │                        [OFF]   (OUTS)           │
// │ T0 thermistor     │  ● 23.5 °C                     (SENS)           │
// │ Fan 0             │  [████░░░] 75%  [ON]           (ACT)            │
// │ Bed heater        │  ● 23.5 °C → 60 °C    [ON]    (ACT)            │
// └──────────────────────────────────────────────────────────────────────┘
//
// Categories (io_category_t):
//   IO_CAT_INPUTS    — DIR_INPUT  + SIG_DIGITAL: limits, probes, e-stop, gpIn
//   IO_CAT_OUTPUTS   — DIR_OUTPUT + SIG_DIGITAL: coolant, ATC, gpOut
//   IO_CAT_SENSORS   — DIR_INPUT  + SIG_ANALOG:  temperature sensors, 0-10V
//   IO_CAT_ACTUATORS — DIR_OUTPUT + SIG_ANALOG:  fans (PWM), heaters, spindle
//
// G-code issued on user interaction (RRF default, role-based):
//   MC_IO_ROLE_FAN:     M106 P{n} S{0..255} / M107 P{n}
//   MC_IO_ROLE_HEATER:  M140 S{temp} (bed)  / M104 T{n} S{temp} (tool)
//   MC_IO_ROLE_SPINDLE: M3 S{rpm} / M5
//   MC_IO_ROLE_COOLANT: M8 / M9
//   others:             M42 P{src_idx} S{0|1}
//
// Integration:
//   1. Call lv_cnc_io_panel_create(parent, machine) once when building UI.
//   2. Call lv_cnc_io_panel_refresh(panel) from the UI thread whenever the
//      machine's sensors_changed callback fires (UI_DIRTY_IO_SENSORS).
//   3. Call lv_cnc_io_panel_destroy(panel) to release all resources.
//
#ifndef LV_CNC_IO_PANEL_H
#define LV_CNC_IO_PANEL_H

#include "lvgl.h"
#include "ui/interface.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Create the I/O control panel as a child of @p parent.
 *
 * The panel fills @p parent.  All RRF I/O categories are placed in a
 * tab-bar at the top; the active category's items are listed below.
 *
 * @param parent   LVGL parent object (should be full-screen or a tile).
 * @return Root lv_obj_t* of the widget, or NULL on failure.
 */
lv_obj_t *lv_cnc_io_panel_create(lv_obj_t *parent);

/**
 * @brief Re-read all I/O state from @p machine and update every row.
 *
 * Call this from the UI thread whenever sensors_changed fires.  Safe to call
 * even if no category has changed; it only redraws values, not layout, unless
 * the number of items in the active category has changed.
 *
 * @param panel     Root object returned by lv_cnc_io_panel_create().
 * @param interface The UI interface_t singleton.
 */
void lv_cnc_io_panel_refresh(lv_obj_t *panel, interface_t *interface);

/**
 * @brief Destroy the panel and free all associated resources.
 *
 * Equivalent to lv_obj_del(panel); the private state is cleaned up via the
 * LV_EVENT_DELETE callback registered at creation time.
 *
 * @param panel  Root object returned by lv_cnc_io_panel_create(), or NULL.
 */
void lv_cnc_io_panel_destroy(lv_obj_t *panel);

// ---------------------------------------------------------------------------
// Optional: programmatic tab selection
// ---------------------------------------------------------------------------

typedef enum {
  IO_CAT_INPUTS    = 0,  ///< Digital inputs: limits, probes, e-stop, door, gpIn …
  IO_CAT_OUTPUTS   = 1,  ///< Digital outputs: coolant relay, ATC, status LED, gpOut …
  IO_CAT_SENSORS   = 2,  ///< Analog inputs: temperature sensors, 0-10 V feedback …
  IO_CAT_ACTUATORS = 3,  ///< Analog outputs: fans (PWM), heaters, spindle, laser …
  IO_CAT__COUNT
} io_category_t;

/**
 * @brief Switch the visible category tab.
 *
 * @param panel  Root object returned by lv_cnc_io_panel_create().
 * @param cat    Category to activate.
 */
void lv_cnc_io_panel_set_category(lv_obj_t *panel, io_category_t cat);

#ifdef __cplusplus
}
#endif

#endif  // LV_CNC_IO_PANEL_H
