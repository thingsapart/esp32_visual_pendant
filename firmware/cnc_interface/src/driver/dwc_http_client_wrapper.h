#ifndef DWC_HTTP_CLIENT_WRAPPER_H
#define DWC_HTTP_CLIENT_WRAPPER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to the underlying C++ HttpClient object
typedef void* dwc_http_handle_t;

/**
 * @brief Creates and initializes an HTTP client for a specific host.
 *
 * @param host The hostname or IP address of the Duet board.
 * @param port The port number (usually 80).
 * @return A handle to the HTTP client, or NULL on failure.
 */
dwc_http_handle_t dwc_http_create(const char* host, int port);

/**
 * @brief Destroys and cleans up the HTTP client.
 *
 * @param handle The handle returned by dwc_http_create.
 */
void dwc_http_destroy(dwc_http_handle_t handle);

/**
 * @brief Performs an HTTP GET request.
 *
 * @param handle The handle to the HTTP client.
 * @param path The path and query string for the request (e.g., "/rr_status").
 * @param response_buffer A buffer to store the response body.
 * @param buffer_len The maximum size of the response buffer.
 * @param status_code A pointer to an integer where the HTTP status code will be
 * stored.
 * @return The number of bytes read into the buffer, or a negative value on
 * error.
 */
int dwc_http_get(dwc_http_handle_t handle, const char* path,
                 char* response_buffer, size_t buffer_len, int* status_code);

#ifdef __cplusplus
}
#endif

#endif  // DWC_HTTP_CLIENT_WRAPPER_H
