#ifdef POSIX

#include "Arduino.h"

#include <iostream>
#include <stdio.h>
#include <stdarg.h>
#include <cstdarg>

void EmuSerial::write(std::string val) {
    std::cout << val;
}

void EmuSerial::begin(int speed) {
}

void EmuSerial::print(int val) {
    std::cout << val;
}
void EmuSerial::print(std::string val) {
    std::cout << val;
}

void EmuSerial::println(std::string val) {
    std::cout << val << std::endl;
}

void EmuSerial::println() {
    std::cout << std::endl;
}
void EmuSerial::printf(const char *format, ...) {
    va_list args;
    va_start(args, format);

    char buffer[256]; // Adjust buffer size as needed
    vsnprintf(buffer, sizeof(buffer), format, args);

    std::cout << buffer;
    va_end(args);
}


#ifdef POSIX

#include <unistd.h>
#include <sys/time.h>

void delay(unsigned int delay_ms) {
    usleep(delay_ms * 1000);
}

unsigned int millis() {
    struct timeval  tv;
    gettimeofday(&tv, NULL);

    return (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;
}

#endif

EmuSerial Serial;

#endif
