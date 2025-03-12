//*********************************************************************************************************/
//  WT32-SC01-PLUS template for platform.io
//  created by Frits Jan / productbakery on 11 oktober 2022
//
//
// When working with the squareline editor from lvgl, set the project in squareline to:
// - Arduino, with TFT_eSPI (which we cannot use, but will replace with LovyanGFX in this main.cpp file)
// - 480 x 320, 16 bit display
//
// Export the template project AND export the UI Files
// You will get a project directory with two directories inside, 'ui' and 'libraries'
// From the libraries directory, copy the lv_conf.h to this projects /src/ directory (overwrite the old one)
// From the ui directory, copy all files to this projects src/ui/ directory (you can empty the ui directory first if needed)
// The ui.ino file can/should be deleted because this main.cpp files takes over.
//
//*********************************************************************************************************/

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Arduino.h"

#include "lvgl.h"

#include "config.h"
#include "debug.h"

//SET_LOOP_TASK_STACK_SIZE(1024 * 48);

static const char *TAG = "ESP32_CNC_HMI";

#include "driver/encoder.hpp"

static bool uiMode = false;
void encoder_indev_read(lv_indev_t * indev, lv_indev_data_t * data) {
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
        _df(0, "Data ENC delta: %d.", data->enc_diff);
      #endif
    } else {
      //data->state = LV_INDEV_STATE_RELEASED;
    }
  }
}

//************************************************************************************
//  SETUP AND LOOP
//************************************************************************************

#include "driver/driver_interface.hpp"

#include "machine/machine_interface.h"
#include "machine/machine_rrf.h"
#include "machine/machine_remote.h"
#include "machine/multi_machine_interface.h"
#include "ui/tab_jog.h"
#include "ui/interface.h"

static machine_rrf_t machine_rrf;
static machine_interface_remote_t machine_remote;
static multi_machine_interface_t machine;

static interface_t interface;

TaskHandle_t machine_task_handle = NULL;
TaskHandle_t lvgl_task_handle = NULL;

/* Initialize the input device driver */
lv_indev_t * indev = NULL;
lv_display_t * display1 = NULL;
lv_group_t *default_group = NULL;

/* Initialize a second indev for the encoder wheel */
lv_indev_t * indev_encoder = NULL;

extern "C" void test_ui(lv_obj_t *screen);

#include "driver/arduino_serial_wrapper.h"

bool machine_remote_init() {
    // Create and initialize the machine interface (RRF in this case)
    if (!machine_interface_remote_init(&machine_remote, (const uint8_t *) "\0\0\0\0\0\0")) {
        _d(2, "Failed to create remote machine interface");
        machine_rrf_deinit(&machine_rrf); // Clean up if the loop somehow exits
        vTaskDelete(NULL); // Delete the task if creation fails
        return false;
    }

    if (!multi_machine_add_impl(&machine, &machine_remote.base)) {
        _d(2, "Failed to register remote machine interface");
      machine_interface_remote_deinit(&machine_remote); // Clean up if the loop somehow exits
      return false;
    }

    return true;
}

bool machine_init() {
#ifndef RRF_SIM
    int rrf_uart_num = RRF_SERIAL_UART_NUM;
#else
    int rrf_uart_num = add_rrf_sim_serial();
#endif
    // Create and initialize the machine interface (RRF in this case)
    if (!machine_rrf_init(&machine_rrf, rrf_uart_num, MACHINE_SEND_GCODE_INTERVAL_MS, MACH_UART_PIN_TX, MACH_UART_PIN_RX)) {
        _d(2, "Failed to create machine interface");
         //vTaskDelete(NULL); // Delete the task if creation fails
        return false;
    }
    
    if (!multi_machine_interface_init(&machine)) {
        _d(2, "Failed to create remote machine interface");
        machine_rrf_deinit(&machine_rrf); // Clean up if the loop somehow exits
        machine_interface_remote_deinit(&machine_remote); // Clean up if the loop somehow exits
        // vTaskDelete(NULL); // Delete the task if creation fails
        return false;
    } else {
      if (!multi_machine_add_impl(&machine, &machine_rrf.base)) {
        _d(2, "Failed to initialize local/remote distribution interface");
        multi_machine_interface_deinit(&machine);
        machine_rrf_deinit(&machine_rrf); // Clean up if the loop somehow exits
        // vTaskDelete(NULL); // Delete the task if creation fails
        return false;
      }
    }

    return true;
}

