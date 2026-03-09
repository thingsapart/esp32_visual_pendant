// app_settings.cpp — NVS-backed declarative settings implementation
//
// Follows the same Preferences pattern as dwc_settings.cpp and
// probe_settings.cpp.  Each group is stored as a flat array of
// app_setting_val_t blobs under a versioned NVS key.

#include "app_settings.h"

#include <string.h>
#include "debug.h"

// need material enum for dropdown options
#include "machine/machine_interface.h"

// Pull in jog macro defaults so this file acts as the single source of truth
#include "tasks/jog_accumulator_task.h"
#include "config.h"

#ifdef ESP32_HW
#  include <Preferences.h>
static Preferences s_prefs;
#endif

static const char *TAG = "app_settings";

// ---------------------------------------------------------------------------
// Setting descriptors — one table per group
// Keep in the same order as the key enums in app_settings.h
// ---------------------------------------------------------------------------

static const app_setting_def_t s_jog_defs[APP_SETTINGS_JOG__COUNT] = {
    [APP_SETTINGS_JOG_FEED_XY]       = { "XY Feed Rate",    "mm/min",
                                          APP_SETTING_TYPE_FLOAT,
                                          {.f=(JOG_ACCUM_FEED_XY_MM_MIN)},
                                          {.f=100.f},  {.f=30000.f}, {.f=100.f} },
    [APP_SETTINGS_JOG_FEED_Z]        = { "Z Feed Rate",     "mm/min",
                                          APP_SETTING_TYPE_FLOAT,
                                          {.f=(JOG_ACCUM_FEED_Z_MM_MIN)},
                                          {.f=50.f},   {.f=10000.f}, {.f=50.f} },
    [APP_SETTINGS_JOG_ACCEL_X]       = { "X Acceleration",  "mm/s\xc2\xb2",
                                          APP_SETTING_TYPE_FLOAT,
                                          {.f=(JOG_ACCUM_ACCEL_X_MM_S2)},
                                          {.f=10.f},   {.f=10000.f}, {.f=50.f} },
    [APP_SETTINGS_JOG_ACCEL_Y]       = { "Y Acceleration",  "mm/s\xc2\xb2",
                                          APP_SETTING_TYPE_FLOAT,
                                          {.f=(JOG_ACCUM_ACCEL_Y_MM_S2)},
                                          {.f=10.f},   {.f=10000.f}, {.f=50.f} },
    [APP_SETTINGS_JOG_ACCEL_Z]       = { "Z Acceleration",  "mm/s\xc2\xb2",
                                          APP_SETTING_TYPE_FLOAT,
                                          {.f=(JOG_ACCUM_ACCEL_Z_MM_S2)},
                                          {.f=10.f},   {.f=5000.f},  {.f=10.f} },
    [APP_SETTINGS_JOG_LEAD_AHEAD_MS] = { "Lead-ahead",      "ms",
                                          APP_SETTING_TYPE_INT32,
                                          {.i=(JOG_ACCUM_LEAD_AHEAD_MS)},
                                          {.i=0},      {.i=200},     {.i=5} },
    [APP_SETTINGS_JOG_MIN_SLEEP_MS]  = { "Min sleep",       "ms",
                                          APP_SETTING_TYPE_INT32,
                                          {.i=(JOG_ACCUM_MIN_SLEEP_MS)},
                                          {.i=1},      {.i=100},     {.i=1} },
    [APP_SETTINGS_JOG_MAX_SLEEP_MS]  = { "Max sleep",       "ms",
                                          APP_SETTING_TYPE_INT32,
                                          {.i=(JOG_ACCUM_MAX_SLEEP_MS)},
                                          {.i=20},     {.i=2000},    {.i=10} },
};

#ifndef CAM_FALLBACK_GRID_DX
#  define CAM_FALLBACK_GRID_DX  25.0f
#endif
#ifndef CAM_FALLBACK_GRID_DY
#  define CAM_FALLBACK_GRID_DY  25.0f
#endif
#ifndef CAM_FALLBACK_GRID_NX
#  define CAM_FALLBACK_GRID_NX  5
#endif
#ifndef CAM_FALLBACK_GRID_NY
#  define CAM_FALLBACK_GRID_NY  5
#endif

