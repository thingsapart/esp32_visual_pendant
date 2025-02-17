#ifndef __DEBUG_H_
#define __DEBUG_H_

#define D_INFO 0
#define D_WARN 1
#define D_ERROR 2

#define UI_DEBUG_LOG D_INFO

#ifdef UI_DEBUG_LOG


# if ESP32_HW
#  include "machine/arduino_serial_wrapper.h"
#  include <string.h>
#  define _d(lvl, s) do { if (lvl >= UI_DEBUG_LOG) { serial_write(get_serial_handle(-1), (uint8_t*) s, strlen(s)); } } while (false)
#  define _df(lvl, format, ...) do { if (lvl >= UI_DEBUG_LOG) { char __temp[1024]; snprintf(__temp, 1023, format, ##__VA_ARGS__); _d(lvl, __temp); } }  while (false)
# else
#  include <stdio.h>
#  define _d(lvl, s) do { if (lvl >= UI_DEBUG_LOG) { printf("%s\n", s); } } while (false)
#  define _df(lvl, format, ...) do { if (lvl >= UI_DEBUG_LOG) { printf(format, __VA_ARGS__); } } while (0)
# endif
#else
#define _d(lvl, s) 
#define _df(lvl, format, ...) 
#endif 

#endif // __DEBUG_H_