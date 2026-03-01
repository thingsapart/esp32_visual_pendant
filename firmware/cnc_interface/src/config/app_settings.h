// app_settings.h — Declarative, NVS-backed application settings
//
// All user-visible settings are declared here in a two-level namespace:
//   (group, key)  e.g.  APP_SETTINGS_GROUP_JOG + APP_SETTINGS_JOG_FEED_XY
//
// Groups and their keys are both sentinel-terminated enums so that code can
// iterate over them without hard-coding counts.
//
// NVS schema version (bump when structs change incompatibly):
#define APP_SETTINGS_SCHEMA_VERSION 1

#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Value types
// ---------------------------------------------------------------------------

typedef enum {
    APP_SETTING_TYPE_FLOAT = 0,
    APP_SETTING_TYPE_INT32,
    APP_SETTING_TYPE_BOOL,
} app_setting_type_t;

typedef union {
    float   f;
    int32_t i;
    bool    b;
} app_setting_val_t;

// ---------------------------------------------------------------------------
// Setting descriptor — one per key
// ---------------------------------------------------------------------------

typedef struct {
    const char         *name;     ///< Human-readable name shown in settings UI
    const char         *unit;     ///< Unit string ("mm/min", "mm/s²", …) or ""
    app_setting_type_t  type;
    app_setting_val_t   def;      ///< Compile-time default (also NVS fallback)
    app_setting_val_t   min;      ///< Minimum value (for FLOAT/INT32)
    app_setting_val_t   max;      ///< Maximum value (for FLOAT/INT32)
    app_setting_val_t   step;     ///< Encoder increment step (for FLOAT/INT32)
} app_setting_def_t;

// ---------------------------------------------------------------------------
// Group enum — add new groups before APP_SETTINGS_GROUP__COUNT
// ---------------------------------------------------------------------------

typedef enum {
    APP_SETTINGS_GROUP_JOG      = 0,  ///< Jog feed rates, accelerations, timing
    APP_SETTINGS_GROUP_CAM,           ///< Camera fallback grid calibration
    APP_SETTINGS_GROUP_MACHINE,       ///< Machine polling intervals
    APP_SETTINGS_GROUP_TIMING,        ///< Hub/camera network timing
    APP_SETTINGS_GROUP_PROBE_UI,      ///< Probe UI geometry parameters
    APP_SETTINGS_GROUP__COUNT         ///< Sentinel — always last
} app_settings_group_t;

// ---------------------------------------------------------------------------
// Per-group key enums — always end with APP_SETTINGS_XXX__COUNT
// ---------------------------------------------------------------------------

// --- Jog group ---
typedef enum {
    APP_SETTINGS_JOG_FEED_XY = 0, ///< XY jog feed rate (mm/min)
    APP_SETTINGS_JOG_FEED_Z,      ///< Z  jog feed rate (mm/min)
    APP_SETTINGS_JOG_ACCEL_X,     ///< X  acceleration  (mm/s²)
    APP_SETTINGS_JOG_ACCEL_Y,     ///< Y  acceleration  (mm/s²)
    APP_SETTINGS_JOG_ACCEL_Z,     ///< Z  acceleration  (mm/s²)
    APP_SETTINGS_JOG_LEAD_AHEAD_MS,///< Wake-up lead-ahead before move ends (ms)
    APP_SETTINGS_JOG_MIN_SLEEP_MS, ///< Minimum post-move sleep (ms)
    APP_SETTINGS_JOG_MAX_SLEEP_MS, ///< Maximum post-move sleep / safety cap (ms)
    APP_SETTINGS_JOG__COUNT        ///< Sentinel
} app_settings_jog_key_t;

// --- Camera group ---
typedef enum {
    APP_SETTINGS_CAM_FALLBACK_GRID_DX = 0, ///< Fallback grid cell spacing X (mm) — used when cam is not connected
    APP_SETTINGS_CAM_FALLBACK_GRID_DY,     ///< Fallback grid cell spacing Y (mm)
    APP_SETTINGS_CAM_FALLBACK_GRID_NX,     ///< Fallback grid columns
    APP_SETTINGS_CAM_FALLBACK_GRID_NY,     ///< Fallback grid rows
    APP_SETTINGS_CAM__COUNT                ///< Sentinel
} app_settings_cam_key_t;

