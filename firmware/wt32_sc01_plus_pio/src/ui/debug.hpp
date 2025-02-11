#ifndef __UI_DEBUG_HPP_
#define __UI_DEBUG_HPP_

#define D_INFO 0
#define D_WARN 1
#define D_ERROR 2

#define UI_DEBUG_LOG D_INFO

#ifdef UI_DEBUG_LOG
#define _d(lvl, s) do { if (lvl >= UI_DEBUG_LOG) { Serial.println(s); } } while (false);
#define _df(lvl, format, ...) do { if (lvl >= UI_DEBUG_LOG) { char __temp[1024]; snprintf(__temp, 1023, format, ##__VA_ARGS__); Serial.println(__temp); } }  while (false);
#else
#define _d(lvl, s) 
#define _df(lvl, format, ...) 
#endif 

#endif // __UI_DEBUG_HPP_