#include "dwc_http_client_wrapper.h"

#include "debug.h"

#ifdef ESP32_HW
#define UI_DEBUG_LOCAL_LEVEL D_WARN
#endif
#include "debug.h"

#ifdef ESP32_HW
#include <HTTPClient.h>
#include <WiFi.h>
#else
#include <ArduinoHttpClient.h>
#include <WiFi.h>
#endif

static const char* TAG = "DwcHttpClientWrapper";

#ifdef ESP32_HW

// --- ESP32 Native HTTPClient Implementation ---

// The handle in this case is just a context struct holding config.
// The HTTPClient object is created and destroyed per-request.
struct DwcHttpClientContext {
  char* host;
  int port;
};

extern "C" {

dwc_http_handle_t dwc_http_create(const char* host, int port) {
  if (!host) return NULL;
  DwcHttpClientContext* context = new DwcHttpClientContext();
  if (!context) return NULL;
  context->host = strdup(host);
  context->port = port;
  return context;
}

void dwc_http_destroy(dwc_http_handle_t handle) {
  if (handle) {
    DwcHttpClientContext* context = static_cast<DwcHttpClientContext*>(handle);
    free(context->host);
    delete context;
  }
}

int dwc_http_get(dwc_http_handle_t handle, const char* path,
                 char* response_buffer, size_t buffer_len, int* status_code) {
  if (!handle || !path || !response_buffer || !status_code) {
    if (status_code) *status_code = -1;
    return -1;
  }

  DwcHttpClientContext* context = static_cast<DwcHttpClientContext*>(handle);
  HTTPClient http;

  // Construct the full UR
  char full_url[256];
  snprintf(full_url, sizeof(full_url), "http://%s:%d%s", context->host,
           context->port, path);

  LOGD(TAG, "Making GET request to %s", full_url);

  http.begin(full_url);
  http.setTimeout(3000);

  int httpCode = http.GET();
  *status_code = httpCode;

  if (httpCode > 0) {
    LOGD(TAG, "HTTP Status Code: %d", httpCode);
    String payload = http.getString();
    int payload_len = payload.length();

    if ((size_t)payload_len >= buffer_len) {
      LOGW(TAG,
           "Response body (len %d) is larger than buffer (len %d), truncating.",
           payload_len, buffer_len);
      payload_len = buffer_len - 1;
    }
    memcpy(response_buffer, payload.c_str(), payload_len);
    response_buffer[payload_len] = '\0';

    LOGV(TAG, "Response Body (len %d):\n%s", payload_len, response_buffer);
    http.end();
    return payload_len;

  } else {
    LOGE(TAG, "HTTP GET failed, error: %s",
         http.errorToString(httpCode).c_str());
    http.end();
    return -1;
  }
}

}  // extern "C"

#else  // Fallback to ArduinoHttpClient for other platforms

// --- ArduinoHttpClient Implementation ---

struct DwcHttpClientContext {
  char* host;
  int port;
};

extern "C" {

dwc_http_handle_t dwc_http_create(const char* host, int port) {
  if (!host) return NULL;
  DwcHttpClientContext* context = new DwcHttpClientContext();
  if (!context) return NULL;
  context->host = strdup(host);
  context->port = port;
  return context;
}

void dwc_http_destroy(dwc_http_handle_t handle) {
  if (handle) {
    DwcHttpClientContext* context = static_cast<DwcHttpClientContext*>(handle);
    free(context->host);
    delete context;
  }
}

int dwc_http_get(dwc_http_handle_t handle, const char* path,
                 char* response_buffer, size_t buffer_len, int* status_code) {
  if (!handle || !path || !response_buffer || !status_code) {
    if (status_code) *status_code = -1;
    return -1;
  }

  DwcHttpClientContext* context = static_cast<DwcHttpClientContext*>(handle);

  WiFiClient wifi_client;
  HttpClient http_client(wifi_client, context->host, context->port);
  http_client.setHttpResponseTimeout(3000);

  LOGD(TAG, "Making GET request to http://%s:%d%s", context->host,
       context->port, path);

  if (!http_client.connect(context->host, context->port)) {
    LOGE(TAG, "HTTP connection failed before GET.");
    *status_code = -10;
    return -1;
  }

  int err = http_client.get(path);
  if (err != 0) {
    LOGE(TAG, "client.get() failed, error: %d.", err);
    *status_code = err;
    http_client.stop();
    return -1;
  }

  *status_code = http_client.responseStatusCode();
  LOGD(TAG, "HTTP Status Code: %d", *status_code);

  String body = http_client.responseBody();
  int body_len = body.length();

  if ((size_t)body_len >= buffer_len) {
    LOGW(TAG,
         "Response body (len %d) is larger than buffer (len %d), truncating.",
         body_len, buffer_len);
    body_len = buffer_len - 1;
  }
  memcpy(response_buffer, body.c_str(), body_len);
  response_buffer[body_len] = '\0';

  LOGV(TAG, "Response Body (len %d):\n%s", body_len, response_buffer);

  http_client.stop();

  return body_len;
}

}  // extern "C"

#endif  // ESP32_HW
