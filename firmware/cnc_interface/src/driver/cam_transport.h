// cam_transport.h — Abstract transport layer for camera data
//
// Provides a pluggable interface between the camera stream receiver and the
// underlying communication channel (ESP-NOW, HTTP, etc.).  Concrete
// implementations live in their own translation units.

#ifndef CAM_TRANSPORT_H
#define CAM_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Callback type: invoked on every raw message received from the camera.
// |data| points to the cam-protocol payload (first byte is the message type).
// Called from the transport's internal context — keep the handler fast.
// ---------------------------------------------------------------------------
typedef void (*cam_transport_rx_cb_t)(const uint8_t *data, size_t len, void *user_data);

// ---------------------------------------------------------------------------
// Abstract transport interface (C "virtual class" via function pointers).
// Implementations embed this struct and set the function pointers.
// ---------------------------------------------------------------------------
typedef struct cam_transport {
    /// Start the transport (begin listening / polling).
    /// Returns 0 on success, negative on error.
    int  (*start)(struct cam_transport *self);

    /// Stop the transport (cease listening, release channel resources).
    void (*stop)(struct cam_transport *self);

    /// Send a raw cam-protocol command to the camera.
    /// |data| includes the message-type byte as the first byte.
    /// Returns 0 on success, negative on error.
    int  (*send)(struct cam_transport *self, const uint8_t *data, size_t len);

    /// Register (or replace) the receive callback.
    /// Pass cb=NULL to unregister.
    void (*set_rx_callback)(struct cam_transport *self,
                            cam_transport_rx_cb_t cb, void *user_data);

    /// Tear down the transport and free all associated memory.
    /// After this call, |self| is invalid.
    void (*destroy)(struct cam_transport *self);
} cam_transport_t;

// ---------------------------------------------------------------------------
// Convenience inline helpers (NULL-safe)
// ---------------------------------------------------------------------------
static inline int cam_transport_start(cam_transport_t *t)
{
    return (t && t->start) ? t->start(t) : -1;
}

static inline void cam_transport_stop(cam_transport_t *t)
{
    if (t && t->stop) t->stop(t);
}

static inline int cam_transport_send(cam_transport_t *t, const uint8_t *data, size_t len)
{
    return (t && t->send) ? t->send(t, data, len) : -1;
}

static inline void cam_transport_set_rx_callback(cam_transport_t *t,
                                                  cam_transport_rx_cb_t cb,
                                                  void *user_data)
{
    if (t && t->set_rx_callback) t->set_rx_callback(t, cb, user_data);
}

static inline void cam_transport_destroy(cam_transport_t *t)
{
    if (t && t->destroy) t->destroy(t);
}

#ifdef __cplusplus
}
#endif
#endif // CAM_TRANSPORT_H
