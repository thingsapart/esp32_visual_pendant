// Lightweight task registry used when FreeRTOS's uxTaskGetSystemState
// (trace facility) is unavailable. When the trace facility is enabled
// this header maps the registry calls to the native API (no-op wrappers).

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Configurable maximum number of tracked tasks. Can be overridden
// by adding -DTASK_REGISTRY_MAX_TASKS=<N> to CFLAGS or in sdkconfig.
#ifndef TASK_REGISTRY_MAX_TASKS
#define TASK_REGISTRY_MAX_TASKS 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Register a task handle and name. Safe to call multiple times for the
// same handle (idempotent). No-op when FreeRTOS trace facility exists.
void task_registry_register_handle(TaskHandle_t handle, const char *name);

// Unregister a task handle. No-op when trace facility exists.
void task_registry_unregister_handle(TaskHandle_t handle);

// Populate an array of TaskStatus_t similar to uxTaskGetSystemState.
// Returns the number of entries written. When the real
// uxTaskGetSystemState is available this calls through to it.
UBaseType_t task_registry_get_system_state(TaskStatus_t *pxTaskStatusArray,
                                          UBaseType_t uxArraySize,
                                          uint32_t *pulTotalRunTime);

// Print a one-line summary per tracked task (name, prio, hwm, state).
// Safe to call from normal task context; uses logging (ESP_LOGI).
void task_registry_print_summary(void);

#ifdef __cplusplus
}
#endif
