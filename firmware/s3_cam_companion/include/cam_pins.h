// cam_pins.h — Default GPIO assignments for GOOUUU ESP32-S3-CAM
//
// All pins are overridable via -D build flags in platformio.ini.
// This file provides fallback defaults only.

#ifndef CAM_PINS_H
#define CAM_PINS_H

// ---- OV3660 DVP camera interface ----
#ifndef CAM_PIN_PWDN
#define CAM_PIN_PWDN   -1
#endif
#ifndef CAM_PIN_RESET
#define CAM_PIN_RESET  -1
#endif
#ifndef CAM_PIN_XCLK
#define CAM_PIN_XCLK   15
#endif
#ifndef CAM_PIN_SIOD
#define CAM_PIN_SIOD   4
#endif
#ifndef CAM_PIN_SIOC
#define CAM_PIN_SIOC   5
#endif
#ifndef CAM_PIN_Y2
#define CAM_PIN_Y2     11
#endif
#ifndef CAM_PIN_Y3
#define CAM_PIN_Y3     9
#endif
#ifndef CAM_PIN_Y4
#define CAM_PIN_Y4     8
#endif
#ifndef CAM_PIN_Y5
#define CAM_PIN_Y5     10
#endif
#ifndef CAM_PIN_Y6
#define CAM_PIN_Y6     12
#endif
#ifndef CAM_PIN_Y7
#define CAM_PIN_Y7     18
#endif
#ifndef CAM_PIN_Y8
#define CAM_PIN_Y8     17
#endif
#ifndef CAM_PIN_Y9
#define CAM_PIN_Y9     16
#endif
#ifndef CAM_PIN_VSYNC
#define CAM_PIN_VSYNC  6
#endif
#ifndef CAM_PIN_HREF
#define CAM_PIN_HREF   7
#endif
#ifndef CAM_PIN_PCLK
#define CAM_PIN_PCLK   13
#endif

// ---- NeoPixel / status LED ----
#ifndef CAM_LED_PIN
#define CAM_LED_PIN    48
#endif

// ---- BOOT button (config mode trigger) ----
#ifndef CAM_BOOT_PIN
#define CAM_BOOT_PIN   0
#endif

// ---- ESP-NOW default channel ----
#ifndef CAM_WIFI_CHANNEL
#define CAM_WIFI_CHANNEL  1
#endif

// ---- XCLK frequency (20 MHz is standard for OV3660) ----
#ifndef CAM_XCLK_FREQ
#define CAM_XCLK_FREQ  20000000
#endif

#endif // CAM_PINS_H
