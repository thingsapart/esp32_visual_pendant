#ifndef __DEBUG_H_
#define __DEBUG_H_

#define D_INFO 0
#define D_WARN 1
#define D_ERROR 2

#ifndef UI_DEBUG_LOG
#define UI_DEBUG_LOG D_INFO
#endif

#ifdef UI_DEBUG_LOG

#if ESP32_HW
#include <stdio.h>
#include <string.h>

#include "driver/arduino_serial_wrapper.h"
#define _d(lvl, s)                                                             \
  do {                                                                         \
    if (lvl >= UI_DEBUG_LOG) {                                                 \
      default_serial_write((uint8_t *)s, strlen(s));                           \
      default_serial_write((uint8_t *)"  \n", 3);                              \
    }                                                                          \
  } while (false)
#define _df(lvl, format, ...)                                                  \
  do {                                                                         \
    if (lvl >= UI_DEBUG_LOG) {                                                 \
      size_t __len = snprintf(NULL, 0, format __VA_OPT__(, )##__VA_ARGS__);    \
      char __temp[__len + 1];                                                  \
      snprintf(__temp, __len + 1, format __VA_OPT__(, )##__VA_ARGS__);         \
      __temp[__len] = '\0';                                                    \
      _d(lvl, __temp);                                                         \
    }                                                                          \
  } while (false)
// #  define _d(lvl, s) do { if (lvl >= UI_DEBUG_LOG) {
// serial_write(get_serial_handle(-1), (uint8_t*) s, strlen(s));
// serial_write(get_serial_handle(-1), (uint8_t *) "\n", 1); } } while (false)
// #  define _df(lvl, format, ...) do { if (lvl >= UI_DEBUG_LOG) { char
// __temp[1024]; snprintf(__temp, 1023, format, ##__VA_ARGS__); _d(lvl, __temp);
// } }  while (false)

#undef ESP_LOGE
#define LOGE(tag, fmt, ...) _df(2, "[%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGE LOGE

#undef ESP_LOGW
#define LOGW(tag, fmt, ...) _df(1, "[%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGW LOGW

#undef ESP_LOGI
#define LOGI(tag, fmt, ...) _df(0, "[%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGI LOGI

#undef ESP_LOGD
#define LOGD(tag, fmt, ...) _df(-1, "[%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGD LOGD

// Verbose.
#define LOGV(tag, fmt, ...) _df(-2, "[%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)

#else
#include <stdio.h>
#define _d(lvl, s)                                                             \
  do {                                                                         \
    if (lvl >= UI_DEBUG_LOG) {                                                 \
      printf("%s\n", s);                                                       \
      fflush(stdout);                                                          \
    }                                                                          \
  } while (false)
#define _df(lvl, format, ...)                                                  \
  do {                                                                         \
    if (lvl >= UI_DEBUG_LOG) {                                                 \
      printf(format, __VA_ARGS__);                                             \
      printf("\n");                                                            \
      fflush(stdout);                                                          \
    }                                                                          \
  } while (0)
#endif
#else
#define _d(lvl, s)
#define _df(lvl, format, ...)
#endif

void LOG_CURR_TASK();

#endif // __DEBUG_H_
