#ifndef __DEBUG_H_
#define __DEBUG_H_

// --- Log Level Definitions ---
// Define the available log levels. A lower number means a higher priority.
#define D_NONE -1  // Special level to disable all logs
#define D_ERROR 0
#define D_WARN 1
#define D_INFO 2
#define D_DEBUG 3
#define D_VERBOSE 4

// --- Global Log Level Configuration ---
// This is the default log level for all files unless overridden locally.
// It can be set via a build flag, e.g., -DUI_DEBUG_LEVEL=D_VERBOSE
#ifndef UI_DEBUG_LEVEL
#define UI_DEBUG_LEVEL D_INFO
#endif

// --- Per-File Log Level Logic ---
// A source file can #define LOG_LOCAL_LEVEL to its desired level *before*
// including this header. If it's defined, we use it; otherwise, we fall back
// to the global UI_DEBUG_LEVEL.

#ifdef UI_DEBUG_LOCAL_LEVEL
#define EFFECTIVE_LOG_LEVEL UI_DEBUG_LOCAL_LEVEL
// We undefine it immediately to prevent it from leaking into other headers
// or source files included in the same compilation unit.
#undef LOG_LOCAL_LEVEL
#else
#define EFFECTIVE_LOG_LEVEL UI_DEBUG_LEVEL
#endif

// --- Backend Implementation ---
// This section defines the actual printing mechanism based on the platform.
#if ESP32_HW
#include <stdio.h>
#include <string.h>

#include "driver/arduino_serial_wrapper.h"
// The core logging function for ESP32 hardware.
#define LOG_BACKEND(format, ...)                                          \
  do {                                                                    \
    size_t __len = snprintf(NULL, 0, format __VA_OPT__(, )##__VA_ARGS__); \
    char __temp[__len + 2]; /* +1 for null, +1 for newline */             \
    snprintf(__temp, __len + 2, format "\n" __VA_OPT__(, )##__VA_ARGS__); \
    default_serial_write((const uint8_t *)__temp, strlen(__temp));        \
  } while (0)
#else  // POSIX / Native simulation
#include <stdio.h>
// The core logging function for native builds.
#define LOG_BACKEND(format, ...)                     \
  do {                                               \
    printf(format "\n" __VA_OPT__(, )##__VA_ARGS__); \
    fflush(stdout);                                  \
  } while (0)
#endif

// --- Compile-Time Log Level Filtering ---
// The preprocessor will completely remove log statements below the effective
// level.

#if EFFECTIVE_LOG_LEVEL >= D_ERROR
#define LOGE(tag, fmt, ...) \
  LOG_BACKEND("[E][%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGE(tag, fmt, ...) do {} while(0)
#endif

#if EFFECTIVE_LOG_LEVEL >= D_WARN
#define LOGW(tag, fmt, ...) \
  LOG_BACKEND("[W][%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGW(tag, fmt, ...) do {} while(0)
#endif

#if EFFECTIVE_LOG_LEVEL >= D_INFO
#define LOGI(tag, fmt, ...) \
  LOG_BACKEND("[I][%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGI(tag, fmt, ...) do {} while(0)
#endif

#if EFFECTIVE_LOG_LEVEL >= D_DEBUG
#define LOGD(tag, fmt, ...) \
  LOG_BACKEND("[D][%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGD(tag, fmt, ...) do {} while(0)
#endif

#if EFFECTIVE_LOG_LEVEL >= D_VERBOSE
#define LOGV(tag, fmt, ...) \
  LOG_BACKEND("[V][%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGV(tag, fmt, ...) do {} while(0)
#endif

// A special macro for temporary debugging that you want to always see.
// It can be easily found and removed later.
#define LOGT(tag, fmt, ...) \
  LOG_BACKEND("[TEMP][%s] " fmt, tag __VA_OPT__(, ) __VA_ARGS__)

// --- ESP-IDF Compatibility Macros ---
// Undefine existing macros to avoid warnings and redefine them to use our
// system.
#ifdef ESP_LOGE
#undef ESP_LOGE
#endif
#define ESP_LOGE(tag, ...) LOGE(tag, __VA_ARGS__)

#ifdef ESP_LOGW
#undef ESP_LOGW
#endif
#define ESP_LOGW(tag, ...) LOGW(tag, __VA_ARGS__)

#ifdef ESP_LOGI
#undef ESP_LOGI
#endif
#define ESP_LOGI(tag, ...) LOGI(tag, __VA_ARGS__)

#ifdef ESP_LOGD
#undef ESP_LOGD
#endif
#define ESP_LOGD(tag, ...) LOGD(tag, __VA_ARGS__)

#ifdef ESP_LOGV
#undef ESP_LOGV
#endif
#define ESP_LOGV(tag, ...) LOGV(tag, __VA_ARGS__)

void LOG_CURR_TASK();

#define RETURN_ON_ERROR(x, log_tag, format, ...)     \
  do {                                               \
    int err_rc_ = (x);                               \
    if (unlikely(err_rc_ != 0)) {                    \
      LOGE(log_tag, "%s(%d): " format, __FUNCTION__, \
           __LINE__ __VA_OPT__(, ) __VA_ARGS__);     \
      return err_rc_;                                \
    }                                                \
  } while (0)

#define ERROR_CHECK(x)       \
  do {                       \
    esp_err_t err_rc_ = (x); \
    (void)sizeof(err_rc_);   \
  } while (0)

#endif  // __DEBUG_H_