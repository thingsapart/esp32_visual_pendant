// cam_transport_espnow.c — ESP-NOW camera transport implementation

#define UI_DEBUG_LOCAL_LEVEL D_DEBUG
#include "debug.h"

#include "cam_transport_espnow.h"
#include "cam_protocol.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_now.h"
#include "remote_comms_wrapper.h"
#endif

static const char *TAG = "cam_xport";

#if defined(ESP32P4_HW)
// ESP32-P4 does not support ESP-NOW in this build; provide a stubbed create
// function so callers gracefully detect absence of ESP-NOW support.
cam_transport_t *cam_transport_espnow_create(const uint8_t cam_mac[6])
{
    LOGW(TAG, "ESP-NOW disabled on ESP32-P4");
    (void)cam_mac;
    return NULL;
}

#else

// ---------------------------------------------------------------------------
// Private state
// ---------------------------------------------------------------------------
typedef struct {
    cam_transport_t     base;           // Must be first (for up-cast)
    uint8_t             cam_mac[6];
    cam_transport_rx_cb_t rx_cb;
    void               *rx_user_data;
    bool                running;
} cam_transport_espnow_t;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static cam_transport_espnow_t *to_espnow(cam_transport_t *t)
{
    return (cam_transport_espnow_t *)t;
}

