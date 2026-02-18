# RGB LED Status Indicators - ESP32-S3 Hub

## Overview
The ESP32-S3 Hub now uses RGB LEDs to visually indicate system status and activity. This provides at-a-glance feedback about the system's state without needing serial console access.

## LED Configuration (ESP32-S3-DevKit-C-1)
- **Red LED**: GPIO 46
- **Green LED**: GPIO 0  
- **Blue LED**: GPIO 1

These GPIO assignments follow the standard ESP32-S3-DevKit layout. If using a different board, you can override these in `include/led_status.h`:
```cpp
#define LED_RED_PIN 46
#define LED_GREEN_PIN 0
#define LED_BLUE_PIN 1
```

## LED Status States

### 🔵 Blue Blinking (50% duty cycle)
**System State**: Connecting to UART machine
- Indicates the hub is attempting to establish a connection with the CNC machine via UART
- Once connected, the LED will change to solid green

### 🟢 Green Solid (Constant)
**System State**: Connected and idle
- Indicates a successful connection to the CNC machine
- The system is operational and ready to receive/send commands
- This is the normal running state

### 🔵 Blue Flash (Brief)
**System State**: Sending data
- A momentary blue flash when the hub sends messages to the remote display
- Happens during:
  - Machine state updates (position, status, homing state, etc.)
  - Feed rate changes
  - Tool/spindle information updates
  - Keep-alive messages
- Briefly returns to green after the flash

### 🔴 Red Blinking (50% duty cycle)
**System State**: Error condition
- Indicates a critical error that requires attention
- Common error causes:
  - Failed to initialize UART connection to machine
  - Failed to initialize ESP-NOW wireless connectivity
  - Failed to add remote display as a peer
  - Failed to serialize/send message data
  - File list serialization failures

## Behavior Details

### Blinking Pattern
- **Blink Period**: 200ms total (100ms on, 100ms off)
- **Flash Duration**: Brief 20ms flash when sending

### State Transitions
```
Power-Up
  ↓
Initialization (Off)
  ↓
Connecting (Blue Blink) ← waiting for UART connection
  ↓
Connected (Green Solid) ← normal operation
  ↕
  ├─ Error (Red Blink) ← critical error detected
  └─ Sending (Blue Flash) ← transient state, returns to Green
```

## Implementation Details

The LED status system is implemented through:
1. **`led_status.h`** - Header file with LED control API and GPIO definitions
2. **`src/driver/led_status.cpp`** - Implementation of LED state machine and update logic
3. **`src/apps/hub.cpp`** - Integration points that trigger LED state changes

### API Functions

```cpp
void led_status_init();              // Initialize GPIO pins
void led_status_set_state(led_state_t state);  // Set LED state directly
void led_status_task_update();       // Update LED animation state (call every 200ms)

// Convenience functions
void led_status_connecting();        // Blue blinking
void led_status_connected();         // Green solid
void led_status_sending();           // Blue flash
void led_status_error();             // Red blinking
```

### Integration Points

LED state changes are triggered at these key points:
- **Initialization**: `led_status_init()` and `led_status_connecting()` in `setup_machine_interface()`
- **Connection State Changes**: Via `on_connected_change()` callback
- **Message Sending**: Automatic `led_status_sending()` before sending any message to the display
- **Errors**: Via `led_status_error()` when errors are detected
- **LED Updates**: `led_status_task_update()` called each poll cycle in `machine_poll_send_task_iter()`

## Troubleshooting

### LED doesn't light up
- Check GPIO pin connections
- Verify LED polarity (longer leg is positive)
- Check power supply to LEDs
- Verify GPIO pins aren't used by other components

### LED stays on error state (red blinking)
- Check UART connection to the machine
- Verify ESP-NOW is properly initialized
- Check WiFi is available for the ESP-NOW display peer
- Review serial logs for specific error messages

### LED flashes are too slow/fast
- Adjust `BLINK_PERIOD` in `led_status.cpp`
- Default is 10 ticks per period, with `led_status_task_update()` called every poll cycle (200ms = ~20ms per tick)

