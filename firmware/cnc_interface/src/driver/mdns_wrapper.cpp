#include "mdns_wrapper.h"

// Provide a portable mdns wrapper: when DWC_MACHINE_MODE is enabled we use
// the real MDNS implementation; otherwise provide lightweight stubs so
// callers (e.g. `wifi_manager`) can link cleanly in builds that don't
// enable the DWC-specific MDNS code path.

#include "mdns_wrapper.h"

#if defined(DWC_MACHINE_MODE)
#include <Arduino.h>
#include <MDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// --- Internal state for the async MDNS resolution ---
static WiFiUDP udp;
static MDNS mdns(udp);

static bool resolution_done = false;
static IPAddress resolved_ip_address;
static const char* volatile target_hostname = NULL;

static void nameFoundCallback(const char* name, IPAddress ip) {
  if (target_hostname && strcmp(name, target_hostname) == 0) {
    resolved_ip_address = ip;
    resolution_done = true;
  }
}

extern "C" {

void mdns_wrapper_init() {
  if (WiFi.status() == WL_CONNECTED) {
    mdns.begin(WiFi.localIP(), "cnc-pendant");
    mdns.setNameResolvedCallback(nameFoundCallback);
  }
}

bool mdns_wrapper_query_host(const char* host, uint32_t* ip_addr_out) {
  if (!host || !ip_addr_out) return false;
  target_hostname = host;
  resolution_done = false;
  resolved_ip_address = INADDR_NONE;
  mdns.resolveName(host, 5000);
  return true;
}

mdns_query_status_t mdns_wrapper_run(uint32_t* ip_addr_out) {
  mdns.run();
  if (resolution_done) {
    if (resolved_ip_address != INADDR_NONE) {
      *ip_addr_out = (uint32_t)resolved_ip_address;
      target_hostname = NULL;
      return MDNS_QUERY_SUCCESS;
    }
    *ip_addr_out = 0;
    target_hostname = NULL;
    return MDNS_QUERY_FAIL;
  }
  return MDNS_QUERY_PENDING;
}

} // extern "C"

#else

// Stubs when DWC_MACHINE_MODE is not set — fast no-op implementations that
// keep behaviour predictable for callers.

extern "C" {

void mdns_wrapper_init() {
  // No-op
}

bool mdns_wrapper_query_host(const char* host, uint32_t* ip_addr_out) {
  (void)host; (void)ip_addr_out; return false;
}

mdns_query_status_t mdns_wrapper_run(uint32_t* ip_addr_out) {
  (void)ip_addr_out; return MDNS_QUERY_FAIL;
}

} // extern "C"

#endif