// --- Machine group ---
typedef enum {
    APP_SETTINGS_MACHINE_SEND_INTERVAL_MS = 0, ///< G-code send interval (ms)
    APP_SETTINGS_MACHINE_POLL_NTH_INTERVAL,    ///< Poll every Nth send interval
    APP_SETTINGS_MACHINE__COUNT                ///< Sentinel
} app_settings_machine_key_t;

// --- Hub/camera timing group ---
typedef enum {
    APP_SETTINGS_TIMING_HUB_POLL_MS = 0,   ///< Hub loop poll interval (ms)
    APP_SETTINGS_TIMING_FULL_STATE_N,      ///< Full-state push every Nth poll
    APP_SETTINGS_TIMING_LOG_COALESCE_MS,   ///< Log batch flush window (ms)
    APP_SETTINGS_TIMING_CAM_FRAME_TIMEOUT_MS,  ///< Camera frame stale threshold (ms)
    APP_SETTINGS_TIMING_CAM_STATUS_TIMEOUT_MS, ///< Camera heartbeat stale threshold (ms)
    APP_SETTINGS_TIMING__COUNT             ///< Sentinel
} app_settings_timing_key_t;

// --- Probe UI geometry group ---
typedef enum {
    APP_SETTINGS_PROBE_UI_TIP_RADIUS = 0,  ///< Probe tip radius (mm)
    APP_SETTINGS_PROBE_UI_BACKOFF_MULT,    ///< XY backoff = tip_radius * multiplier
    APP_SETTINGS_PROBE_UI__COUNT           ///< Sentinel
} app_settings_probe_ui_key_t;

// Max key count across all groups — used for statically-sized storage.
// Set generously so adding new keys never requires bumping this.
#define APP_SETTINGS_MAX_KEYS_PER_GROUP  16

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Initialise the module.
 *
 * Applies compile-time defaults to the in-RAM cache.  Must be called before
 * any get/set.  Does NOT touch NVS.
 */
void app_settings_init(void);

/**
 * @brief Load all groups from NVS.
 *
 * Falls back to compile-time defaults for any group whose NVS blob is absent
 * or version-mismatched.  Returns true if every group loaded successfully.
 */
bool app_settings_load(void);

/**
 * @brief Save a single group to NVS.  Returns true on success.
 */
bool app_settings_save_group(app_settings_group_t group);

// --- Typed getters ---
float   app_settings_get_float(app_settings_group_t g, int key);
int32_t app_settings_get_int  (app_settings_group_t g, int key);
bool    app_settings_get_bool (app_settings_group_t g, int key);

// --- Typed setters (in-RAM only; call app_settings_save_group to persist) ---
bool    app_settings_set_float(app_settings_group_t g, int key, float   v);
bool    app_settings_set_int  (app_settings_group_t g, int key, int32_t v);
bool    app_settings_set_bool (app_settings_group_t g, int key, bool    v);

// --- Generic value access (used by the settings UI) ---
app_setting_val_t        app_settings_get_val(app_settings_group_t g, int key);
bool                     app_settings_set_val(app_settings_group_t g, int key,
                                              app_setting_val_t v);

// --- Metadata accessors (used by lv_settings to build the UI) ---
int                      app_settings_group_count(void);   ///< == APP_SETTINGS_GROUP__COUNT
const char              *app_settings_group_name(app_settings_group_t g);
int                      app_settings_key_count(app_settings_group_t g);
const app_setting_def_t *app_settings_get_def(app_settings_group_t g, int key);

/**
 * @brief Reset all settings in a group to their compile-time defaults and save.
 */
bool app_settings_reset_group(app_settings_group_t g);

/**
 * @brief Reset all settings to defaults and save all groups.
 */
bool app_settings_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif // APP_SETTINGS_H
