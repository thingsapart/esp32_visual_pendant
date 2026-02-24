// cam_transport_espnow.h — ESP-NOW transport for camera streaming
//
// This transport delivers raw cam-protocol messages that arrive over ESP-NOW
// (either directly or through the C6-bridge UART link on ESP32-P4 boards).
//
// Integration:  Whoever owns the low-level ESP-NOW / bridge receive path must
// call  cam_transport_espnow_feed()  when a packet from the camera's MAC
// arrives.  The transport will invoke the registered rx-callback.

#ifndef CAM_TRANSPORT_ESPNOW_H
#define CAM_TRANSPORT_ESPNOW_H

#include "cam_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Create an ESP-NOW transport instance.
/// |cam_mac| is the 6-byte MAC address of the camera companion board.
/// Returns NULL on allocation failure.
cam_transport_t *cam_transport_espnow_create(const uint8_t cam_mac[6]);

/// Feed a raw ESP-NOW payload that was received from the camera.
/// This is the integration point: call from the ESP-NOW recv callback or from
/// the bridge-UART frame parser after verifying the source MAC.
/// |data|/|len| is the ESP-NOW payload (cam-protocol message).
///
/// May be called from ISR context or any task — the implementation must be
/// safe for that.
void cam_transport_espnow_feed(cam_transport_t *self,
                                const uint8_t *data, size_t len);

/// Return the 6-byte camera MAC stored in this transport instance.
const uint8_t *cam_transport_espnow_get_mac(const cam_transport_t *self);

#ifdef __cplusplus
}
#endif
#endif // CAM_TRANSPORT_ESPNOW_H
