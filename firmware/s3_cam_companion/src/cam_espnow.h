// cam_espnow.h — ESP-NOW initialisation, rate-limited frame chunk sending
//
// Handles Wi-Fi STA mode + ESP-NOW coexistence.  Sends camera frame
// chunks with configurable inter-packet delay to avoid swamping the
// hub's CNC data channel.

#ifndef CAM_ESPNOW_H
#define CAM_ESPNOW_H

#include <stdint.h>
#include <stdbool.h>
#include "cam_protocol.h"
#include "cam_diff.h"

#ifdef __cplusplus
extern "C" {
#endif

// Callback: peer sent us a message (pendant request, config, etc.).
typedef void (*cam_espnow_recv_cb_t)(const uint8_t *mac,
                                     const uint8_t *data, int len);

// Initialise Wi-Fi (STA) + ESP-NOW on the given channel.
// recv_cb is called from the ESP-NOW receive ISR context — keep it minimal.
bool cam_espnow_init(uint8_t wifi_channel, cam_espnow_recv_cb_t recv_cb);

// De-initialise.
void cam_espnow_deinit(void);

// Add the pendant/hub as a known ESP-NOW peer.
bool cam_espnow_add_peer(const uint8_t *mac);

// Auto-learn peer from received MAC (if no peer known yet).
bool cam_espnow_learn_peer(const uint8_t *received_mac, uint8_t *stored_mac);

// Send raw data to a specific peer (or broadcast if mac == NULL).
bool cam_espnow_send(const uint8_t *mac, const uint8_t *data, size_t len);

// Send a complete diff result as a series of frame_start + tile_chunks + frame_end.
// frame_id is incremented by the caller.
// send_interval_ms: delay between each ESP-NOW packet.
// Returns true if all chunks were sent successfully.
bool cam_espnow_send_frame(const uint8_t *peer_mac,
                           uint16_t frame_id,
                           const cam_diff_result_t *diff,
                           uint8_t send_interval_ms);

// Send a camera status heartbeat.
bool cam_espnow_send_status(const uint8_t *peer_mac,
                            cam_device_status_t status,
                            uint16_t frame_id,
                            uint8_t fps_x10);

// Get our own MAC address.
void cam_espnow_get_mac(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif // CAM_ESPNOW_H