static const app_setting_def_t s_cam_defs[APP_SETTINGS_CAM__COUNT] = {
    [APP_SETTINGS_CAM_FALLBACK_GRID_DX] = { "Fallback Grid DX", "mm",
                                             APP_SETTING_TYPE_FLOAT,
                                             {.f=CAM_FALLBACK_GRID_DX},
                                             {.f=1.f}, {.f=500.f}, {.f=1.f} },
    [APP_SETTINGS_CAM_FALLBACK_GRID_DY] = { "Fallback Grid DY", "mm",
                                             APP_SETTING_TYPE_FLOAT,
                                             {.f=CAM_FALLBACK_GRID_DY},
                                             {.f=1.f}, {.f=500.f}, {.f=1.f} },
    [APP_SETTINGS_CAM_FALLBACK_GRID_NX] = { "Fallback Grid Cols", "",
                                             APP_SETTING_TYPE_INT32,
                                             {.i=CAM_FALLBACK_GRID_NX},
                                             {.i=2},  {.i=20},    {.i=1} },
    [APP_SETTINGS_CAM_FALLBACK_GRID_NY] = { "Fallback Grid Rows", "",
                                             APP_SETTING_TYPE_INT32,
                                             {.i=CAM_FALLBACK_GRID_NY},
                                             {.i=2},  {.i=20},    {.i=1} },
};

static const app_setting_def_t s_machine_defs[APP_SETTINGS_MACHINE__COUNT] = {
    [APP_SETTINGS_MACHINE_SEND_INTERVAL_MS] = { "Send Interval", "ms",
                                                 APP_SETTING_TYPE_INT32,
                                                 {.i=(MACHINE_SEND_GCODE_INTERVAL_MS)},
                                                 {.i=10}, {.i=500}, {.i=5} },
    [APP_SETTINGS_MACHINE_POLL_NTH_INTERVAL]= { "Poll Every Nth", "",
                                                 APP_SETTING_TYPE_INT32,
                                                 {.i=(MACHINE_POLL_EVERY_NTH_INTERVAL)},
                                                 {.i=1},  {.i=20},  {.i=1} },
};

// Defaults from hub.cpp and cam_receiver.c (file-local defines — not in headers).
// Update these if the upstream file-local defines change.
#ifndef APP_SETTINGS_HUB_POLL_MS_DEFAULT
#  define APP_SETTINGS_HUB_POLL_MS_DEFAULT        100
#endif
#ifndef APP_SETTINGS_FULL_STATE_N_DEFAULT
#  define APP_SETTINGS_FULL_STATE_N_DEFAULT         48  // every 48 * 100 ms ≈ 4.8 s
#endif
#ifndef APP_SETTINGS_LOG_COALESCE_MS_DEFAULT
#  define APP_SETTINGS_LOG_COALESCE_MS_DEFAULT      30
#endif
#ifndef APP_SETTINGS_CAM_FRAME_TIMEOUT_MS_DEFAULT
#  define APP_SETTINGS_CAM_FRAME_TIMEOUT_MS_DEFAULT 2000
#endif
#ifndef APP_SETTINGS_CAM_STATUS_TIMEOUT_MS_DEFAULT
#  define APP_SETTINGS_CAM_STATUS_TIMEOUT_MS_DEFAULT 5000
#endif

