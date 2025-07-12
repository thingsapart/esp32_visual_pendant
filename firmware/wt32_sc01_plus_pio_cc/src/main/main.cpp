//*********************************************************************************************************/
//  WT32-SC01-PLUS template for platform.io
//  created by Frits Jan / productbakery on 11 oktober 2022
//*********************************************************************************************************/

#ifdef PENDANT_DISPLAY

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Arduino.h"

#include "lvgl.h"

#include "config.h"
#include "debug.h"

// SET_LOOP_TASK_STACK_SIZE(1024 * 48);

static const char *TAG = "ESP32_CNC_HMI";

#include "driver/encoder.hpp"

#include "tasks/machine_response_proc_task.h"
#include "tasks/machine_task.h"
#include "tasks/machine_send_task.h"

static bool uiMode = false;
void encoder_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
  /*
  //if (uiMode == false && encoder.isUiMode() == true) {
  if (uiMode) {
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  */

  uiMode = encoder.isUiMode();
  data->state = LV_INDEV_STATE_RELEASED;

  if (!uiMode) {
    data->enc_diff = 0;
  } else {
    data->enc_diff = encoder.readAndReset();

    if (data->enc_diff != 0) {
      data->state = LV_INDEV_STATE_PRESSED;
#if DEBUG_ENCODER != 0
      LOGI(TAG,"Data ENC delta: %d.", data->enc_diff);
#endif
    } else {
      // data->state = LV_INDEV_STATE_RELEASED;
    }
  }
}

//************************************************************************************
//  SETUP AND LOOP
//************************************************************************************

#include "driver/driver_interface.hpp"

#include "machine/machine_interface.h"
#include "machine/machine_remote.h"
#include "machine/machine_rrf.h"
#include "machine/multi_machine_interface.h"
#include "ui/interface.h"
#include "ui/tab_jog.h"

static machine_rrf_t machine_rrf;
static machine_interface_remote_t machine_remote;
static multi_machine_interface_t machine;

static interface_t interface;

TaskHandle_t lvgl_task_handle = NULL;

/* Initialize the input device driver */
lv_indev_t *indev = NULL;
lv_display_t *display1 = NULL;
lv_group_t *default_group = NULL;

/* Initialize a second indev for the encoder wheel */
lv_indev_t *indev_encoder = NULL;

extern "C" void test_ui(lv_obj_t *screen);

#include "driver/arduino_serial_wrapper.h"

bool machine_remote_init() {
  // Create and initialize the machine interface (RRF in this case)
  if (!machine_interface_remote_init(&machine_remote,
                                     (const uint8_t *)"\0\0\0\0\0\0")) {
    _d(2, "Failed to create remote machine interface");
    machine_rrf_deinit(&machine_rrf); // Clean up if the loop somehow exits
    vTaskDelete(NULL);                // Delete the task if creation fails
    return false;
  }

  if (!multi_machine_add_impl(&machine, &machine_remote.base)) {
    _d(2, "Failed to register remote machine interface");
    machine_interface_remote_deinit(
        &machine_remote); // Clean up if the loop somehow exits
    return false;
  }
  LOGI(TAG, "Machine remote initialized");

  return true;
}

int rrf_uart_num = -1;

bool machine_init() {
#ifndef RRF_SIM
  rrf_uart_num = RRF_SERIAL_UART_NUM;
#else
  rrf_uart_num = add_rrf_sim_serial();
#endif
  // Create and initialize the machine interface (RRF in this case)
#ifdef MACH_UART_PIN_TX
  if (!machine_rrf_init(&machine_rrf, rrf_uart_num,
                        MACHINE_SEND_GCODE_INTERVAL_MS, MACH_UART_PIN_TX,
                        MACH_UART_PIN_RX)) {
    LOGE(TAG, "Failed to create machine interface");
    // vTaskDelete(NULL); // Delete the task if creation fails
    return false;
  }
#endif

  if (!multi_machine_interface_init(&machine)) {
    LOGE(TAG, "Failed to create remote machine interface");
    machine_rrf_deinit(&machine_rrf); // Clean up if the loop somehow exits
    machine_interface_remote_deinit(
        &machine_remote); // Clean up if the loop somehow exits
    // vTaskDelete(NULL); // Delete the task if creation fails
    return false;
  } else {
#ifdef MACH_UART_PIN_TX
    if (!multi_machine_add_impl(&machine, &machine_rrf.base)) {
      LOGE(TAG, "Failed to initialize local/remote distribution interface");
      multi_machine_interface_deinit(&machine);
      machine_rrf_deinit(&machine_rrf); // Clean up if the loop somehow exits
      // vTaskDelete(NULL); // Delete the task if creation fails
      return false;
    }
#endif
  }

  return true;
}

