#ifdef POSIX
#ifndef __ARDUINO_H__
#define __ARDUINO_H__

#include <string>

class EmuSerial {
public:
    void write(std::string val);
    void begin(int speed);
    void print(int val);
    void print(std::string val);
    void println(std::string val);
    void println();
    void printf(const char *format, ...);
};

void delay(unsigned int ms);
unsigned int millis();

extern EmuSerial Serial;

#endif // __ARDUINO_H__ 
#endif // POSIX