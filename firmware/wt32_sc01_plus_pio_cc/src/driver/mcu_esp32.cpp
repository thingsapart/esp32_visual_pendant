#ifdef ESP32_HW

#include "Arduino.h"

#include "sdkconfig.h"
#include <esp_task_wdt.h>

#include "debug.h"
#include "driver/arduino_serial_wrapper.h"

#if (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
# define HAS_CORE_DUMP
#else
# undef HAS_CORE_DUMP
# warning "CORE DUMP DISABLED" 
#endif

#ifdef HAS_CORE_DUMO

void print_reset_reason() {
    Serial.println("");
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.print("Reset Reason was: "); Serial.println(reason);

    switch (reason) {
        case ESP_RST_POWERON:
          Serial.println("Power ON");
        break;
        case ESP_RST_SW:
          Serial.println("Reset ESPrestart()");
        break;
        case ESP_RST_PANIC:Serial.println("Reset por exception/panic");break;
        case ESP_RST_UNKNOWN:Serial.println("Reset UNKNOW");break;
        case ESP_RST_EXT:Serial.println("Reset by external pin (not applicable for ESP32)");break;
        case ESP_RST_INT_WDT:Serial.println("Reset (software or hardware) por interrupção WATCHDOG");break;
        case ESP_RST_TASK_WDT:Serial.println("Reset WATCHDOG");break;
        case ESP_RST_WDT:Serial.println("Reset others WATCHDOG´s");break;
        case ESP_RST_DEEPSLEEP:Serial.println("Reset DEEP SLEEP MODE");break;
        case ESP_RST_BROWNOUT:Serial.println("Brownout reset (software or hardware)");break;
        case ESP_RST_SDIO:Serial.println("Reset over SDIO");break;
        default:
        break;
    }
}

#include "esp_core_dump.h"

void print_backtrace_info(const esp_core_dump_summary_t *coredump_summary) {
 if (coredump_summary != NULL)
  {
    esp_core_dump_bt_info_t bt_info = coredump_summary->exc_bt_info;

    char results[512]; // Assuming a maximum of 512 characters for the backtrace string
    int offset = snprintf(results, sizeof(results), "Traceback:\n\n");

    // for (int i = 0; i < bt_info.depth; i++)
    for (int i = bt_info.depth - 1; i >= 0; i--)
    {
      uintptr_t pc = bt_info.bt[i]; // Program Counter (PC)
      int len = snprintf(results + offset, sizeof(results) - offset, " 0x%08X", pc);
      if (len >= 0 && offset + len < sizeof(results))
      {
        offset += len;
      }
      else
      {
        break; // Reached the limit of the results buffer
      }
    }

    _df(0, "[backtrace]: %s", results);
    _df(0, "[backtrace]Backtrace Depth: %u", bt_info.depth);
    _df(0, "[backtrace]Backtrace Corrupted: %s", bt_info.corrupted ? "Yes" : "No");
    _df(0, "[backtrace]Program Counter: %d", coredump_summary->exc_pc);
    _df(0, "[backtrace]Coredump Version: %d", coredump_summary->core_dump_version);
  }
  else
  {
    _d(2, "Invalid core dump summary");
  }
}

void read_core_dump() {
    printf("Hello, world!\n");

    esp_core_dump_init();
    esp_core_dump_summary_t *summary = (esp_core_dump_summary_t*) malloc(sizeof(esp_core_dump_summary_t));
    if (summary) {
        esp_err_t err = esp_core_dump_get_summary(summary);
        if (err == ESP_OK) {
            printf("Getting core dump summary ok.");
            print_backtrace_info(summary);
        } else {
            printf("Getting core dump summary not ok. Error: %d\n", (int) err);
            printf("Probably no coredump present yet.\n");
            printf("esp_core_dump_image_check() = %d\n", esp_core_dump_image_check());
        }
        free(summary);
    }
}
#endif

void mcu_setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  add_standard_serial();

  delay(200);
}

void mcu_startup() {
  #ifdef HAS_CORE_DUMP
  print_reset_reason();
  #endif
}

#endif