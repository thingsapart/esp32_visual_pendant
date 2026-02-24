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

// Broadcast grid mapping. points: array of normalized (x,y) pairs, count = nx*ny
// Returns false if payload too large or send failed.
bool cam_espnow_send_grid(const uint8_t *peer_mac,
                          float minx, float maxx, float miny, float maxy,
                          float dx, float dy,
                          uint16_t nx, uint16_t ny,
                          const float *points_xy, uint16_t points_count);

// Compact grid send: packs surface width/height and spacing as four float32
// (w,h,dx,dy) followed by u16 nx, u16 ny, u16 points_count, u16 img_w,
// u16 img_h, u16 inset_left, u16 inset_top, u16 inset_right, u16 inset_bottom,
// and then points_count pairs of signed int8 offsets (x_off, y_off).
// The offsets are raw pixel differences: actual_image_px - ideal_image_px,
// clamped to int8_t range.
// img_w / img_h are the dimensions of the image when calibration was performed,
// allowing the receiver to reconstruct exact pixel positions.
bool cam_espnow_send_grid_compact(const uint8_t *peer_mac,
                                  float w, float h, float dx, float dy,
                                  uint16_t nx, uint16_t ny,
                                  uint16_t img_w, uint16_t img_h,
                                  uint16_t inset_l, uint16_t inset_t,
                                  uint16_t inset_r, uint16_t inset_b,
                                  const int8_t *offsets_xy, uint16_t points_count);

// Get our own MAC address.
void cam_espnow_get_mac(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif // CAM_ESPNOW_H
