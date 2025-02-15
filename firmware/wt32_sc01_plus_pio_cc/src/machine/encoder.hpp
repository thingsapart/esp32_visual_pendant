#ifndef __MACHINE_ENC_HPP__
#define __MACHINE_ENC_HPP__

#include "Arduino.h"
#include "contrib/rotary_encoder.hpp"

class Encoder {
public:
    Encoder(uint8_t pin_x, uint8_t pin_y, int divisor = 1);

    int readAndReset();
    int position();

    void setUiMode();
    void setEncoderMode();

    bool isUiMode() { return uiMode; }
private:
    bool uiMode;
    int uiModeCount;
    int encModeCount;
    int encModePosition;
    int divisor;
#ifdef ESP32_HW
    RotaryEncoderPCNT enc;
#else
    class Enc { 
    public:
        Enc(uint8_t pin_x, uint8_t pin_y) {}
        int position() { return 0; }
        void zero() {}
    } 
    enc;
#endif
};

extern Encoder encoder;

#endif // __MACHINE_ENC_HPP__