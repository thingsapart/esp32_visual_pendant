#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <Arduino.h>

// `g_c6_serial_verbose` is defined in `main.cpp`; declare as extern here.
extern bool g_c6_serial_verbose;

#ifdef __cplusplus
extern "C" {
#endif

void c6_serial_print(const char *s) {
    if (!g_c6_serial_verbose || !s) return;
    Serial.print(s);
    // Serial0 = physical UART0 (pins 30/31), initialised early in setup().
    Serial0.write((const uint8_t *)s, strlen(s));
}

void c6_serial_vprintf(const char *fmt, ...) {
    if (!g_c6_serial_verbose || !fmt) return;
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        /* Ensure newline termination for raw writes */
        if ((size_t)n >= sizeof(tmp) - 1) {
            tmp[sizeof(tmp) - 1] = '\0';
            c6_serial_print(tmp);
            c6_serial_print("\n");
        } else {
            c6_serial_print(tmp);
            if (tmp[n - 1] != '\n') {
                c6_serial_print("\n");
            }
        }
    }
}

int c6_esp_log_vprintf(const char *fmt, va_list ap)
{
    char tmp[512];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    if (n > 0) {
        // Write to USB-CDC (Serial) and hardware UART (Serial0) so logs
        // are visible whether you're monitoring USB or the board UART pins.
        Serial.write((const uint8_t *)tmp, n);
        Serial0.write((const uint8_t *)tmp, n);
    }
    return n;
}

#ifdef __cplusplus
}
#endif
