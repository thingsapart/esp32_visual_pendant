// app_log.h — Serial-backed logging helpers for camera companion

#ifndef APP_LOG_H
#define APP_LOG_H

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

static inline void app_log_printf(const char *level,
                                  const char *tag,
                                  const char *fmt,
                                  ...) {
    char msg[384];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (n < 0) return;

    uint32_t now = millis();
    if (n < (int)sizeof(msg)) {
        Serial.printf("[%8lu][%s][%s] %s\r\n", (unsigned long)now, level, tag, msg);
    } else {
        msg[sizeof(msg) - 1] = '\0';
        Serial.printf("[%8lu][%s][%s] %s...\r\n", (unsigned long)now, level, tag, msg);
    }
}

#define APP_LOGE(tag, fmt, ...) app_log_printf("E", tag, fmt, ##__VA_ARGS__)
#define APP_LOGW(tag, fmt, ...) app_log_printf("W", tag, fmt, ##__VA_ARGS__)
#define APP_LOGI(tag, fmt, ...) app_log_printf("I", tag, fmt, ##__VA_ARGS__)
#define APP_LOGD(tag, fmt, ...) app_log_printf("D", tag, fmt, ##__VA_ARGS__)
#define APP_LOGV(tag, fmt, ...) app_log_printf("V", tag, fmt, ##__VA_ARGS__)

#endif // APP_LOG_H
