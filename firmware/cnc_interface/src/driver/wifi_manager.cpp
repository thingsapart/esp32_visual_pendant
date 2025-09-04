#include "wifi_manager.h"

#ifdef ESP32_HW
#include <Arduino.h>
#include <WiFi.h>

#include "mdns_wrapper.h"
#endif

#include "debug.h"

static const char* TAG = "wifi_manager";

void wifi_manager_init() {
#ifdef ESP32_HW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  LOGI(TAG, "Wi-Fi manager initialized.");
#else
  LOGI(TAG, "Wi-Fi manager initialized (native simulation).");
#endif
}

void wifi_manager_disconnect() {
#ifdef ESP32_HW
  WiFi.disconnect();
#endif
}

bool wifi_manager_connect(const char* ssid, const char* password,
                          uint32_t timeout_ms) {
#ifdef ESP32_HW
  LOGI(TAG, "Connecting to SSID: %s", ssid);
  WiFi.begin(ssid, password);

  uint32_t start_time = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start_time > timeout_ms) {
      LOGE(TAG, "Connection timed out!");
      WiFi.disconnect();
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  LOGI(TAG, "Connected! IP Address: %s", WiFi.localIP().toString().c_str());
  mdns_wrapper_init();  // Initialize MDNS now that we have an IP
  return true;
#else
  LOGI(TAG, "Simulating Wi-Fi connection to SSID: %s", ssid);
  return true;  // Always succeed in native simulation
#endif
}

bool wifi_manager_resolve_host(const char* host, char* ip_buffer,
                               size_t buffer_len) {
  if (!host || host[0] == '\0' || !ip_buffer || buffer_len == 0) {
    return false;
  }

#ifdef ESP32_HW
  // Check if it's already an IP address by trying to parse it.
  IPAddress ip;
  if (ip.fromString(host)) {
    strncpy(ip_buffer, host, buffer_len - 1);
    ip_buffer[buffer_len - 1] = '\0';
    return true;
  }

  // It's a hostname, try to resolve it asynchronously
  LOGI(TAG, "Resolving hostname '%s' via mDNS/DNS...", host);
  uint32_t ip_addr_num = 0;

  if (!mdns_wrapper_query_host(host, &ip_addr_num)) {
    LOGE(TAG, "Failed to start MDNS query.");
    return false;
  }

  uint32_t start_time = millis();
  while (millis() - start_time < 5000) {  // 5 second overall timeout
    mdns_query_status_t status = mdns_wrapper_run(&ip_addr_num);
    if (status == MDNS_QUERY_SUCCESS) {
      IPAddress resolved_ip(ip_addr_num);
      String ip_str = resolved_ip.toString();
      LOGI(TAG, "Hostname '%s' resolved to %s", host, ip_str.c_str());
      strncpy(ip_buffer, ip_str.c_str(), buffer_len - 1);
      ip_buffer[buffer_len - 1] = '\0';
      return true;
    }
    if (status == MDNS_QUERY_FAIL) {
      LOGE(TAG, "Could not resolve hostname '%s'", host);
      ip_buffer[0] = '\0';
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));  // Don't spinlock
  }

  LOGE(TAG, "MDNS query for '%s' timed out.", host);
  ip_buffer[0] = '\0';
  return false;

#else
  LOGI(TAG, "Simulating host resolution for: %s", host);
  // Simulate resolving to a common local IP for testing
  strncpy(ip_buffer, "192.168.1.100", buffer_len - 1);
  ip_buffer[buffer_len - 1] = '\0';
  return true;
#endif
}

void wifi_manager_scan(wifi_scan_done_cb_t callback, void* user_data) {
#ifdef ESP32_HW
  LOGI(TAG, "Starting Wi-Fi scan...");
  int n = WiFi.scanNetworks();
  LOGI(TAG, "Scan done, %d networks found.", n);

  if (n == 0) {
    if (callback) callback("", user_data);
  } else {
    // Use Arduino String class for safer and more idiomatic string building
    String ssid_list;
    for (int i = 0; i < n; ++i) {
      ssid_list += WiFi.SSID(i);
      if (i < n - 1) {
        ssid_list += "\n";
      }
    }

    if (callback) {
      callback(ssid_list.c_str(), user_data);
    }
  }
  // Clear the scan results from memory
  WiFi.scanDelete();
#else
  LOGI(TAG, "Simulating Wi-Fi scan...");
  const char* fake_results =
      "FakeSSID-1\nMyHomeWiFi\nAnotherNetwork\nEspressif-Guest";
  if (callback) callback(fake_results, user_data);
#endif
}

bool wifi_manager_is_connected() {
#ifdef ESP32_HW
  return WiFi.status() == WL_CONNECTED;
#else
  return true;  // Always connected in simulation
#endif
}
