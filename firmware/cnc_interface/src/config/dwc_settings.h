#ifndef DWC_SETTINGS_H
#define DWC_SETTINGS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Structure to hold the DWC configuration
typedef struct {
    char ssid[33];      // 32 max SSID length + null terminator
    char password[65];  // 64 max password length + null terminator
    char host[64];      // Hostname or IP address
} dwc_settings_t;

/**
 * @brief Initializes the DWC settings module.
 */
void dwc_settings_init();

/**
 * @brief Loads the DWC settings from non-volatile storage.
 *
 * @return true if settings were loaded successfully, false otherwise.
 */
bool dwc_settings_load();

/**
 * @brief Clears the DWC settings from non-volatile storage.
 */
void dwc_settings_clear();

/**
 * @brief Saves the DWC settings to non-volatile storage.
 *
 * @param settings A pointer to the settings structure to save.
 * @return true if settings were saved successfully, false otherwise.
 */
bool dwc_settings_save(const dwc_settings_t* settings);

/**
 * @brief Checks if valid settings have been loaded.
 *
 * @return true if the loaded SSID is not empty, false otherwise.
 */
bool dwc_settings_are_valid();

/**
 * @brief Gets a pointer to the currently loaded settings.
 *
 * @return A const pointer to the internal settings structure. Do not modify.
 */
const dwc_settings_t* dwc_settings_get();

#ifdef __cplusplus
}
#endif

#endif // DWC_SETTINGS_H
