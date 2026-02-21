// cam_protocol.h — Camera Companion ↔ Pendant ESP-NOW protocol definitions
//
// This header is shared between the camera companion and the pendant firmware.
// It defines message types, structs, and constants for camera image streaming
// over ESP-NOW.  It has NO dependencies on machine_interface.h or any display
// code — keep it self-contained.
//
// Symlink this file into the pendant project's include path so both sides
// stay in sync automatically.

#ifndef CAM_PROTOCOL_H
#define CAM_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// ESP-NOW limits
// ---------------------------------------------------------------------------
#define CAM_ESPNOW_MAX_DATA   250   // ESP-NOW max payload
#define CAM_ESPNOW_MTU        (CAM_ESPNOW_MAX_DATA)

// ---------------------------------------------------------------------------
// Message type IDs — Camera ↔ Pendant
//
// These occupy a separate namespace from the hub's remote_message_type_t to
// avoid collisions.  The first byte of every ESP-NOW frame from the camera
// carries one of these values.  Values start at 0x80 so they can coexist
// on the same ESP-NOW link if needed (hub uses 0x00–0x1F).
// ---------------------------------------------------------------------------

typedef enum {
    // Camera → Pendant
    CAM_MSG_FRAME_START  = 0x80,  // Frame metadata + changed-tile bitmap
    CAM_MSG_TILE_CHUNK   = 0x81,  // JPEG tile data chunk
    CAM_MSG_FRAME_END    = 0x82,  // Signals all tiles for this frame sent
    CAM_MSG_STATUS       = 0x83,  // Camera status / heartbeat

    // Pendant → Camera
    CAM_CMD_REQUEST_FRAME  = 0x90,  // Poll for next frame
    CAM_CMD_SET_CONFIG     = 0x91,  // Push configuration to camera
    CAM_CMD_FORCE_KEYFRAME = 0x92,  // Request a full keyframe now
} cam_msg_type_t;

// ---------------------------------------------------------------------------
// Frame types
// ---------------------------------------------------------------------------
typedef enum {
    CAM_FRAME_KEYFRAME = 0,   // Full image — all tiles present
    CAM_FRAME_DIFF     = 1,   // Only changed tiles present
} cam_frame_type_t;

// ---------------------------------------------------------------------------
// Encoding types for tile data
// ---------------------------------------------------------------------------
typedef enum {
    CAM_ENC_JPEG    = 0,   // JPEG-compressed tile
    CAM_ENC_RGB565  = 1,   // Raw RGB565 pixels (uncompressed)
} cam_encoding_t;

// ---------------------------------------------------------------------------
// Resolution presets
// ---------------------------------------------------------------------------
typedef enum {
    CAM_RES_QVGA  = 0,   // 320×240
    CAM_RES_VGA   = 1,   // 640×480
    CAM_RES_SVGA  = 2,   // 800×600
} cam_resolution_t;

// ---------------------------------------------------------------------------
// Camera → Pendant: frame metadata (sent once at the start of each frame)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;           // CAM_MSG_FRAME_START
    uint16_t frame_id;       // Wrapping frame sequence number
    uint8_t  frame_type;     // cam_frame_type_t (KEYFRAME / DIFF)
    uint16_t img_width;      // Output image width in pixels
    uint16_t img_height;     // Output image height in pixels
    uint8_t  tiles_x;        // Tile grid columns
    uint8_t  tiles_y;        // Tile grid rows
    uint8_t  encoding;       // cam_encoding_t
    uint8_t  jpeg_quality;   // 1–63 (if JPEG)
    uint8_t  num_tiles;      // Number of tiles in this frame update
    // Variable-length bitmap: ceil(tiles_x * tiles_y / 8) bytes
    // Bit N = 1 means tile N is included in this frame.
    uint8_t  tile_bitmap[];
} cam_frame_start_msg_t;

#define CAM_FRAME_START_HEADER_SIZE  (offsetof(cam_frame_start_msg_t, tile_bitmap))

// ---------------------------------------------------------------------------
// Camera → Pendant: tile JPEG/RGB565 data chunk
//
// A single tile may span multiple chunks (if JPEG > ~240 bytes).
// The pendant reassembles tiles by (frame_id, tile_idx, chunk_idx).
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;           // CAM_MSG_TILE_CHUNK
    uint16_t frame_id;       // Must match the frame_start
    uint8_t  tile_idx;       // 0-based tile index (row-major)
    uint8_t  chunk_idx;      // 0-based chunk within this tile
    uint8_t  total_chunks;   // Total chunks for this tile
    uint16_t tile_data_len;  // Total byte length of this tile's JPEG/raw data
    uint8_t  data[];         // Chunk payload (up to MTU - header)
} cam_tile_chunk_msg_t;

