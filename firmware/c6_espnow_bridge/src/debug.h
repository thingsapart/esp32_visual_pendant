// Minimal debug.h for C6 bridge build
// Provides LOGI/LOGW/LOGE/LOGD/LOGV mapping to ESP logging so code using
// the project's LOG macros compiles within the C6 project.
#ifndef C6_BRIDGE_DEBUG_H
#define C6_BRIDGE_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdarg.h>
#include <stdbool.h>

void c6_serial_print(const char *s);
void c6_serial_vprintf(const char *fmt, ...);
int c6_esp_log_vprintf(const char *fmt, va_list ap);

extern bool g_c6_serial_verbose;

#ifdef __cplusplus
}
#endif


/* Core C6 logging helper used by the level macros below. */
#define C6_LOG(...) c6_serial_vprintf(__VA_ARGS__)
#define C6_PRINT(s)   c6_serial_print(s)

/* LOG macros: accept a `tag` argument (string pointer or string literal).
 * Emit a prefixed format string and pass `tag` as the first format arg.
 */
#define LOGI(tag, fmt, ...) C6_LOG("[I] %s: " fmt, tag, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) C6_LOG("[W] %s: " fmt, tag, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) C6_LOG("[E] %s: " fmt, tag, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) C6_LOG("[D] %s: " fmt, tag, ##__VA_ARGS__)
#define LOGV(tag, fmt, ...) C6_LOG("[V] %s: " fmt, tag, ##__VA_ARGS__)
#define LOGT(tag, fmt, ...) C6_LOG("[T] %s: " fmt, tag, ##__VA_ARGS__)

// Verbose logging macro: enabled only when C6_BRIDGE_LOG_EXT is defined.
#ifdef C6_BRIDGE_LOG_EXT
#define C6_VLOG(fmt, ...) ESP_LOGV(TAG, fmt, ##__VA_ARGS__)
#else
#define C6_VLOG(fmt, ...) do { (void)0; } while (0)
#endif

#endif // C6_BRIDGE_DEBUG_H
