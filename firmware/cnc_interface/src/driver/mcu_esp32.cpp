#ifdef ESP32_HW

#include <esp_task_wdt.h>
#include <esp_wifi.h>

#include "Arduino.h"
#include "WiFi.h"
#include "debug.h"
#include "driver/arduino_serial_wrapper.h"
#include "driver/remote_comms_wrapper.h"
#include "sdkconfig.h"

#if (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
#define HAS_CORE_DUMP
#else
#undef HAS_CORE_DUMP
#warning "CORE DUMP DISABLED"
#endif

static const char *TAG = "mcu_esp32";

// Declared in freertos_hooks.cpp
extern void freertos_install_oom_hook(void);

void ram_usage() {
    if (!Serial) return; // Don't print if not connected

    // Build the entire report into a stack buffer, then emit it as a single
    // mutex-protected write via default_serial_write().  Using Serial.printf()
    // directly would bypass the write_mutex, interleaving with log output from
    // the machine_send_task and the WiFi ESP-NOW send callback and producing
    // garbled / truncated lines in the host terminal.
    char buf[512];
    int n = 0;

#define RAM_APPEND(...) \
    n += snprintf(buf + n, (int)sizeof(buf) > n ? sizeof(buf) - n : 0, __VA_ARGS__)

    // --- Internal SRAM ---
    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    RAM_APPEND("Internal SRAM:\n");
    RAM_APPEND("  Total: %u bytes\n", internal_total);
    RAM_APPEND("  Free: %u bytes\n", internal_free);
    RAM_APPEND("  Largest Free Block: %u bytes\n", internal_largest_free);

    // --- PSRAM ---
#if CONFIG_SPIRAM
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (psram_total > 0) {
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t psram_largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        RAM_APPEND("PSRAM (SPIRAM):\n");
        RAM_APPEND("  Total: %u bytes\n", psram_total);
        RAM_APPEND("  Free: %u bytes\n", psram_free);
        RAM_APPEND("  Largest Free Block: %u bytes\n", psram_largest_free);
    } else {
        RAM_APPEND("PSRAM: Not available or size is 0.\n");
    }
#else
    RAM_APPEND("PSRAM: Not enabled in menuconfig/sdkconfig.\n");
#endif
    RAM_APPEND("-------------------\n");

#undef RAM_APPEND

    // Single atomic write through the shared serial mutex – no separate flush
    // needed; the TinyUSB CDC stack drains the TX FIFO every USB SOF (~1 ms).
    default_serial_write((const uint8_t *)buf, (size_t)n);

    // Also dump bridge stats if applicable:
    remote_wrapper_print_stats();
}

#ifdef HAS_CORE_DUMP

void print_reset_reason() {
  if (!Serial) return;
  Serial.println("");
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("Reset Reason was: ");
  Serial.println(reason);

  switch (reason) {
    case ESP_RST_POWERON:
      Serial.println("Power ON");
      break;
    case ESP_RST_SW:
      Serial.println("Reset ESPrestart()");
      break;
    case ESP_RST_PANIC:
      Serial.println("Reset por exception/panic");
      break;
    case ESP_RST_UNKNOWN:
      Serial.println("Reset UNKNOW");
      break;
    case ESP_RST_EXT:
      Serial.println("Reset by external pin (not applicable for ESP32)");
      break;
    case ESP_RST_INT_WDT:
      Serial.println("Reset (software or hardware) por interrupção WATCHDOG");
      break;
    case ESP_RST_TASK_WDT:
      Serial.println("Reset WATCHDOG");
      break;
    case ESP_RST_WDT:
      Serial.println("Reset others WATCHDOG´s");
      break;
    case ESP_RST_DEEPSLEEP:
      Serial.println("Reset DEEP SLEEP MODE");
      break;
    case ESP_RST_BROWNOUT:
      Serial.println("Brownout reset (software or hardware)");
      break;
    case ESP_RST_SDIO:
      Serial.println("Reset over SDIO");
      break;
    default:
      break;
  }
}

#include "esp_core_dump.h"

