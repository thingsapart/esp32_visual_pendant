#ifndef __MACHINE_RESPONSE_PROC_TASK_H__
#define __MACHINE_RESPONSE_PROC_TASK_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t machine_response_proc_task_handle;

void machine_response_proc_task(void *args);

#endif // __MACHINE_RESPONSE_PROC_TASK_H__