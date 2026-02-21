# ESP32-S3 Camera Companion

Camera module firmware for the CNC Visual Pendant system.  Captures images from
an OV3660 DVP camera, applies perspective correction (homography), performs
tile-based differential compression, and streams changed tiles as JPEG over
ESP-NOW to the pendant display.

## Hardware

- **Board**: GOOUUU ESP32-S3-CAM (or compatible ESP32-S3-DevKitC-1 with OV3660)
- **Camera**: OV3660 DVP (parallel interface)
- **Flash**: 8 MB QIO
- **PSRAM**: 8 MB OPI
- **LED**: WS2812 NeoPixel on GPIO48

## Build

```bash
cd firmware/s3_cam_companion
pio run -e cam-goouuu-s3
pio run -e cam-goouuu-s3 -t upload
```

## Boot Modes

| Condition | Mode | Description |
|-----------|------|-------------|
| BOOT button held during power-on | **Config Mode** | Starts WiFi AP ("CamCompanion"), serves web UI at `192.168.4.1` for calibration and settings. |
| Normal boot, no calibration saved | **Config Mode** | Same as above — forces configuration on first use. |
| Normal boot, calibration saved | **Camera Mode** | Captures frames, applies transform, sends diffs via ESP-NOW. |

## Architecture

```
┌──────────────────────────────────────────────────┐
│                  Camera Pipeline                  │
│                                                  │
│  OV3660 DVP ──→ RGB565 frame (PSRAM)             │
│       ↓                                          │
│  Homography LUT ──→ Warped RGB565 (PSRAM)        │
│       ↓                                          │
│  Tile Diff ──→ Changed tiles identified           │
│       ↓                                          │
│  Per-tile JPEG encode ──→ Compact JPEG chunks     │
│       ↓                                          │
│  ESP-NOW send (rate-limited) ──→ Pendant         │
└──────────────────────────────────────────────────┘
```

### Frame Protocol (cam_protocol.h)

All messages use the `0x80–0x9F` type range to coexist with hub CNC messages.

| Direction | Type | Description |
|-----------|------|-------------|
| Cam → Pendant | `CAM_MSG_FRAME_START` | Frame metadata + changed-tile bitmap |
| Cam → Pendant | `CAM_MSG_TILE_CHUNK` | JPEG tile data chunk |
| Cam → Pendant | `CAM_MSG_FRAME_END` | End-of-frame marker |
| Cam → Pendant | `CAM_MSG_STATUS` | Heartbeat with FPS, channel, MAC |
| Pendant → Cam | `CAM_CMD_REQUEST_FRAME` | Poll for next frame |
| Pendant → Cam | `CAM_CMD_SET_CONFIG` | Push settings |
| Pendant → Cam | `CAM_CMD_FORCE_KEYFRAME` | Request full refresh |

### Tile Grid

The output image is divided into an N×M grid of tiles (default 4×4).
Each tile is independently JPEG-encoded.  Only tiles that differ from the
previous frame are transmitted (diff frames).  A full keyframe with all
tiles is sent periodically.

### Rate Limiting

Frame delivery is **poll-driven**: the pendant requests frames one at a time.
Between ESP-NOW chunk sends, the camera inserts a configurable delay
(`send_interval_ms`) to avoid starving the hub's CNC data channel.

## Pin Configuration

All camera DVP pins are configurable via `-D` build flags.  Defaults match
the common Freenove / GOOUUU ESP32-S3-CAM pinout.  See `include/cam_pins.h`.

## Web UI

In config mode, connect to the "CamCompanion" WiFi network and navigate to
`http://192.168.4.1`.  The web interface provides:

- **Live snapshot** viewer
- **4-corner calibration** — click the bed corners on the snapshot
- **Camera settings** — resolution, JPEG quality, tile grid, diff threshold
- **Sensor settings** — brightness, contrast, auto/manual exposure and gain
- **Save & Reboot** — persists settings to NVS and restarts in camera mode

## Shared Code

The protocol header `include/cam_protocol.h` is symlinked into
`cnc_interface/include/` so the pendant firmware can decode camera messages
without code duplication.

## Files

| File | Description |
|------|-------------|
| `src/main.cpp` | Entry point, boot mode detection, pipeline orchestration |
| `src/cam_capture.h/.cpp` | OV3660 DVP camera init and capture |
| `src/cam_transform.h/.cpp` | Homography LUT build and apply |
| `src/cam_diff.h/.cpp` | Tile-based frame diff + JPEG encoding |
| `src/cam_espnow.h/.cpp` | ESP-NOW init, peer management, frame sending |
| `src/cam_led.h/.cpp` | WS2812 NeoPixel status animations |
| `src/cam_webserver.h/.cpp` | AP mode web server + embedded HTML UI |
| `src/cam_settings.h/.cpp` | NVS persistence, homography computation |
| `include/cam_protocol.h` | Shared protocol definitions (symlinked) |
| `include/cam_pins.h` | GPIO pin defaults |
