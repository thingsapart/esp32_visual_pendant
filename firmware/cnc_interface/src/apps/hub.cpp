#ifdef APP_HUB

#include "Arduino.h"
#include "debug.h"

// Placeholder for Hub application logic
// This ensures that the hub environment has a main entry point.

void setup() {
    Serial.begin(115200);
    LOGI("HUB", "Hub Application Started");
}

void loop() {
    delay(1000);
}

#endif