static const app_setting_def_t s_timing_defs[APP_SETTINGS_TIMING__COUNT] = {
    [APP_SETTINGS_TIMING_HUB_POLL_MS]  = { "Hub Poll Interval", "ms",
                                            APP_SETTING_TYPE_INT32,
                                            {.i=APP_SETTINGS_HUB_POLL_MS_DEFAULT},
                                            {.i=20}, {.i=500}, {.i=10} },
    [APP_SETTINGS_TIMING_FULL_STATE_N] = { "Full State Every Nth", "polls",
                                            APP_SETTING_TYPE_INT32,
                                            {.i=APP_SETTINGS_FULL_STATE_N_DEFAULT},
                                            {.i=1}, {.i=200}, {.i=1} },
    [APP_SETTINGS_TIMING_LOG_COALESCE_MS] = { "Log Batch Window", "ms",
                                            APP_SETTING_TYPE_INT32,
                                            {.i=APP_SETTINGS_LOG_COALESCE_MS_DEFAULT},
                                            {.i=5}, {.i=500}, {.i=5} },
    [APP_SETTINGS_TIMING_CAM_FRAME_TIMEOUT_MS] = { "Cam Frame Timeout", "ms",
                                            APP_SETTING_TYPE_INT32,
                                            {.i=APP_SETTINGS_CAM_FRAME_TIMEOUT_MS_DEFAULT},
                                            {.i=100}, {.i=10000}, {.i=100} },
    [APP_SETTINGS_TIMING_CAM_STATUS_TIMEOUT_MS] = { "Cam Status Timeout", "ms",
                                            APP_SETTING_TYPE_INT32,
                                            {.i=APP_SETTINGS_CAM_STATUS_TIMEOUT_MS_DEFAULT},
                                            {.i=500}, {.i=30000}, {.i=500} },
};

// Defaults from mos_machine_handler.c (file-local defines).
#ifndef APP_SETTINGS_PROBE_TIP_RADIUS_DEFAULT
#  define APP_SETTINGS_PROBE_TIP_RADIUS_DEFAULT     1.0f   // mm (2 mm dia tip)
#endif
#ifndef APP_SETTINGS_PROBE_BACKOFF_MULT_DEFAULT
#  define APP_SETTINGS_PROBE_BACKOFF_MULT_DEFAULT   6.0f
#endif

static const app_setting_def_t s_probe_ui_defs[APP_SETTINGS_PROBE_UI__COUNT] = {
    [APP_SETTINGS_PROBE_UI_TIP_RADIUS]  = { "Probe Tip Radius", "mm",
                                             APP_SETTING_TYPE_FLOAT,
                                             {.f=APP_SETTINGS_PROBE_TIP_RADIUS_DEFAULT},
                                             {.f=0.1f}, {.f=5.0f}, {.f=0.1f} },
    [APP_SETTINGS_PROBE_UI_BACKOFF_MULT]= { "XY Backoff Mult", "x",
                                             APP_SETTING_TYPE_FLOAT,
                                             {.f=APP_SETTINGS_PROBE_BACKOFF_MULT_DEFAULT},
                                             {.f=1.0f}, {.f=20.0f}, {.f=0.5f} },
};

// ---------------------------------------------------------------------------
// Material choices (mirrors tool_material_t enum)
// ---------------------------------------------------------------------------
static const char * const s_material_choices[TOOL_MATERIAL_COUNT] = {
    "Aluminium",
    "Mild Steel",
    "Stainless Steel",
    "Hard Plastic",
    "Acrylic",
    "MDF",
    "Softwood / Plywood",
    "Hardwood",
};

static const app_setting_def_t s_material_defs[APP_SETTINGS_MATERIALS__COUNT] = {
    [APP_SETTINGS_MATERIAL_TYPE] = { .name = "Material", 
                                     .unit = "",
                                     .type = APP_SETTING_TYPE_ENUM,
                                     .def = { .i=TOOL_MATERIAL_ALUMINIUM },
                                     .min = { .i=0 }, 
                                     .max = {.i=TOOL_MATERIAL_COUNT-1 }, 
                                     .step = { .i=1 },
                                     .choices = s_material_choices,
                                     .choice_count = TOOL_MATERIAL_COUNT 
                                    },
};

// ---------------------------------------------------------------------------
// Group metadata table
// ---------------------------------------------------------------------------

typedef struct {
    const char             *name;
    const app_setting_def_t *defs;
    int                      count;
    const char             *nvs_namespace;
    const char             *nvs_key;        // versioned key
} group_meta_t;

