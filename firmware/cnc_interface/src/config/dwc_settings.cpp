#include "dwc_settings.h"
#include <string.h>

#ifdef ESP32_HW
#include <Preferences.h>
#endif

#include "debug.h"

static const char* TAG = "dwc_settings";
static const char* NVS_NAMESPACE = "dwc-config";

// Keys for NVS
static const char* KEY_SSID = "ssid";
static const char* KEY_PASSWORD = "password";
static const char* KEY_HOST = "host";

static dwc_settings_t g_settings;
static bool g_loaded = false;

#ifdef ESP32_HW
static Preferences preferences;
#endif

void dwc_settings_init() {
    memset(&g_settings, 0, sizeof(dwc_settings_t));
    g_loaded = false;
}

bool dwc_settings_load() {
#ifdef ESP32_HW
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        LOGE(TAG, "Failed to initialize preferences");
        return false;
    }

    preferences.getString(KEY_SSID, g_settings.ssid, sizeof(g_settings.ssid));
    preferences.getString(KEY_PASSWORD, g_settings.password, sizeof(g_settings.password));
    preferences.getString(KEY_HOST, g_settings.host, sizeof(g_settings.host));

    preferences.end();
    g_loaded = true;
    LOGI(TAG, "Loaded settings: SSID='%s', Host='%s'", g_settings.ssid, g_settings.host);
    return true;
#else
    LOGW(TAG, "DWC settings not supported on this platform.");
    // Simulate empty settings for native builds
    g_loaded = true;
    return true;
#endif
}

void dwc_settings_clear() {
#ifdef ESP32_HW
    preferences.clear();
#endif
}

bool dwc_settings_save(const dwc_settings_t* settings) {
#ifdef ESP32_HW
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        LOGE(TAG, "Failed to initialize preferences for writing");
        return false;
    }

    bool success = true;
    if (preferences.putString(KEY_SSID, settings->ssid) == 0) {
        LOGE(TAG, "Failed to save SSID");
        success = false;
    }
    if (preferences.putString(KEY_PASSWORD, settings->password) == 0) {
        LOGE(TAG, "Failed to save password");
        success = false;
    }
    if (preferences.putString(KEY_HOST, settings->host) == 0) {
        LOGE(TAG, "Failed to save host");
        success = false;
    }

    preferences.end(); // This commits the changes to flash and returns void.

    if (success) {
        LOGI(TAG, "Saved settings: SSID='%s', Host='%s'", settings->ssid, settings->host);
        memcpy(&g_settings, settings, sizeof(dwc_settings_t));
    } else {
        LOGE(TAG, "Failed to write one or more settings to NVS");
    }
    return success;
#else
    LOGW(TAG, "DWC settings not supported on this platform.");
    return false;
#endif
}

bool dwc_settings_are_valid() {
    return g_loaded && g_settings.ssid[0] != '\0' && g_settings.host[0] != '\0';
}

const dwc_settings_t* dwc_settings_get() {
    return &g_settings;
}
