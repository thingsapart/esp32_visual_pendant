// Implementation of the lightweight task registry used when
// CONFIG_FREERTOS_USE_TRACE_FACILITY is not enabled.

#include "task_registry.h"
#include "sdkconfig.h"
#include <string.h>

#include "debug.h"

// If the trace facility is enabled, forward to the OS implementation.
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) && CONFIG_FREERTOS_USE_TRACE_FACILITY

void task_registry_register_handle(TaskHandle_t handle, const char *name) {
    (void)handle; (void)name; // no-op
}

void task_registry_unregister_handle(TaskHandle_t handle) { (void)handle; }

UBaseType_t task_registry_get_system_state(TaskStatus_t *pxTaskStatusArray,
                                          UBaseType_t uxArraySize,
                                          uint32_t *pulTotalRunTime) {
    return uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, pulTotalRunTime);
}

// Print a compact one-line-per-task summary to the log when the FreeRTOS
// trace facility is enabled. This implementation takes a snapshot via
// `uxTaskGetSystemState` and prints similar information to the lightweight
// fallback implementation below.
void task_registry_print_summary(void) {
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    if (uxArraySize == 0) {
        LOGI("TASKS", "(no tasks available)");
        return;
    }

    TaskStatus_t *snap = pvPortMalloc(sizeof(TaskStatus_t) * uxArraySize);
    if (snap == NULL) {
        LOGI("TASKS", "(no tasks available)");
        return;
    }

    UBaseType_t n = uxTaskGetSystemState(snap, uxArraySize, NULL);
    for (UBaseType_t i = 0; i < n; ++i) {
        const char *state_str = "?";
        switch (snap[i].eCurrentState) {
            case eRunning:   state_str = "RUN"; break;
            case eReady:     state_str = "RDY"; break;
            case eBlocked:   state_str = "BLK"; break;
            case eSuspended: state_str = "SUS"; break;
            case eDeleted:   state_str = "DEL"; break;
            default:         state_str = "???"; break;
        }
        const char *name = snap[i].pcTaskName ? snap[i].pcTaskName : "<unnamed>";
        LOGI("TASKS", "%s prio=%u hwm=%u state=%s handle=%p",
             name,
             (unsigned)snap[i].uxCurrentPriority,
             (unsigned)snap[i].usStackHighWaterMark,
             state_str,
             (void *)snap[i].xHandle);
    }

    vPortFree(snap);
}

#else // custom registry implementation

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    TaskHandle_t handle;
    char name[configMAX_TASK_NAME_LEN];
} registry_entry_t;

static registry_entry_t registry[TASK_REGISTRY_MAX_TASKS];

// Protect registry updates with a critical section
// Use a port-specific spinlock/critical section object where required by
// the ESP port macros which expect a `portMUX_TYPE *` argument.
static portMUX_TYPE registry_mux = portMUX_INITIALIZER_UNLOCKED;
static void registry_lock(void) { taskENTER_CRITICAL(&registry_mux); }
static void registry_unlock(void) { taskEXIT_CRITICAL(&registry_mux); }

void task_registry_register_handle(TaskHandle_t handle, const char *name) {
    if (handle == NULL) return;
    registry_lock();
    // Try to update existing entry
    for (int i = 0; i < TASK_REGISTRY_MAX_TASKS; ++i) {
        if (registry[i].handle == handle) {
            if (name) strncpy(registry[i].name, name, sizeof(registry[i].name)-1);
            registry[i].name[sizeof(registry[i].name)-1] = '\0';
            registry_unlock();
            return;
        }
    }
    // Find empty slot
    for (int i = 0; i < TASK_REGISTRY_MAX_TASKS; ++i) {
        if (registry[i].handle == NULL) {
            registry[i].handle = handle;
            if (name) strncpy(registry[i].name, name, sizeof(registry[i].name)-1);
            registry[i].name[sizeof(registry[i].name)-1] = '\0';
            registry_unlock();
            return;
        }
    }
    // No space; drop silently
    registry_unlock();
}

void task_registry_unregister_handle(TaskHandle_t handle) {
    if (handle == NULL) return;
    registry_lock();
    for (int i = 0; i < TASK_REGISTRY_MAX_TASKS; ++i) {
        if (registry[i].handle == handle) {
            registry[i].handle = NULL;
            registry[i].name[0] = '\0';
            break;
        }
    }
    registry_unlock();
}

UBaseType_t task_registry_get_system_state(TaskStatus_t *pxTaskStatusArray,
                                          UBaseType_t uxArraySize,
                                          uint32_t *pulTotalRunTime) {
    (void)pulTotalRunTime;
    if (pxTaskStatusArray == NULL || uxArraySize == 0) return 0;

    registry_lock();
    UBaseType_t written = 0;
    for (int i = 0; i < TASK_REGISTRY_MAX_TASKS && written < uxArraySize; ++i) {
        if (registry[i].handle == NULL) continue;
        TaskStatus_t info;
        memset(&info, 0, sizeof(info));
        info.xHandle = registry[i].handle;
        info.pcTaskName = registry[i].name;
        // Best-effort: query priority and stack high water mark using lightweight
        // APIs that are usually available even when trace facility is disabled.
        info.uxCurrentPriority = uxTaskPriorityGet(registry[i].handle);
        info.uxBasePriority = info.uxCurrentPriority;
        info.usStackHighWaterMark = uxTaskGetStackHighWaterMark(registry[i].handle);
        // Mark state as RUN if this is the current task, otherwise RDY.
        info.eCurrentState = (registry[i].handle == xTaskGetCurrentTaskHandle()) ? eRunning : eReady;
        pxTaskStatusArray[written++] = info;
    }
    registry_unlock();
    return written;
}

// ---------------------------------------------------------------------------
// Print a compact one-line-per-task summary to the log.
// Uses the same snapshot semantics as task_registry_get_system_state.
// ---------------------------------------------------------------------------
void task_registry_print_summary(void) {
    TaskStatus_t snap[TASK_REGISTRY_MAX_TASKS];
    UBaseType_t n = task_registry_get_system_state(snap, TASK_REGISTRY_MAX_TASKS, NULL);
    if (n == 0) {
        LOGI("TASKS", "(no tasks available)");
        return;
    }
    for (UBaseType_t i = 0; i < n; ++i) {
        const char *state_str = "?";
        switch (snap[i].eCurrentState) {
            case eRunning:   state_str = "RUN"; break;
            case eReady:     state_str = "RDY"; break;
            case eBlocked:   state_str = "BLK"; break;
            case eSuspended: state_str = "SUS"; break;
            case eDeleted:   state_str = "DEL"; break;
            default:         state_str = "???"; break;
        }
        const char *name = snap[i].pcTaskName ? snap[i].pcTaskName : "<unnamed>";
        LOGI("TASKS", "%s prio=%u hwm=%u state=%s handle=%p",
                 name,
                 (unsigned)snap[i].uxCurrentPriority,
                 (unsigned)snap[i].usStackHighWaterMark,
                 state_str,
                 (void *)snap[i].xHandle);
    }
}

#endif