static const group_meta_t s_groups[APP_SETTINGS_GROUP__COUNT] = {
    [APP_SETTINGS_GROUP_JOG]      = { "Jog Movement",   s_jog_defs,
                                      APP_SETTINGS_JOG__COUNT,
                                      "app-jog",     "vals_v1" },
    [APP_SETTINGS_GROUP_CAM]      = { "Camera",          s_cam_defs,
                                      APP_SETTINGS_CAM__COUNT,
                                      "app-cam",     "vals_v1" },
    [APP_SETTINGS_GROUP_MACHINE]  = { "Machine Timing",  s_machine_defs,
                                      APP_SETTINGS_MACHINE__COUNT,
                                      "app-machine", "vals_v1" },
    [APP_SETTINGS_GROUP_TIMING]   = { "Network/Hub",     s_timing_defs,
                                      APP_SETTINGS_TIMING__COUNT,
                                      "app-timing",  "vals_v1" },
    [APP_SETTINGS_GROUP_PROBE_UI] = { "Probe UI",        s_probe_ui_defs,
                                      APP_SETTINGS_PROBE_UI__COUNT,
                                      "app-probe-ui","vals_v1" },
    [APP_SETTINGS_GROUP_MATERIALS] = { "Materials",        s_material_defs,
                                      APP_SETTINGS_MATERIALS__COUNT,
                                      "app-materials","vals_v1" },
};

// ---------------------------------------------------------------------------
// In-RAM cache — indexed [group][key]
// ---------------------------------------------------------------------------

static app_setting_val_t s_vals[APP_SETTINGS_GROUP__COUNT]
                                [APP_SETTINGS_MAX_KEYS_PER_GROUP];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool _group_key_valid(app_settings_group_t g, int key)
{
    if (g < 0 || g >= APP_SETTINGS_GROUP__COUNT) return false;
    if (key < 0 || key >= s_groups[g].count) return false;
    return true;
}

static void _apply_defaults(app_settings_group_t g)
{
    const group_meta_t *m = &s_groups[g];
    for (int k = 0; k < m->count; k++) {
        s_vals[g][k] = m->defs[k].def;
    }
}

// ---------------------------------------------------------------------------
// Public: init / load / save
// ---------------------------------------------------------------------------

void app_settings_init(void)
{
    for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++) {
        _apply_defaults((app_settings_group_t)g);
    }
    LOGI(TAG, "Defaults applied for %d groups", APP_SETTINGS_GROUP__COUNT);
}

bool app_settings_load(void)
{
    bool all_ok = true;
#ifdef ESP32_HW
    for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++) {
        const group_meta_t *m = &s_groups[g];
        size_t expected = (size_t)m->count * sizeof(app_setting_val_t);

        if (!s_prefs.begin(m->nvs_namespace, /*readOnly=*/true)) {
            LOGW(TAG, "[%s] NVS namespace absent — using defaults", m->name);
            all_ok = false;
            continue;
        }
        size_t sz = s_prefs.getBytesLength(m->nvs_key);
            if (sz == expected) {
            s_prefs.getBytes(m->nvs_key, &s_vals[g][0], expected);
            LOGI(TAG, "[%s] loaded %u bytes", m->name, (unsigned)expected);
        } else {
            LOGW(TAG, "[%s] NVS size %u vs %u — using defaults",
                 m->name, (unsigned)sz, (unsigned)expected);
            all_ok = false;
        }
        s_prefs.end();
    }
#else
    LOGW(TAG, "NVS not available — using defaults");
    all_ok = false;
#endif
    return all_ok;
}