// Function that will run as the FreeRTOS task calling machine_interface_setup_lookp infinitely.
void machine_task(void *pvParameters) {
    LOGI(TAG, ">> Starting machine task...");
    if (machine_remote_init()) {
      LOGI(TAG, "  >> Success... running loop.");
      // Call the setup loop function (this will run indefinitely)
      machine_interface_setup_loop(&machine.base);
    }
    LOGI(TAG, "<< Machine Task Loop Ended?");

    // Should never reach here, but good practice to include
    _d(2, "Machine task exiting (should not happen)");
    multi_machine_interface_deinit(&machine);
    machine_rrf_deinit(&machine_rrf); // Clean up if the loop somehow exits
    machine_interface_remote_deinit(&machine_remote); // Clean up if the loop somehow exits
    vTaskDelete(NULL);
}

static unsigned int ctr = 0;
void lvgl_task(void *pv_params) {
  _d(0, "LV_INIT");
  lv_init();

  display1 = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
  indev = lv_indev_create();
  indev_encoder = lv_indev_create();

  display_setup(display1, indev);

  lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(indev_encoder, encoder_indev_read);

  _d(0, "Setting up encoder scroll/value change...\n");
  default_group = lv_group_create();
  lv_indev_set_group(indev_encoder, default_group);
  lv_group_set_editing(default_group, true);
  lv_group_set_default(default_group);

  _d(0, "Creating Interface...\n");
  interface_init(&interface, &machine.base);
  _d(0, "Interface loaded...\n");

  while (true) {
    auto time_start = millis();
    uint32_t sleep_time = lv_task_handler();
    if (sleep_time < 20) { sleep_time = 20; }
    vTaskDelay(sleep_time / portTICK_PERIOD_MS);

    auto time_end = millis();
    if ((ctr++ % 500) == 0) {
      _df(0, "LVGL task stack size high: %d\n", uxTaskGetStackHighWaterMark(lvgl_task_handle));
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
        if (jog_dial_axis_selected(dial)) { jog_dial_apply_diff(dial, diff); }

        machine_interface_step_current_axis(&machine.base, 3000, diff);

        _df(0, "ENCODER DIFF %d", diff);
      }
    }

    lv_tick_inc(time_end - time_start);
  }
}

void setup() {
  mcu_setup();
  mcu_startup();

  machine_init();

  //_df(0, "Loop task stack size high: %d\n", uxTaskGetStackHighWaterMark(NULL));

  _d(0, "Creating LVGL Task..\n");
  // Create the FreeRTOS machine loop task.
  BaseType_t create_res = xTaskCreatePinnedToCore(
      lvgl_task,           // Function that implements the task
      "lvgl_task",         // Task name (for debugging)
      1024 * 44,           // Stack size in words (adjust as needed)
      NULL,                // Task input parameter (not used here)
      10,                  // Task priority (adjust as needed) - higher than machine task
      &lvgl_task_handle,   // Task handle (optional, can be used to control the task)
      0
  );
  if (create_res == pdPASS) {
    _d(0, "DONE: Created LVGL Task..\n");
  } else {
    LOGE(TAG, "FAIL: Could not create LVGL Task: error %d", create_res);
  }

  _d(0, "Creating Machine Task..\n");
  // Create the FreeRTOS machine loop task.
  create_res = xTaskCreatePinnedToCore(
      machine_task,         // Function that implements the task
      "machine_task",       // Task name (for debugging)
      1024 * 28,            // Stack size in words (adjust as needed)
      NULL,                 // Task input parameter (not used here)
      5,                    // Task priority (adjust as needed)
      &machine_task_handle, // Task handle (optional, can be used to control the task)
      1
  );
  if (create_res == pdPASS) {
    _d(0, "DONE: Created Machine Task..\n");
  } else {
    LOGE(TAG, "FAIL: Could not create Machine Task: error %d", create_res);
  }

  // start the UI
  _d(0, "Machine loaded..\n");
}

void loop() {
  // Don't need to loop here.
  // Let FreeRTOS tasks handle their own loops.
  vTaskDelete(NULL);
}
