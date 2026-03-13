// FreeRTOS hooks: stack overflow and heap OOM diagnostics
#include "Arduino.h"
#include "debug.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#if CONFIG_SPIRAM
  #ifdef ESP32_HW
    #ifdef ESP32P4_HW
      #include "esp_psram.h"
    #else
      // #include "esp_spiram.h"
      #include "esp_psram.h"
    #endif
  #endif
#endif

#define HOOKS_TAG "FREERTOS"

#include "driver/task_registry.h"
#include "esp_rom_sys.h"  // esp_rom_printf — writes directly to ROM UART, survives USB-CDC teardown

// ---------------------------------------------------------------------------
// Shared helpers — safe to call from any panic/OOM/overflow context.
// Use esp_rom_printf (synchronous ROM UART write) rather than ESP_LOGE or our
// LOGE macro.  Both of those route through USB-CDC or the xStreamBuffer /
// log_writer_task chain, which are torn down or stalled before the panic
// handler fires.  esp_rom_printf is exactly what the IDF panic handler itself
// uses, so it is always visible on the hardware UART regardless of USB state.
// ---------------------------------------------------------------------------
static void _print_heap_state_safe(void) {
    size_t int_free    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t int_lfb     = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t int_min     = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t def_free    = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t def_lfb     = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    esp_rom_printf("[%s] --- RAM state ---\n", HOOKS_TAG);
    esp_rom_printf("[%s]   Internal:  free=%u  largest_free_block=%u  min_ever=%u\n",
                   HOOKS_TAG, (unsigned)int_free, (unsigned)int_lfb, (unsigned)int_min);
    esp_rom_printf("[%s]   Default:   free=%u  largest_free_block=%u\n",
                   HOOKS_TAG, (unsigned)def_free, (unsigned)def_lfb);
#if CONFIG_SPIRAM
    /* Only query PSRAM diagnostics if SPIRAM support is enabled and the
     * PSRAM subsystem has been initialized at runtime. On targets without
     * PSRAM or where PSRAM isn't brought up yet, querying PSRAM pools can
     * trigger internal TLSF traversal that dereferences invalid pointers. */
    #ifdef ESP32P4_HW
    if (esp_psram_is_initialized()) {
    #else
    if (esp_psram_is_initialized()) {
    #endif
        size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t psram_lfb   = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        esp_rom_printf("[%s]   PSRAM:     free=%u  largest_free_block=%u\n",
                       HOOKS_TAG, (unsigned)psram_free, (unsigned)psram_lfb);
    } else {
        esp_rom_printf("[%s]   PSRAM:     (not initialized)\n", HOOKS_TAG);
    }
#endif
}

// ---------------------------------------------------------------------------
// Print per-task stack high-water marks without allocating heap.
// Uses uxTaskGetSystemState with a static buffer for safety.
// ---------------------------------------------------------------------------
#define MAX_TASK_SNAPSHOT 32
static void _print_task_watermarks_safe(void) {
    // Use a static array — no heap allocation needed.
    static TaskStatus_t snap[MAX_TASK_SNAPSHOT];
    UBaseType_t n = task_registry_get_system_state(snap, MAX_TASK_SNAPSHOT, NULL);
    if (n == 0) {
        esp_rom_printf("[%s]   (task list unavailable)\n", HOOKS_TAG);
        return;
    }
    esp_rom_printf("[%s] --- Task stack high-water marks (words remaining) ---\n", HOOKS_TAG);
    for (UBaseType_t i = 0; i < n; i++) {
        // eCurrentState: eRunning=0 eReady=1 eBlocked=2 eSuspended=3 eDeleted=4
        const char *state_str[] = {"RUN", "RDY", "BLK", "SUS", "DEL"};
        const char *st = (snap[i].eCurrentState <= eDeleted)
                             ? state_str[snap[i].eCurrentState]
                             : "???";
        esp_rom_printf("[%s]   %-16s  prio=%2u  hwm=%5u words  [%s]\n",
                       HOOKS_TAG,
                       snap[i].pcTaskName ? snap[i].pcTaskName : "?",
                       (unsigned)snap[i].uxCurrentPriority,
                       (unsigned)snap[i].usStackHighWaterMark,
                       st);
    }
}

// ---------------------------------------------------------------------------
// heap_caps failed-alloc hook — registered via heap_caps_register_failed_alloc_callback.
// Called every time any heap_caps_malloc / malloc / calloc / realloc returns NULL.
// Parameters:
//   size         — number of bytes that could not be allocated
//   caps         — capability flags (MALLOC_CAP_*) of the failed request
//   function_name— "malloc" / "calloc" / "heap_caps_malloc" etc.
// ---------------------------------------------------------------------------
static void _heap_alloc_failed_hook(size_t size, uint32_t caps,
                                     const char *function_name) {
    // Derive a short caps string without allocating.
    char caps_buf[48];
    int pos = 0;
#define APPEND_CAP(flag, name) \
    if ((caps & (flag)) && pos < (int)sizeof(caps_buf) - 2) { \
        const char *s = (name); \
        while (*s && pos < (int)sizeof(caps_buf) - 2) caps_buf[pos++] = *s++; \
        caps_buf[pos++] = '|'; \
    }
    APPEND_CAP(MALLOC_CAP_INTERNAL,  "INT")
    APPEND_CAP(MALLOC_CAP_SPIRAM,    "PSRAM")
    APPEND_CAP(MALLOC_CAP_DMA,       "DMA")
    APPEND_CAP(MALLOC_CAP_32BIT,     "32B")
    APPEND_CAP(MALLOC_CAP_IRAM_8BIT, "IRAM8")
    APPEND_CAP(MALLOC_CAP_DEFAULT,   "DEF")
#undef APPEND_CAP
    if (pos > 0 && caps_buf[pos - 1] == '|') pos--;  // remove trailing '|'
    caps_buf[pos] = '\0';

    const char *fn = function_name ? function_name : "?";
    // malloc is not valid from real ISRs, so this will always be a task handle.
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    const char *task_name = cur ? pcTaskGetName(cur) : "<none>";

    // Use esp_rom_printf: synchronous ROM UART, bypasses USB-CDC and the
    // xStreamBuffer/log_writer_task chain that would be stalled under OOM.
    esp_rom_printf("[%s] HEAP ALLOC FAILED: %s requested %u bytes  caps=0x%08X [%s]  task=%s\n",
                   HOOKS_TAG, fn, (unsigned)size, (unsigned)caps, caps_buf, task_name);
    _print_heap_state_safe();
}

// ---------------------------------------------------------------------------
// Call this once from mcu_setup() to arm the failed-alloc hook.
// ---------------------------------------------------------------------------
void freertos_install_oom_hook(void) {
    esp_err_t err = heap_caps_register_failed_alloc_callback(_heap_alloc_failed_hook);
    if (err != ESP_OK) {
        ESP_LOGW(HOOKS_TAG, "heap_caps_register_failed_alloc_callback failed: %d", err);
    } else {
        ESP_LOGI(HOOKS_TAG, "Heap OOM hook installed (output via esp_rom_printf)");
    }
}

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// vApplicationStackOverflowHook — fires when CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY
// detects that a task's stack canary has been corrupted.
// ---------------------------------------------------------------------------
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  const char *name = pcTaskName ? pcTaskName : "<unknown>";

  // Use esp_rom_printf first — it writes directly to ROM UART (same path as the
  // panic dump) and is not buffered through USB-CDC, so it is always visible even
  // when the USB connection is torn down by the ensuing crash.
  esp_rom_printf("\n[FREERTOS] STACK OVERFLOW in task: %s\n", name);
  ESP_LOGE(HOOKS_TAG, "STACK OVERFLOW in task: %s", name);
  // NOTE: do NOT call _print_heap_state_safe() here.  A stack overflow corrupts
  // adjacent SRAM which may include TLSF block headers.  Walking the heap via
  // heap_caps_get_*() will dereference the corrupted "next block" pointer and
  // immediately trigger a second Load Access Fault, hiding the task name.
  // Heap diagnostics are only safe in the OOM hook (heap_alloc_failed), not here.
  _print_task_watermarks_safe();

#ifdef ESP32_HW
  if (Serial) {
    Serial.printf("[PANIC] Stack overflow in task: %s\n", name);
    Serial.flush();
  }
#endif

  // Force a hard fault so the platform panic handler runs and
  // (with CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) stores a coredump.
  volatile uint32_t *p = (volatile uint32_t *)0x0;
  *p = 0xDEADBEEF;

  for (;;) ;
}

#ifdef __cplusplus
}
#endif
