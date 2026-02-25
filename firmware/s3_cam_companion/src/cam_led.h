// cam_led.h — WS2812 NeoPixel status indicator

#ifndef CAM_LED_H
#define CAM_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAM_LED_OFF,           // LED off
    CAM_LED_BOOT,          // White pulse — booting
    CAM_LED_CONFIG_MODE,   // Slow cyan breathe — AP config mode
    CAM_LED_IDLE,          // Solid dim green — connected, waiting for request
    CAM_LED_CAPTURING,     // Blue flash — capturing frame
    CAM_LED_SENDING,       // Purple flash — sending chunks
    CAM_LED_ERROR,         // Red blink — error
    CAM_LED_NO_PEER,       // Yellow blink — no pendant paired
    CAM_LED_FACTORY_RESET, // Fast orange blink — factory reset imminent
} cam_led_state_t;

// Initialise the NeoPixel on CAM_LED_PIN.
void cam_led_init(void);

// Set the current LED state.  The actual animation is advanced by cam_led_tick().
void cam_led_set(cam_led_state_t state);

// Call periodically (~20–50 ms) to advance blink/breathe animations.
void cam_led_tick(void);

#ifdef __cplusplus
}
#endif

#endif // CAM_LED_H