#define CAM_TILE_CHUNK_HEADER_SIZE  (offsetof(cam_tile_chunk_msg_t, data))
#define CAM_TILE_CHUNK_MAX_PAYLOAD  (CAM_ESPNOW_MTU - CAM_TILE_CHUNK_HEADER_SIZE)

// ---------------------------------------------------------------------------
// Camera → Pendant: end-of-frame marker
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;           // CAM_MSG_FRAME_END
    uint16_t frame_id;
    uint8_t  tiles_sent;     // How many tiles were actually sent
} cam_frame_end_msg_t;

// ---------------------------------------------------------------------------
// Camera → Pendant: camera status / heartbeat
// ---------------------------------------------------------------------------
typedef enum {
    CAM_STATUS_IDLE          = 0,
    CAM_STATUS_CAPTURING     = 1,
    CAM_STATUS_SENDING       = 2,
    CAM_STATUS_CONFIG_MODE   = 3,
    CAM_STATUS_ERROR         = 4,
} cam_device_status_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;           // CAM_MSG_STATUS
    uint8_t  status;         // cam_device_status_t
    uint16_t frame_id;       // Last completed frame
    uint8_t  fps_x10;       // Approx FPS × 10 (e.g. 15 = 1.5 fps)
    uint8_t  wifi_channel;   // Current Wi-Fi channel
    uint8_t  mac[6];         // Camera's own MAC
} cam_status_msg_t;

// ---------------------------------------------------------------------------
// Pendant → Camera: request next frame
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;           // CAM_CMD_REQUEST_FRAME
    uint16_t last_frame_id;  // Last frame the pendant fully received
    uint8_t  flags;          // bit 0: force keyframe
} cam_request_frame_cmd_t;

// ---------------------------------------------------------------------------
// Pendant → Camera: push configuration
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;           // CAM_CMD_SET_CONFIG
    uint8_t  resolution;     // cam_resolution_t
    uint8_t  jpeg_quality;   // 1–63
    uint8_t  tiles_x;        // Desired tile columns
    uint8_t  tiles_y;        // Desired tile rows
    uint8_t  diff_threshold; // Block diff sensitivity (0–255)
    uint8_t  keyframe_interval; // Send keyframe every N frames (0=auto)
    int8_t   brightness;     // -2..+2
    int8_t   contrast;       // -2..+2
    uint8_t  aec_enable;     // Auto-exposure  (1=on, 0=off)
    uint8_t  agc_enable;     // Auto-gain      (1=on, 0=off)
    int16_t  aec_value;      // Manual exposure value (when AEC off)
    uint8_t  agc_gain;       // Manual gain ceiling  (when AGC off)
    float    homography[9];  // 3×3 row-major perspective matrix
} cam_config_cmd_t;

// ---------------------------------------------------------------------------
// Pendant → Camera: force keyframe (shorthand)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;           // CAM_CMD_FORCE_KEYFRAME
} cam_force_keyframe_cmd_t;

// ---------------------------------------------------------------------------
// Default configuration values
// ---------------------------------------------------------------------------
#define CAM_DEFAULT_RESOLUTION       CAM_RES_VGA
#define CAM_DEFAULT_JPEG_QUALITY     12
#define CAM_DEFAULT_TILES_X          4
#define CAM_DEFAULT_TILES_Y          4
#define CAM_DEFAULT_DIFF_THRESHOLD   15
#define CAM_DEFAULT_KEYFRAME_INTERVAL 30  // Every 30 frames
#define CAM_DEFAULT_SEND_INTERVAL_MS  8   // ms between ESP-NOW chunk sends
#define CAM_DEFAULT_FRAME_TIMEOUT_MS  3000 // ms before pendant drops incomplete frame

// ---------------------------------------------------------------------------
// Tile geometry helpers
// ---------------------------------------------------------------------------
static inline uint16_t cam_tile_width(uint16_t img_w, uint8_t tiles_x) {
    return img_w / tiles_x;
}
static inline uint16_t cam_tile_height(uint16_t img_h, uint8_t tiles_y) {
    return img_h / tiles_y;
}
static inline uint8_t cam_tile_bitmap_bytes(uint8_t tiles_x, uint8_t tiles_y) {
    return (uint8_t)((tiles_x * tiles_y + 7) / 8);
}

#ifdef __cplusplus
}
#endif

#endif // CAM_PROTOCOL_H
