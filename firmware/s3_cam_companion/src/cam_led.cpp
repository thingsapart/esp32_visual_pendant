// cam_led.cpp — WS2812 NeoPixel status animation

#include "cam_led.h"
#include "cam_pins.h"
#include <Adafruit_NeoPixel.h>

// Single NeoPixel on CAM_LED_PIN.
static Adafruit_NeoPixel pixel(1, CAM_LED_PIN, NEO_GRB + NEO_KHZ800);

static cam_led_state_t s_state   = CAM_LED_OFF;
static uint16_t        s_counter = 0;  // Tick counter for animations
static const uint8_t   MAX_BRIGHT = 40;  // Keep dim to avoid glare

// ---------------------------------------------------------------------------
// Helper: set pixel and show
// ---------------------------------------------------------------------------
static void set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    static uint8_t last_r = 0, last_g = 0, last_b = 0;
    if (r == last_r && g == last_g && b == last_b) return;
    last_r = r; last_g = g; last_b = b;
    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}

// Breathe function: 0.0 → 1.0 → 0.0 over `period` ticks.
static uint8_t breathe(uint16_t tick, uint16_t period) {
    uint16_t phase = tick % period;
    uint16_t half  = period / 2;
    float v;
    if (phase < half)
        v = (float)phase / (float)half;
    else
        v = (float)(period - phase) / (float)half;
    return (uint8_t)(v * MAX_BRIGHT);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void cam_led_init(void) {
    pixel.begin();
    pixel.setBrightness(MAX_BRIGHT);
    set_rgb(0, 0, 0);
}

void cam_led_set(cam_led_state_t state) {
    if (state != s_state) {
        s_state   = state;
        s_counter = 0;
    }
}

void cam_led_tick(void) {
    s_counter++;

    switch (s_state) {
    case CAM_LED_OFF:
        set_rgb(0, 0, 0);
        break;

    case CAM_LED_BOOT: {
        // White pulse over ~1 second (50 ticks at 20ms)
        uint8_t v = breathe(s_counter, 50);
        set_rgb(v, v, v);
        break;
    }

    case CAM_LED_CONFIG_MODE: {
        // Slow cyan breathe (~3 seconds)
        uint8_t v = breathe(s_counter, 150);
        set_rgb(0, v, v);
        break;
    }

    case CAM_LED_IDLE:
        // Dim solid green
        set_rgb(0, MAX_BRIGHT / 4, 0);
        break;

    case CAM_LED_CAPTURING:
        // Blue flash for 5 ticks then dim green
        if (s_counter < 5)
            set_rgb(0, 0, MAX_BRIGHT);
        else
            set_rgb(0, MAX_BRIGHT / 4, 0);  // Return to idle colour
        break;

    case CAM_LED_SENDING: {
        // Purple pulse while sending
        uint8_t v = breathe(s_counter, 20);
        set_rgb(v, 0, v);
        break;
    }

    case CAM_LED_ERROR: {
        // Red blink 500ms on / 500ms off  (25 ticks each at 20ms)
        bool on = (s_counter % 50) < 25;
        set_rgb(on ? MAX_BRIGHT : 0, 0, 0);
        break;
    }

    case CAM_LED_NO_PEER: {
        // Yellow blink ~1Hz
        bool on = (s_counter % 50) < 25;
        set_rgb(on ? MAX_BRIGHT : 0, on ? MAX_BRIGHT / 2 : 0, 0);
        break;
    }

    case CAM_LED_FACTORY_RESET: {
        // Fast orange blink ~5 Hz (4 ticks on / 4 off at 50 ms per tick).
        // Visually distinct from all other states — a clear hold warning.
        bool on = (s_counter % 8) < 4;
        set_rgb(on ? MAX_BRIGHT : 0, on ? MAX_BRIGHT / 4 : 0, 0);
        break;
    }
    }
}