bool app_settings_save_group(app_settings_group_t group)
{
    if (group < 0 || group >= APP_SETTINGS_GROUP__COUNT) return false;
#ifdef ESP32_HW
    const group_meta_t *m = &s_groups[group];
    size_t sz = (size_t)m->count * sizeof(app_setting_val_t);

    if (!s_prefs.begin(m->nvs_namespace, /*readOnly=*/false)) {
        LOGE(TAG, "[%s] failed to open NVS for write", m->name);
        return false;
    }
    bool ok = (s_prefs.putBytes(m->nvs_key, &s_vals[group][0], sz) == sz);
    s_prefs.end();
    if (ok) {
        LOGI(TAG, "[%s] saved %u bytes", m->name, (unsigned)sz);
    } else {
        LOGE(TAG, "[%s] NVS write failed", m->name);
    }
    return ok;
#else
    LOGW(TAG, "NVS not available — settings not persisted");
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Typed getters
// ---------------------------------------------------------------------------

float app_settings_get_float(app_settings_group_t g, int key)
{
    if (!_group_key_valid(g, key)) return 0.0f;
    return s_vals[g][key].f;
}

int32_t app_settings_get_int(app_settings_group_t g, int key)
{
    if (!_group_key_valid(g, key)) return 0;
    return s_vals[g][key].i;
}

bool app_settings_get_bool(app_settings_group_t g, int key)
{
    if (!_group_key_valid(g, key)) return false;
    return s_vals[g][key].b;
}

// ---------------------------------------------------------------------------
// Typed setters — in-RAM only
// ---------------------------------------------------------------------------

bool app_settings_set_float(app_settings_group_t g, int key, float v)
{
    if (!_group_key_valid(g, key)) return false;
    const app_setting_def_t *d = &s_groups[g].defs[key];
    if (v < d->min.f) v = d->min.f;
    if (v > d->max.f) v = d->max.f;
    s_vals[g][key].f = v;
    return true;
}

bool app_settings_set_int(app_settings_group_t g, int key, int32_t v)
{
    if (!_group_key_valid(g, key)) return false;
    const app_setting_def_t *d = &s_groups[g].defs[key];
    if (v < d->min.i) v = d->min.i;
    if (v > d->max.i) v = d->max.i;
    s_vals[g][key].i = v;
    return true;
}

bool app_settings_set_bool(app_settings_group_t g, int key, bool v)
{
    if (!_group_key_valid(g, key)) return false;
    s_vals[g][key].b = v;
    return true;
}

// ---------------------------------------------------------------------------
// Generic value access for the settings UI
// ---------------------------------------------------------------------------

app_setting_val_t app_settings_get_val(app_settings_group_t g, int key)
{
    app_setting_val_t zero = {.i = 0};
    if (!_group_key_valid(g, key)) return zero;
    return s_vals[g][key];
}

bool app_settings_set_val(app_settings_group_t g, int key, app_setting_val_t v)
{
    if (!_group_key_valid(g, key)) return false;
    const app_setting_def_t *d = &s_groups[g].defs[key];
    switch (d->type) {
        case APP_SETTING_TYPE_FLOAT:
            if (v.f < d->min.f) v.f = d->min.f;
            if (v.f > d->max.f) v.f = d->max.f;
            break;
        case APP_SETTING_TYPE_INT32:
            if (v.i < d->min.i) v.i = d->min.i;
            if (v.i > d->max.i) v.i = d->max.i;
            break;
        case APP_SETTING_TYPE_BOOL:
            break;
    }
    s_vals[g][key] = v;
    return true;
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

int app_settings_group_count(void)
{
    return APP_SETTINGS_GROUP__COUNT;
}

const char *app_settings_group_name(app_settings_group_t g)
{
    if (g < 0 || g >= APP_SETTINGS_GROUP__COUNT) return "?";
    return s_groups[g].name;
}

int app_settings_key_count(app_settings_group_t g)
{
    if (g < 0 || g >= APP_SETTINGS_GROUP__COUNT) return 0;
    return s_groups[g].count;
}

const app_setting_def_t *app_settings_get_def(app_settings_group_t g, int key)
{
    if (!_group_key_valid(g, key)) return NULL;
    return &s_groups[g].defs[key];
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

bool app_settings_reset_group(app_settings_group_t g)
{
    if (g < 0 || g >= APP_SETTINGS_GROUP__COUNT) return false;
    _apply_defaults(g);
    return app_settings_save_group(g);
}

bool app_settings_reset_all(void)
{
    bool ok = true;
    for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++) {
        _apply_defaults((app_settings_group_t)g);
        ok &= app_settings_save_group((app_settings_group_t)g);
    }
    return ok;
}
