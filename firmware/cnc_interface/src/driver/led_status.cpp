#include "led_status.h"

#ifdef ESP32_HW

#include "Arduino.h"
#include "debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "led_status";

// LED state tracking
static led_state_t current_state = LED_STATE_INIT;
static uint32_t blink_counter = 0;
static const uint32_t BLINK_PERIOD = 10;  // Update every BLINK_PERIOD ticks
static const uint32_t BLINK_ON_TIME = 5;  // Blink on for 5 ticks (50% duty)
static const uint32_t FLASH_DURATION = 2; // Brief flash for sending
static uint32_t flash_countdown = 0;

// Helper function to turn off all LEDs
static void led_off_all() {
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, LOW);
}

// Helper function to set specific LED
static void led_set(uint8_t pin, uint8_t state) {
  digitalWrite(pin, state ? HIGH : LOW);
}

void led_status_init() {
  // Initialize LED pins as outputs
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  
  // Turn off all LEDs
  led_off_all();
  
  LOGI(TAG, "LED Status initialized (RED:%d, GREEN:%d, BLUE:%d)", 
       LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN);
}

void led_status_set_state(led_state_t state) {
  if (state != current_state) {
    LOGI(TAG, "LED State change: %d -> %d", current_state, state);
    current_state = state;
    blink_counter = 0;
    flash_countdown = 0;
    
    // Set initial LED state for the new mode
    if (state == LED_STATE_CONNECTED) {
      led_off_all();
      led_set(LED_GREEN_PIN, 1);  // Green solid
    } else {
      led_off_all();
    }
  }
}

void led_status_task_update() {
  blink_counter = (blink_counter + 1) % BLINK_PERIOD;
  
  switch (current_state) {
    case LED_STATE_INIT:
      // All off
      led_off_all();
      break;
      
    case LED_STATE_CONNECTING:
      // Blue blinking at 50% duty cycle
      led_off_all();
      if (blink_counter < BLINK_ON_TIME) {
        led_set(LED_BLUE_PIN, 1);
      }
      break;
      
    case LED_STATE_CONNECTED:
      // Green solid (no blinking)
      led_off_all();
      led_set(LED_GREEN_PIN, 1);
      break;
      
    case LED_STATE_SENDING:
      // Blue flash for brief moment, then return to green
      if (flash_countdown > 0) {
        flash_countdown--;
        led_off_all();
        led_set(LED_BLUE_PIN, 1);
      } else {
        // Return to connected state after flash
        led_off_all();
        led_set(LED_GREEN_PIN, 1);
      }
      break;
      
    case LED_STATE_ERROR:
      // Red blinking at 50% duty cycle
      led_off_all();
      if (blink_counter < BLINK_ON_TIME) {
        led_set(LED_RED_PIN, 1);
      }
      break;
      
    default:
      led_off_all();
  }
}

void led_status_connecting() {
  led_status_set_state(LED_STATE_CONNECTING);
}

void led_status_connected() {
  led_status_set_state(LED_STATE_CONNECTED);
}

void led_status_sending() {
  if (current_state == LED_STATE_CONNECTED || current_state == LED_STATE_SENDING) {
    current_state = LED_STATE_SENDING;
    flash_countdown = FLASH_DURATION;
    led_off_all();
    led_set(LED_BLUE_PIN, 1);
  }
}

void led_status_error() {
  led_status_set_state(LED_STATE_ERROR);
}

#endif
