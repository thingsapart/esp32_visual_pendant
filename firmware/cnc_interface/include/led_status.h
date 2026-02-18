#pragma once

#ifdef ESP32_HW

#include <stdint.h>

/*
 * LED Status Indicator Controller for ESP32-S3 Hub
 * 
 * Controls RGB LEDs to show system state:
 * - Blue blinking: Connecting to UART
 * - Green: Connected and idle
 * - Blue flash: Sending data
 * - Red blinking: Error state
 */

typedef enum {
  LED_STATE_INIT,           // Initial state (off)
  LED_STATE_CONNECTING,     // Connecting to UART (blue blink)
  LED_STATE_CONNECTED,      // Connected to machine (green solid)
  LED_STATE_SENDING,        // Sending data (blue flash)
  LED_STATE_ERROR,          // Error state (red blink)
} led_state_t;

// Configuration for ESP32-S3-DevKit RGB LEDs
// Standard ESP32-S3-DevKit-C-1 pin assignments
#ifndef LED_RED_PIN
  #define LED_RED_PIN 46    // GPIO 46 - Red LED
#endif

#ifndef LED_GREEN_PIN
  #define LED_GREEN_PIN 0   // GPIO 0 - Green LED
#endif

#ifndef LED_BLUE_PIN
  #define LED_BLUE_PIN 1    // GPIO 1 - Blue LED
#endif

// LED Control Functions
void led_status_init();
void led_status_set_state(led_state_t state);
void led_status_task_update();

// Convenience functions for specific states
void led_status_connecting();
void led_status_connected();
void led_status_sending();
void led_status_error();

#endif