// ---------------------------------------------------------------------------
// Wrapper recv callback — filters by camera MAC, feeds the transport
// ---------------------------------------------------------------------------
#ifdef ESP_PLATFORM
static void espnow_wrapper_recv_cb(const uint8_t *mac_addr,
                                    const uint8_t *data, int data_len,
                                    void *user_data)
{
    cam_transport_espnow_t *ctx = (cam_transport_espnow_t *)user_data;
    if (!ctx->running) return;

    // If cam_mac is all-zeros we are in auto-discovery mode: accept the first
    // ESP-NOW packet that looks like a cam-protocol message (first byte in the
    // 0x80–0x8F range) and lock onto that sender's MAC.
    static const uint8_t zero_mac[6] = {0};
    bool mac_unknown = (memcmp(ctx->cam_mac, zero_mac, 6) == 0);
    if (mac_unknown) {
        if (data_len < 1 || (data[0] & 0xF0) != 0x80) return; // not cam proto
        memcpy(ctx->cam_mac, mac_addr, 6);
        LOGI(TAG, "Camera discovered: MAC %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);
        // Register newly discovered camera as an ESP-NOW peer so we can
        // send request_frame / force_keyframe commands back to it.
        esp_now_peer_info_t peer = {0};
        memcpy(peer.peer_addr, mac_addr, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);  // Ignore "already exists" error
    } else {
        if (memcmp(mac_addr, ctx->cam_mac, 6) != 0) return;  // not from camera
    }

    cam_transport_espnow_feed(&ctx->base, data, (size_t)data_len);
}
#endif

// ---------------------------------------------------------------------------
// Interface: start / stop
// ---------------------------------------------------------------------------
static int espnow_start(cam_transport_t *self)
{
    cam_transport_espnow_t *ctx = to_espnow(self);
    if (ctx->running) return 0;

#ifdef ESP_PLATFORM
    // Register the camera as an ESP-NOW peer if the MAC is already known.
    // When MAC is all-zeros we are in auto-discovery mode; the peer will be
    // added (below) once the first cam-protocol packet arrives.
    static const uint8_t zero_mac[6] = {0};
    if (memcmp(ctx->cam_mac, zero_mac, 6) != 0) {
        esp_now_peer_info_t peer = {0};
        memcpy(peer.peer_addr, ctx->cam_mac, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);  // Ignore "already exists" error
    }

    // Register with the wrapper so camera packets are fed directly to us
    // without going through machine_remote.
    if (!remote_wrapper_add_recv_cb(espnow_wrapper_recv_cb, ctx)) {
        LOGW(TAG, "Failed to add recv callback — camera packets may be missed");
    }
#endif

    ctx->running = true;
    LOGI(TAG, "ESP-NOW camera transport started (MAC %02X:%02X:%02X:%02X:%02X:%02X)",
         ctx->cam_mac[0], ctx->cam_mac[1], ctx->cam_mac[2],
         ctx->cam_mac[3], ctx->cam_mac[4], ctx->cam_mac[5]);
    return 0;
}

static void espnow_stop(cam_transport_t *self)
{
    cam_transport_espnow_t *ctx = to_espnow(self);
    if (!ctx->running) return;
    ctx->running = false;
#ifdef ESP_PLATFORM
    remote_wrapper_remove_recv_cb(espnow_wrapper_recv_cb);
#endif
    LOGI(TAG, "ESP-NOW camera transport stopped");
}

// ---------------------------------------------------------------------------
// Interface: send
// ---------------------------------------------------------------------------
static int espnow_send(cam_transport_t *self, const uint8_t *data, size_t len)
{
    cam_transport_espnow_t *ctx = to_espnow(self);
    if (!ctx->running) return -1;

#ifdef ESP_PLATFORM
    // Don't send if camera MAC has not been discovered yet (all-zeros = unknown).
    // The auto-discovery path in espnow_wrapper_recv_cb will fill cam_mac on
    // the first STATUS heartbeat from the companion; until that happens, any
    // esp_now_send to 00:00:00:00:00:00 returns ESP_ERR_ESPNOW_NOT_FOUND.
    static const uint8_t zero_mac[6] = {0};
    if (memcmp(ctx->cam_mac, zero_mac, 6) == 0) return -1;

    // Use the remote comms wrapper to send via ESP-NOW (or bridge on P4).
    return remote_wrapper_send(ctx->cam_mac, data, (uint16_t)len);
#else
    LOGW(TAG, "send not implemented on this platform");
    (void)data; (void)len;
    return -1;
#endif
}

// ---------------------------------------------------------------------------
// Interface: set_rx_callback
// ---------------------------------------------------------------------------
static void espnow_set_rx_callback(cam_transport_t *self,
                                    cam_transport_rx_cb_t cb, void *user_data)
{
    cam_transport_espnow_t *ctx = to_espnow(self);
    ctx->rx_cb        = cb;
    ctx->rx_user_data = user_data;
}

// ---------------------------------------------------------------------------
// Interface: destroy
// ---------------------------------------------------------------------------
static void espnow_destroy(cam_transport_t *self)
{
    cam_transport_espnow_t *ctx = to_espnow(self);
    ctx->running = false;
    free(ctx);
}

// ---------------------------------------------------------------------------
// Public: create
// ---------------------------------------------------------------------------
cam_transport_t *cam_transport_espnow_create(const uint8_t cam_mac[6])
{
    cam_transport_espnow_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    memcpy(ctx->cam_mac, cam_mac, 6);

    ctx->base.start           = espnow_start;
    ctx->base.stop            = espnow_stop;
    ctx->base.send            = espnow_send;
    ctx->base.set_rx_callback = espnow_set_rx_callback;
    ctx->base.destroy         = espnow_destroy;

    return &ctx->base;
}

// ---------------------------------------------------------------------------
// Public: feed (called from whichever layer receives ESP-NOW data)
// ---------------------------------------------------------------------------
void cam_transport_espnow_feed(cam_transport_t *self,
                                const uint8_t *data, size_t len)
{
    cam_transport_espnow_t *ctx = to_espnow(self);
    if (!ctx->running || !ctx->rx_cb || len == 0) return;
    ctx->rx_cb(data, len, ctx->rx_user_data);
}

// ---------------------------------------------------------------------------
// Public: get camera MAC
// ---------------------------------------------------------------------------
const uint8_t *cam_transport_espnow_get_mac(const cam_transport_t *self)
{
    return ((const cam_transport_espnow_t *)self)->cam_mac;
}

#endif // ESP32P4_HW
