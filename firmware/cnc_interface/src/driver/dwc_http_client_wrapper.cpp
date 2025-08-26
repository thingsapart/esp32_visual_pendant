#include "dwc_http_client_wrapper.h"
#include <ArduinoHttpClient.h>
#include <WiFi.h>

#ifndef LOGE
#include "debug.h"
#endif

static const char* TAG = "DwcHttpClientWrapper";

// Using a struct to hold the C++ objects, so their lifecycle is managed.
struct DwcHttpClient {
    WiFiClient wifi_client;
    HttpClient http_client;
    String host; // Using String for convenience with the library

    DwcHttpClient(const char* h, int port) : http_client(wifi_client, h, port), host(h) {
        http_client.setHttpResponseTimeout(3000);
    }
};

extern "C" {

dwc_http_handle_t dwc_http_create(const char* host, int port) {
    if (!host) return NULL;
    // The handle is a pointer to our C++ helper struct
    return new DwcHttpClient(host, port);
}

void dwc_http_destroy(dwc_http_handle_t handle) {
    if (handle) {
        delete static_cast<DwcHttpClient*>(handle);
    }
}

int dwc_http_get(dwc_http_handle_t handle, const char* path, char* response_buffer, size_t buffer_len, int* status_code) {
    if (!handle || !path || !response_buffer || !status_code) {
        if(status_code) *status_code = -1;
        return -1;
    }
    
    DwcHttpClient* dwc = static_cast<DwcHttpClient*>(handle);
    HttpClient& client = dwc->http_client;
    
    LOGD(TAG, "Making GET request to http://%s%s", dwc->host.c_str(), path);

    // Make the request
    int err = client.get(path);
    if (err != 0) {
        LOGE(TAG, "client.get() failed, error: %d. Stopping client.", err);
        client.stop(); // Clean up the failed connection
        *status_code = err; 
        return -1;
    }
    
    // Get the status code, this also parses the headers
    *status_code = client.responseStatusCode();
    LOGD(TAG, "HTTP Status Code: %d", *status_code);

    // Get the response body as a String object, which correctly handles headers.
    String body = client.responseBody();
    int body_len = body.length();
    
    // Copy to the C buffer
    if ((size_t)body_len >= buffer_len) {
        LOGW(TAG, "Response body (len %d) is larger than buffer (len %d), truncating.", body_len, buffer_len);
        body_len = buffer_len - 1;
    }
    memcpy(response_buffer, body.c_str(), body_len);
    response_buffer[body_len] = '\0';
    
    LOGV(TAG, "Response Body (len %d):\n%s", body_len, response_buffer);

    // The server sends "Connection: close", so we must stop the client to close the socket.
    // The next client.get() will open a new connection.
    client.stop();
    
    return body_len;
}

} // extern "C"