static unsigned int ctr = 0;
void lvgl_task(void *pv_params) {
  LOGI(TAG, "LV_INIT");
  lv_init();

  display1 = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
  indev = lv_indev_create();
  indev_encoder = lv_indev_create();

  display_setup(display1, indev);

  lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(indev_encoder, encoder_indev_read);

  LOGI(TAG, "Setting up encoder scroll/value change...\n");
  default_group = lv_group_create();
  lv_indev_set_group(indev_encoder, default_group);
  lv_group_set_editing(default_group, true);
  lv_group_set_default(default_group);

  LOGI(TAG, "Creating Interface...\n");
  interface_init(&interface, &machine.base);
  LOGI(TAG, "Interface loaded...\n");

  while (true) {
    auto time_start = millis();
    uint32_t sleep_time = lv_task_handler();
    if (sleep_time < 20) {
      sleep_time = 20;
    }
    vTaskDelay(sleep_time / portTICK_PERIOD_MS);

    auto time_end = millis();
    if ((ctr++ % 500) == 0) {
      LOGI(TAG, "LVGL task stack size high: %d\n",
          uxTaskGetStackHighWaterMark(lvgl_task_handle));
    }

    // Update all components that rely on data from machine_interface.
    // Run from main UI thread due to data races/crashes if directly called from
    // machine_interface_t callbacks.
    interface_tick(&interface);

    // interface_update_machine_state(&interface, &machine.base);

    if (!encoder.isUiMode()) {
      int diff = encoder.readAndReset();
      if (diff != 0) {
        // interface->tab_jog->jog_dial->setValue(encoder.position());
        auto dial = interface.tab_jog->jog_dial;
        if (jog_dial_axis_selected(dial)) {
          jog_dial_apply_diff(dial, diff);
        }

        machine_interface_step_current_axis(&machine.base, 3000, diff);

        LOGI(TAG, "ENCODER DIFF %d", diff);
      }
    }

    lv_tick_inc(time_end - time_start);
  }
}

TaskHandle_t machine_rrf_proc_task_handle = NULL;
QueueHandle_t machine_rrf_proc_queue = NULL;
TaskHandle_t machine_rrf_task = NULL;

TaskHandle_t machine_remote_proc_task_handle = NULL;
QueueHandle_t machine_remote_proc_queue = NULL;
TaskHandle_t machine_remote_task = NULL;

TaskHandle_t machine_send_task_handle = NULL;
QueueHandle_t machine_send_queue = NULL;

void setup() {
  Serial.begin(115200);

  mcu_setup();
  mcu_startup();

  machine_init();

  LOGI(TAG, "Loop task stack size high: %d\n", uxTaskGetStackHighWaterMark(NULL));

  bool abort = false;

  LOGI(TAG, "Creating LVGL Task..\n");
  // Create the FreeRTOS machine loop task.
  BaseType_t create_res = xTaskCreatePinnedToCore(
      lvgl_task,            // Function that implements the task
      "lvgl_task",          // Task name (for debugging)
      1024 * 50,            // Stack size (adjust as needed, ESP32 it's bytes)
      NULL,                 // Task input parameter (not used here)
      tskIDLE_PRIORITY + 2, // Task priority (adjust as needed) - higher than machine task
      &lvgl_task_handle,    // Task handle (optional, can be used to control the
                            // task)
      0);
  if (create_res == pdPASS) {
    LOGI(TAG, "DONE: Created LVGL Task..\n");
  } else {
    LOGE(TAG, "FAIL: Could not create LVGL Task: error %d", create_res);
    abort = true;
  }

  LOGI(TAG, "Creating Machine Tasks...\n");

  LOGI(TAG, "Creating Machine Interfaces... ");
  if (!abort && machine_init() && machine_remote_init()) {
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "\nFAIL: Could not create Machine Task: error");
    abort = true;
  }

  LOGI(TAG, "Creating RRF Machine Task... ");
  if (!abort && machine_task_run("MachineRRF", &machine.base, &machine_rrf_task,
                                 TASK_MACHINE_CORE)) {
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "\nFAIL: Could not create Machine Task: error");
    abort = true;
  }

  LOGI(TAG, "Creating Remote Machine Task... ");
  if (!abort && machine_task_run("MachineRemote", &machine.base, &machine_remote_task,
                                  TASK_MACHINE_CORE)) {
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "\nFAIL: Could not create Machine Task: error");
    abort = true;
  }

  LOGI(TAG, "Creating RRF Machine State Processing Task... ");
  if (!abort &&
      machine_response_proc_task_run(
          "MachineRRFProc", &machine_rrf.base, &machine_rrf_proc_task_handle,
          &machine_rrf_proc_queue, TASK_MACHINE_CORE)) {
    if (!machine_rrf_setup_response_processing_task(&machine_rrf,
                                                    machine_rrf_proc_queue)) {
      LOGE(TAG, "Failed to set up even processing queue for RRF task");
    }
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "FAIL: Could not create RRF Machine Processing Task: error");
    abort = true;
  }

  LOGI(TAG, "Creating Remote Machine State Processing Task... ");
  if (!abort && machine_response_proc_task_run(
                    "MachineRemoteProc", &machine_remote.base, &machine_remote_proc_task_handle,
                    &machine_remote_proc_queue, TASK_MACHINE_CORE)) {
    if (!machine_remote_setup_response_processing_task(
            &machine_remote, machine_remote_proc_queue)) {
      LOGE(TAG, "Failed to set up even processing queue for RRF task");
    }
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG,
         "FAIL: Could not create Remote Machine State Processing Task: error");
    abort = true;
  }


#ifdef ASYNC_GCODE_SENDING
  LOGI(TAG, "Creating Machine GCode Sending Task... ");
  if (!abort && machine_send_task_run(
                    "MachineSendTask", &machine_rrf.base, &machine_send_task_handle,
                    &machine_send_queue,
                    TASK_MACHINE_CORE, 2 * 1024, tskIDLE_PRIORITY + 5)) {
    machine_rrf.base.gcode_queue = machine_send_queue;
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG,
         "FAIL: Could not create Machine GCode Sending Task: error");
    abort = true;
  }
#endif

  LOGI(TAG, "Machine loaded..\n");

  if (abort) {
    multi_machine_interface_deinit(&machine);
    machine_rrf_deinit(&machine_rrf);
    machine_interface_remote_deinit(&machine_remote);
  }
}

void loop() {
  // Don't need to loop here.
  // Let FreeRTOS tasks handle their own loops.
  vTaskDelete(NULL);
}

#endif
