// cam_webserver.h — Configuration AP + web server
//
// When BOOT button is held at power-on (or on first boot with no saved config),
// the camera starts in AP mode ("CamCompanion" SSID) and serves a web UI
// for configuring camera settings, calibrating the perspective transform,
// and viewing live snapshots.

#ifndef CAM_WEBSERVER_H
#define CAM_WEBSERVER_H

#include <stdbool.h>
#include "cam_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the AP and web server.  Blocks the calling task until the user
// saves settings and reboots, or the timeout expires.
// settings: live settings struct (modified in-place by the web UI).
void cam_webserver_start(cam_settings_t *settings);

// Stop the web server and AP.
void cam_webserver_stop(void);

// Is the web server currently running?
bool cam_webserver_is_running(void);

// Must be called repeatedly from loop() to handle HTTP clients.
void cam_webserver_handle(void);

#ifdef __cplusplus
}
#endif

#endif // CAM_WEBSERVER_H
