#ifndef __TASK_UTIL_H__
#define __TASK_UTIL_H__

#ifdef POSIX
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#endif  /// __TASK_UTIL_H__