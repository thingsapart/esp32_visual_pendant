#include "encoder.hpp"

Encoder encoder(ENCODER_PIN_X, ENCODER_PIN_Y, 4);

Encoder::Encoder(uint8_t pin_x, uint8_t pin_y, int divisor) 
    : enc(pin_x, pin_y), uiMode(false), uiModeCount(0), encModeCount(0), divisor(divisor),
      encModePosition(0) {}

int ::Encoder::readAndReset() {
    int val = enc.position() + (uiMode ? uiModeCount : encModeCount);
    int rem = val % divisor;
    int res = (val - rem) / divisor;
    enc.zero();
    if (uiMode) {
        uiModeCount = rem;
    } else {
        encModePosition += res;
        encModeCount = rem;
    }
    return res;
}

int Encoder::position() {
    assert(!uiMode);

    return encModePosition;
}

void Encoder::setUiMode() {
    if (!uiMode) {
        uiMode = true;
        encModeCount += enc.position();
        enc.zero();
        uiMode = true;
    }
}

void Encoder::setEncoderMode() {
    if (uiMode) {
        uiModeCount += enc.position();
        enc.zero();
        uiMode = false;
    }
}