#if ESP32P4_HW
void print_backtrace_info(const esp_core_dump_summary_t *coredump_summary) {}
#else
void print_backtrace_info(const esp_core_dump_summary_t *coredump_summary) {
  if (coredump_summary != NULL) {
    esp_core_dump_bt_info_t bt_info = coredump_summary->exc_bt_info;

    char results[512];  // Assuming a maximum of 512 characters for the
                        // backtrace string
    int offset = snprintf(results, sizeof(results), "Traceback:\n\n");

    // for (int i = 0; i < bt_info.depth; i++)
    for (int i = bt_info.depth - 1; i >= 0; i--) {
      uintptr_t pc = bt_info.bt[i];  // Program Counter (PC)
      int len =
          snprintf(results + offset, sizeof(results) - offset, " 0x%08X", pc);
      if (len >= 0 && offset + len < sizeof(results)) {
        offset += len;
      } else {
        break;  // Reached the limit of the results buffer
      }
    }

    LOGI(TAG, "[backtrace]: %s", results);
    LOGI(TAG, "[backtrace]Backtrace Depth: %u", bt_info.depth);
    LOGI(TAG, "[backtrace]Backtrace Corrupted: %s",
         bt_info.corrupted ? "Yes" : "No");
    LOGI(TAG, "[backtrace]Program Counter: %d", coredump_summary->exc_pc);
    LOGI(TAG, "[backtrace]Coredump Version: %d",
         coredump_summary->core_dump_version);
  } else {
    LOGW(TAG, "Invalid core dump summary");
  }
}

