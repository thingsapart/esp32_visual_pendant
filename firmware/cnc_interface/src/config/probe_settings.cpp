// probe_settings.cpp — Persistent probe operation parameters
//
// Follows the same pattern as dwc_settings.cpp: wraps ESP-IDF Preferences
// (NVS) on hardware, no-ops on native/simulation builds.

#include "probe_settings.h"

#include <string.h>
#include "debug.h"

#ifdef ESP32_HW
#include <Preferences.h>
static Preferences s_prefs;
#endif

static const char *TAG           = "probe_settings";
static const char *NVS_NAMESPACE = "probe_cfg";
// Key bumped to v2 because the struct gained new fields.
static const char *KEY_SETTINGS  = "settings_v2";

// In-RAM copy, initialised to defaults by probe_settings_init().
static probe_settings_t s_settings;

void probe_settings_init(void) {
    s_settings.safe_z          = PROBE_SETTINGS_DEFAULT_SAFE_Z;
    s_settings.max_z_depth     = PROBE_SETTINGS_DEFAULT_MAX_Z_DEPTH;
    s_settings.xy_probe_depth  = PROBE_SETTINGS_DEFAULT_XY_PROBE_DEPTH;
    s_settings.width           = PROBE_SETTINGS_DEFAULT_WIDTH;
    s_settings.height          = PROBE_SETTINGS_DEFAULT_HEIGHT;
    s_settings.clearance       = PROBE_SETTINGS_DEFAULT_CLEARANCE;
    s_settings.overtravel      = PROBE_SETTINGS_DEFAULT_OVERTRAVEL;
    s_settings.quick_mode      = PROBE_SETTINGS_DEFAULT_QUICK_MODE;
}

bool probe_settings_load(void) {
#ifdef ESP32_HW
    if (!s_prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) {
        LOGW(TAG, "NVS namespace not found, using defaults");
        return false;
    }
    size_t sz = s_prefs.getBytesLength(KEY_SETTINGS);
    bool ok = (sz == sizeof(probe_settings_t));
    if (ok) {
        probe_settings_t tmp;
        s_prefs.getBytes(KEY_SETTINGS, &tmp, sizeof(tmp));
        s_settings = tmp;
        LOGI(TAG, "Loaded: safe_z=%.2f max_z=%.2f xy_depth=%.2f w=%.2f h=%.2f cl=%.2f ov=%.2f quick=%d",
             (double)s_settings.safe_z,
             (double)s_settings.max_z_depth,
             (double)s_settings.xy_probe_depth,
             (double)s_settings.width,
             (double)s_settings.height,
             (double)s_settings.clearance,
             (double)s_settings.overtravel,
             (int)s_settings.quick_mode);
    } else {
        LOGW(TAG, "NVS probe_cfg size mismatch (%u vs %u), using defaults",
             (unsigned)sz, (unsigned)sizeof(probe_settings_t));
    }
    s_prefs.end();
    return ok;
#else
    LOGW(TAG, "NVS not available on this platform, using defaults");
    return false;
#endif
}

bool probe_settings_save(const probe_settings_t *s) {
    if (!s) return false;
#ifdef ESP32_HW
    if (!s_prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        LOGE(TAG, "Failed to open NVS namespace for writing");
        return false;
    }
    bool ok = (s_prefs.putBytes(KEY_SETTINGS, s, sizeof(*s)) == sizeof(*s));
    s_prefs.end();
    if (ok) {
        s_settings = *s;
        LOGI(TAG, "Saved: safe_z=%.2f max_z=%.2f xy_depth=%.2f w=%.2f h=%.2f cl=%.2f ov=%.2f quick=%d",
             (double)s->safe_z,
             (double)s->max_z_depth,
             (double)s->xy_probe_depth,
             (double)s->width,
             (double)s->height,
             (double)s->clearance,
             (double)s->overtravel,
             (int)s->quick_mode);
    } else {
        LOGE(TAG, "Failed to write probe settings to NVS");
    }
    return ok;
#else
    LOGW(TAG, "NVS not available, probe settings not persisted");
    s_settings = *s;
    return false;
#endif
}

const probe_settings_t *probe_settings_get(void) {
    return &s_settings;
}
