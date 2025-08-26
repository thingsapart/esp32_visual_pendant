#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_scan_done_cb_t)(const char* scan_results, void* user_data);

/**
 * @brief Initializes the Wi-Fi manager.
 */
void wifi_manager_init();

/**
 * @brief Connects to a Wi-Fi network.
 *
 * This is a blocking call that will attempt to connect for a specified timeout.
 *
 * @param ssid The SSID of the network.
 * @param password The password for the network.
 * @param timeout_ms The maximum time to wait for a connection.
 * @return true if connected successfully, false otherwise.
 */
bool wifi_manager_connect(const char* ssid, const char* password, uint32_t timeout_ms);

/**
 * @brief Disconnects from the current Wi-Fi network.
 */
void wifi_manager_disconnect();

/**
 * @brief Resolves a hostname to an IPv4 address using mDNS or DNS.
 *
 * If the provided host string is already a valid IPv4 address, it will be
 * copied directly to the output buffer.
 *
 * @param host The hostname or IP address string to resolve.
 * @param ip_buffer A character buffer to store the resulting IP address string.
 * @param buffer_len The size of the ip_buffer.
 * @return true if the host was successfully resolved or was already an IP,
 *         false on failure (e.g., timeout, host not found).
 */
bool wifi_manager_resolve_host(const char* host, char* ip_buffer, size_t buffer_len);

/**
 * @brief Scans for available Wi-Fi networks.
 *
 * This is a blocking call. The results will be provided via the callback.
 *
 * @param callback The function to call when the scan is complete.
 * @param user_data A pointer to pass to the callback function.
 */
void wifi_manager_scan(wifi_scan_done_cb_t callback, void* user_data);


/**
 * @brief Checks if the device is currently connected to Wi-Fi.
 *
 * @return true if connected, false otherwise.
 */
bool wifi_manager_is_connected();


#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
