#ifndef MDNS_WRAPPER_H
#define MDNS_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MDNS_QUERY_PENDING,
    MDNS_QUERY_SUCCESS,
    MDNS_QUERY_FAIL
} mdns_query_status_t;


/**
 * @brief Initializes the MDNS service. Must be called after WiFi is connected.
 */
void mdns_wrapper_init();

/**
 * @brief Starts a non-blocking mDNS/DNS query for a given hostname.
 *
 * This function initiates the name resolution process. The result is not
 * available immediately. You must call `mdns_wrapper_run()` periodically
 * to process the query and get the result.
 *
 * @param host The hostname to resolve (e.g., "duet3.local").
 * @param ip_addr_out Pointer to a uint32_t. This is not used on initiation but
 *                    is required for consistency with the final result retrieval.
 * @return true if the query was successfully started, false otherwise.
 */
bool mdns_wrapper_query_host(const char* host, uint32_t* ip_addr_out);

/**
 * @brief Processes MDNS packets and checks the status of an ongoing query.
 *
 * This function must be called periodically in a loop after starting a query
 * with `mdns_wrapper_query_host`.
 *
 * @param ip_addr_out Pointer to a uint32_t where the resolved IPv4 address will be
 *                    stored upon successful completion.
 * @return The current status of the query (PENDING, SUCCESS, or FAIL).
 */
mdns_query_status_t mdns_wrapper_run(uint32_t* ip_addr_out);


#ifdef __cplusplus
}
#endif

#endif // MDNS_WRAPPER_H
