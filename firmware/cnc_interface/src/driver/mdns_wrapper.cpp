#include "mdns_wrapper.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <MDNS.h>

// --- Internal state for the async MDNS resolution ---
static WiFiUDP udp;
static MDNS mdns(udp);

static bool resolution_done = false;
static IPAddress resolved_ip_address;
static const char* volatile target_hostname = NULL;

/**
 * @brief Callback function for the MDNS library.
 * This is called when a name is resolved or the query times out.
 */
static void nameFoundCallback(const char* name, IPAddress ip) {
    if (target_hostname && strcmp(name, target_hostname) == 0) {
        resolved_ip_address = ip;
        resolution_done = true;
    }
}

// --- Public C API Functions ---

#ifdef __cplusplus
extern "C" {
#endif

void mdns_wrapper_init() {
    // The MDNS object is constructed with a UDP instance.
    // We need to begin it once WiFi is connected.
    if (WiFi.status() == WL_CONNECTED) {
        mdns.begin(WiFi.localIP(), "cnc-pendant");
        mdns.setNameResolvedCallback(nameFoundCallback);
    }
}

bool mdns_wrapper_query_host(const char* host, uint32_t* ip_addr_out) {
    if (!host || !ip_addr_out) {
        return false;
    }

    // Set up the state for the async query
    target_hostname = host;
    resolution_done = false;
    resolved_ip_address = INADDR_NONE;

    // Start the resolution. The result will be delivered to nameFoundCallback.
    // The timeout parameter here is for the MDNS library's internal query process.
    mdns.resolveName(host, 5000);

    return true; // Indicates that the query has started
}

mdns_query_status_t mdns_wrapper_run(uint32_t* ip_addr_out) {
    mdns.run();
    
    if (resolution_done) {
        if (resolved_ip_address != INADDR_NONE) {
            *ip_addr_out = (uint32_t)resolved_ip_address;
            target_hostname = NULL; // Clear for next query
            return MDNS_QUERY_SUCCESS;
        } else {
            *ip_addr_out = 0;
            target_hostname = NULL; // Clear for next query
            return MDNS_QUERY_FAIL;
        }
    }
    
    return MDNS_QUERY_PENDING;
}


#ifdef __cplusplus
}
#endif
