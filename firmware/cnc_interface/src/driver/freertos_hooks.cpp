// FreeRTOS hooks: stack overflow handler
#include "Arduino.h"
#include "debug.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  const char *name = pcTaskName ? pcTaskName : "<unknown>";
  LOGE("FREERTOS", "Stack overflow detected in task: %s", name);

#ifdef ESP32_HW
  // Try to flush serial output briefly so logs are visible
  if (Serial) {
    Serial.printf("Stack overflow in task: %s\n", name);
    Serial.flush();
  }
#endif

  // Force a hard fault to ensure the platform panic handler runs and
  // (with CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) stores a coredump for post-mortem.
  volatile uint32_t *p = (volatile uint32_t *)0x0;
  *p = 0xDEADBEEF;

  // Fallback infinite loop (should not be reached)
  for (;;)
    ;
}

#ifdef __cplusplus
}
#endif