void read_core_dump() {
  printf("Reading core dump...\n");

  esp_core_dump_init();
  esp_core_dump_summary_t *summary =
      (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
  if (summary) {
    esp_err_t err = esp_core_dump_get_summary(summary);
    if (err == ESP_OK) {
      printf("Getting core dump summary ok.");
      print_backtrace_info(summary);
    } else {
      printf("Getting core dump summary not ok. Error: %d\n", (int)err);
      printf("Probably no coredump present yet.\n");
      printf("esp_core_dump_image_check() = %d\n", esp_core_dump_image_check());
    }
    free(summary);
  }
}
#endif

#endif

void print_mac_address() {
  if (!Serial) return;
  uint8_t baseMac[6];
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
  if (ret == ESP_OK) {
    Serial.printf("MAC ADDRESS: %02x:%02x:%02x:%02x:%02x:%02x\n", baseMac[0],
                  baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  } else {
    Serial.println("MAC ADDRESS: Failed to read MAC address");
  }
}

void mcu_setup() {
#ifndef USB_UART_PIN_TX
  Serial.begin(115200);
  // Optional: Wait a very short time for serial, but don't block boot
  unsigned long start_time = millis();
  while (!Serial && (millis() - start_time < 500)) {
    delay(10);
  }
  // Apply non-blocking TX when Serial is a USB CDC type (HWCDC/USBCDC).
  // This applies to ESP32-S3 builds with ARDUINO_USB_CDC_ON_BOOT=1, where
  // Serial == HWCDCSerial or USBSerial and has a 100 ms TX timeout by default.
  // macOS auto-enumerates the USB device, so isCDC_Connected() returns true
  // even without a terminal open — write() then blocks up to 100 ms per call,
  // stalling lvgl_task when the display/touch drivers emit IDF log messages.
  //
  // Note: ESP32-P4 (ESP32P4_HW) uses ARDUINO_USB_MODE=1 but NOT
  // ARDUINO_USB_CDC_ON_BOOT, so Serial == Serial0 (UART0 / HardwareSerial)
  // which never blocks.  The P4's USB-JTAG secondary console blocking is an
  // IDF-level issue (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG) handled
  // separately — HWCDCSerial is not externally declared in that config.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT)
  // Serial == HWCDCSerial or USBSerial — both have setTxTimeoutMs.
  Serial.setTxTimeoutMs(0);
#endif
  add_standard_serial();
#else
  init_standard_serial(115200, CFG_SERIAL_8N1, USB_UART_PIN_RX,
                       USB_UART_PIN_TX);
#endif
  // Do NOT forward IDF logs to Serial on USB-CDC builds (ARDUINO_USB_CDC_ON_BOOT=1
  // or ESP32-P4).  setDebugOutput() routes esp_log output directly through
  // Serial.write() in whatever task generates the log — bypassing the async
  // stream-buffer path and the !Serial guard.  On HWCDC without
  // setTxTimeoutMs(0) this blocks for the default 100 ms TX timeout, stalling
  // lvgl_task when the LVGL port or touch driver emits IDF log messages.
  // Application-level logging already goes through default_serial_write().
  // Only enable setDebugOutput on UART-based Serial (always "connected",
  // hardware FIFO never blocks).
#if (!defined(ARDUINO_USB_CDC_ON_BOOT) || (ARDUINO_USB_CDC_ON_BOOT == 0)) && \
    !defined(ESP32P4_HW)
  Serial.setDebugOutput(true);
#endif
  LOGI(TAG, "PRE-INIT");
  delay(200);

  // Arm the heap OOM diagnostics callback (logs failed allocs + RAM state).
  freertos_install_oom_hook();
}

void mcu_startup() {
#ifdef HAS_CORE_DUMP
  print_reset_reason();
  read_core_dump();
#endif

  LOGI(TAG, "POST-INIT");
  // P4 has the potential to run custon ESP32-C6 firmware which only acts as ESP-NOW
  // bridge. That firmware will make these lines crash.
  #if 0
  #ifndef ESP32P4_HW
  WiFi.mode(WIFI_STA);
  WiFi.STA.begin();
  print_mac_address();
  WiFi.STA.end();
  #endif
  #endif
  LOGI(TAG, "POST-INIT DONE");
}

void LIST_TASKS() {
  const char *TAG = "LIST_TASKS";

  // Get the number of tasks
  size_t num_tasks = uxTaskGetNumberOfTasks();

  // Allocate memory for TaskStatus_t structures
  TaskStatus_t *task_status_array =
      (TaskStatus_t *)pvPortMalloc(num_tasks * sizeof(TaskStatus_t));

  // Check if memory allocation was successful
  if (task_status_array == NULL) {
    LOGI(TAG, "Memory allocation failed!\n");
    return;
  }

  // Get the system state
  size_t num_tasks_populated =
      uxTaskGetSystemState(task_status_array, num_tasks, NULL);

  // Check if the function returned the correct number of tasks
  if (num_tasks_populated != num_tasks) {
    LOGI(TAG, "Error: uxTaskGetSystemState returned %u tasks, expected %u\n",
         (unsigned)num_tasks_populated, (unsigned)num_tasks);
    vPortFree(task_status_array);
    return;
  }

  // Print task information
  printf("FreeRTOS Task List:\n");
  for (size_t i = 0; i < num_tasks; i++) {
    LOGI(TAG, "  Task Name: %s\n", task_status_array[i].pcTaskName);
    LOGI(TAG, "  Task Priority: %u\n", task_status_array[i].uxCurrentPriority);
    LOGI(TAG, "  Task Base Priority: %u\n",
         task_status_array[i].uxBasePriority);
    LOGI(TAG, "  Task State: %s\n",
         (task_status_array[i].eCurrentState == eRunning)   ? "Running"
         : (task_status_array[i].eCurrentState == eReady)   ? "Ready"
         : (task_status_array[i].eCurrentState == eBlocked) ? "Blocked"
                                                            : "Other");
    LOGI(TAG, "  Task Handle: 0x%X\n", task_status_array[i].xHandle);
    LOGI(TAG, "  Task Stack High Water Mark: %u\n",
         task_status_array[i].usStackHighWaterMark);
    LOGI(TAG, "\n");
  }

  // Free the allocated memory
  vPortFree(task_status_array);
}

void LOG_CURR_TASK() {
#if 0
  TaskHandle_t xHandle = xTaskGetCurrentTaskHandle();
  TaskStatus_t det;

  if (!xHandle) {
    LOGW("LOG_CURR_TASK", "Cannot determine current task handle");
    return;
  }

  static char cBuffer[512];
  vTaskGetRunTimeStats(cBuffer);
  LOGI("LOG_CURR_TASK", "System Task Info:\n\n%s", cBuffer);

  vTaskGetInfo(xHandle, &det, pdTRUE, eInvalid);
  LOGI(det.pcTaskName, "Name: %s (#%d), Prio: %d (%d), High watermark: %d", 
      det.pcTaskName, det.xTaskNumber, det.uxCurrentPriority, det.uxBasePriority, det.usStackHighWaterMark);
#else
  TaskHandle_t xHandle = xTaskGetCurrentTaskHandle();
  // LIST_TASKS();

  LOGV("LOG_CURR_TASK", "Task (%d), memory used: %d", xHandle,
       uxTaskGetStackHighWaterMark(NULL));
#endif
}

#endif
