//*********************************************************************************************************/
//  WT32-SC01-PLUS template for platform.io
//  created by Frits Jan / productbakery on 11 oktober 2022
//*********************************************************************************************************/

#ifdef APP_PENDANT

#include <stdio.h>

#include "Arduino.h"
#include "config.h"
#include "debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_ui.h"

// SET_LOOP_TASK_STACK_SIZE(1024 * 48);

#include "esp_freertos_hooks.h" // For xTaskCreateStaticWithCaps

static const char *TAG = "ESP32_CNC_HMI";

#include "driver/encoder.hpp"
#include "tasks/machine_response_proc_task.h"
#include "tasks/machine_send_task.h"
#include "tasks/machine_task.h"
#include "tasks/jog_accumulator_task.h"

extern void ram_usage();

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
      LOGI(TAG, "Data ENC delta: %d.", data->enc_diff);
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
#ifdef UPDATE_JOG_DIAL
#include "ui/tab_jog.h"
#endif

// DWC Mode specific includes
#ifdef DWC_MACHINE_MODE
#include <WiFi.h>

#include "config/dwc_settings.h"
#include "driver/wifi_manager.h"

// Global flag to communicate startup failure to UI
bool g_dwc_startup_connection_failed = false;
char g_dwc_startup_host[65] = {0};

#endif

#ifndef DWC_MACHINE_MODE
static machine_rrf_t machine_rrf;
static machine_interface_remote_t machine_remote;
#else
static machine_rrf_t
    machine_dwc;  // Unified struct, named for clarity in this mode
#endif
static multi_machine_interface_t machine;

static interface_t interface;

static TaskHandle_t lvgl_task_handle = NULL;

// LVGL objects are now static global to be accessible from setup() and lvgl_task()
static lv_display_t *display1 = NULL;
static lv_indev_t *indev = NULL;
static lv_indev_t *indev_encoder = NULL;
static lv_group_t *default_group = NULL;


extern "C" void test_ui(lv_obj_t *screen);

#include "driver/arduino_serial_wrapper.h"

#ifdef ESP32_HW
#include "driver/cam_transport_espnow.h"
#include "driver/cam_receiver.h"
#include "ui/components/lv_cam_stream.h"

static cam_transport_t *s_cam_transport = NULL;
static cam_receiver_t  *s_cam_receiver  = NULL;

static void cam_init(void)
{
    static const uint8_t cam_mac[] = CAM_MAC_ADDR;
    s_cam_transport = cam_transport_espnow_create(cam_mac);
    if (!s_cam_transport) { LOGE(TAG, "cam_transport_espnow_create failed"); return; }

    //cam_receiver_config_t cfg = { .transport = s_cam_transport, .max_width = 640, .max_height = 480 };
    cam_receiver_config_t cfg = { .transport = s_cam_transport, .max_width = TFT_HEIGHT, .max_height = TFT_WIDTH };
    s_cam_receiver = cam_receiver_create(&cfg);
    if (!s_cam_receiver) { LOGE(TAG, "cam_receiver_create failed"); return; }

    lv_cam_stream_set_default_receiver(s_cam_receiver);
    cam_receiver_start(s_cam_receiver);
    LOGI(TAG, "Camera receiver started");
}
#endif

#include "ui/touch_calib/ui_touch_calib.h"
#include "ui/touch_calib/touch_calib.h"

#ifndef DWC_MACHINE_MODE
bool machine_remote_init() {
  // Create and initialize the machine interface (RRF in this case)
  if (!machine_interface_remote_init(&machine_remote,
                                     (const uint8_t *)"\0\0\0\0\0\0")) {
    LOGE(TAG, "Failed to create remote machine interface");
    machine_rrf_deinit(&machine_rrf);  // Clean up if the loop somehow exits
    vTaskDelete(NULL);                 // Delete the task if creation fails
    return false;
  }

  if (!multi_machine_add_impl(&machine, &machine_remote.base)) {
    LOGE(TAG, "Failed to register remote machine interface");
    machine_interface_remote_deinit(
        &machine_remote);  // Clean up if the loop somehow exits
    return false;
  }
  LOGI(TAG, "Machine remote initialized");

  return true;
}

int rrf_uart_num = -1;
#endif

bool machine_init() {
  if (!multi_machine_interface_init(&machine)) {
    LOGE(TAG, "Failed to create multi machine interface");
    return false;
  }

#ifdef DWC_MACHINE_MODE
  dwc_settings_init();
  if (dwc_settings_load() && dwc_settings_are_valid()) {
    const dwc_settings_t *settings = dwc_settings_get();
    LOGI(TAG, "Found DWC settings. Connecting to WiFi '%s'...", settings->ssid);
    wifi_manager_init();
    if (wifi_manager_connect(settings->ssid, settings->password, 15000)) {
      LOGI(TAG, "WiFi connected. Resolving DWC host '%s'.", settings->host);

      char resolved_host_ip[16];
      if (wifi_manager_resolve_host(settings->host, resolved_host_ip,
                                    sizeof(resolved_host_ip))) {
        LOGI(TAG,
             "Initializing DWC machine interface with host %s (resolved from "
             "%s)",
             resolved_host_ip, settings->host);
        if (machine_rrf_init_dwc(&machine_dwc, resolved_host_ip, NULL,
                                 MACHINE_SEND_GCODE_INTERVAL_MS)) {
          multi_machine_add_impl(&machine, (machine_interface_t *)&machine_dwc);
        } else {
          LOGE(TAG, "Failed to initialize DWC machine interface.");
          g_dwc_startup_connection_failed = true;
          strncpy(g_dwc_startup_host, settings->host,
                  sizeof(g_dwc_startup_host) - 1);
        }
      } else {
        LOGE(TAG, "Failed to resolve host '%s'.", settings->host);
        g_dwc_startup_connection_failed = true;
        strncpy(g_dwc_startup_host, settings->host,
                sizeof(g_dwc_startup_host) - 1);
      }
    } else {
      LOGE(TAG, "Failed to connect to WiFi '%s'. Proceeding to manual setup.",
           settings->ssid);
      g_dwc_startup_connection_failed = true;
      strncpy(g_dwc_startup_host, settings->host,
              sizeof(g_dwc_startup_host) - 1);
    }
  } else {
    LOGI(TAG, "No valid DWC settings found. UI will start in setup mode.");
  }
#else
// --- RRF + Remote Mode ---
#ifndef RRF_SIM
  rrf_uart_num = RRF_SERIAL_UART_NUM;
#else
  rrf_uart_num = add_rrf_sim_serial();
#endif

#ifdef MACH_UART_PIN_TX
  if (!machine_rrf_init_serial(&machine_rrf, rrf_uart_num,
                               MACHINE_SEND_GCODE_INTERVAL_MS, MACH_UART_PIN_TX,
                               MACH_UART_PIN_RX)) {
    LOGE(TAG, "Failed to create RRF machine interface");
    return false;
  }
  if (!multi_machine_add_impl(&machine, &machine_rrf.base)) {
    LOGE(TAG, "Failed to add RRF machine to multi interface");
    machine_rrf_deinit(&machine_rrf);
    return false;
  }
#endif

  if (!machine_remote_init()) {
    LOGE(TAG, "Failed to initialize remote machine interface");
    return false;
  }
#endif
  return true;
}

#if defined(configUSE_TICK_HOOK) || defined(CONFIG_USE_TICK_HOOK)

#ifndef ESP32_HW
extern "C" {
void vApplicationIdleHook(void) { lv_tick_inc(1000 / CONFIG_FREERTOS_HZ); }
}
#else

#include "esp_freertos_hooks.h"
void lv_tick_task_esp(void) { lv_tick_inc(1000 / CONFIG_FREERTOS_HZ); }
#endif

#else

void lv_tick_task(void *pvParameters) {
  for (;;) {
    lv_tick_inc(1);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

#endif

static unsigned int ctr = 0;
void lvgl_task(void *pv_params) {
  // All initialization is now done in setup(). This task's sole purpose
  // is to run the LVGL handler and UI update loop.
  LOGI(TAG, "LVGL task started.");

  while (true) {
    auto time_start = millis();
    uint32_t sleep_time = lv_task_handler();
    vTaskDelay(sleep_time / portTICK_PERIOD_MS);

    // Handle deferred loading other lvgl_ui related functions that need to happen outside LVGL.
    lvgl_ui_task_handler();

    if ((ctr++ % 500) == 0) {
      LOGE(TAG, "LVGL task stack size high: %d\n",
           uxTaskGetStackHighWaterMark(lvgl_task_handle));
    }

    // Update all components that rely on data from machine_interface.
    // Run from main UI thread due to data races/crashes if directly called from
    // machine_interface_t callbacks.
    interface_tick(&interface);

    if (!encoder.isUiMode()) {
      int diff = encoder.readAndReset();
      if (diff != 0) {
#ifdef UPDATE_JOG_DIAL
        // interface->tab_jog->jog_dial->setValue(encoder.position());
        auto dial = interface.tab_jog->jog_dial;
        if (jog_dial_axis_selected(dial)) {
          jog_dial_apply_diff(dial, diff);
        }
#endif

        /* Post to the accumulator queue instead of issuing a G-Code
         * directly.  The jog_accumulator_task will batch these clicks,
         * send a single consolidated move, and self-throttle using a
         * kinematic sleep so the controller is never overwhelmed.       */
        jog_accumulator_post_clicks(diff);

        LOGI(TAG, "ENCODER DIFF %d", diff);
      }
    }

#ifndef USE_TICK_TASK
    auto time_end = millis();
    lv_tick_inc(time_end - time_start);
#endif
  }
}

#ifndef DWC_MACHINE_MODE
TaskHandle_t machine_rrf_proc_task_handle = NULL;
QueueHandle_t machine_rrf_proc_queue = NULL;
TaskHandle_t machine_rrf_task = NULL;

TaskHandle_t machine_remote_proc_task_handle = NULL;
QueueHandle_t machine_remote_proc_queue = NULL;
TaskHandle_t machine_remote_task = NULL;
#else
TaskHandle_t machine_dwc_task_handle = NULL;
#endif

TaskHandle_t machine_send_task_handle = NULL;
QueueHandle_t machine_send_queue = NULL;

void init_touch_cal() {
  LOGI(TAG, "LV_INIT TOUCHCAL");

  lv_init();

  LOGI(TAG, "LV_INIT DISPLAY");
  display1 = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
  indev = lv_indev_create();

  LOGI(TAG, "Display setup...");
  display_setup(display1, indev);
  LOGI(TAG, "Display setup... DONE");

  LOGI(TAG, "No touch calibration found — running calibration wizard");
  ui_run_touch_calib_blocking();

  // Infinite loop.
  while (true) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void init_lvgl() {
  LOGI(TAG, "LV_INIT");

  lv_init();

  LOGI(TAG, "LV_INIT DISPLAY");
  display1 = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
  indev = lv_indev_create();
  indev_encoder = lv_indev_create();

  LOGI(TAG, "Display setup...");
  display_setup(display1, indev);
  LOGI(TAG, "Display setup... DONE");

  lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(indev_encoder, encoder_indev_read);

  LOGI(TAG, "Setting up encoder scroll/value change...\n");
  default_group = lv_group_create();
  lv_indev_set_group(indev_encoder, default_group);
  lv_group_set_editing(default_group, true);
  lv_group_set_default(default_group);
  
  LOGI(TAG, "Creating Interface...\n");
  // If no touch calibration is present, run the calibration wizard first.
  while (!touch_calib_has()) {
    LOGI(TAG, "No touch calibration found — running calibration wizard");
    ui_run_touch_calib_blocking();
    /* If the wizard finishes without saving, loop will run it again. */
  }

  // Temporarily initialize with machine == NULL.
  interface_init(&interface, &machine.base);
  LOGI(TAG, "Interface loaded...\n");
}

void setup() {
  Serial.begin(115200);

  ram_usage();

  mcu_setup();
  mcu_startup();

  display_alloc();

  LOGI(TAG, "Loop task stack size high: %d\n",
       uxTaskGetStackHighWaterMark(NULL));

  bool abort = false;

  //touch_calib_clear(); // Uncomment to always bring up the touch calibration wizard for testing.
  if (!touch_calib_has()) {
    LOGI(TAG, "Touch calib not found... ");
    init_touch_cal();
  }

  LOGI(TAG, "Loading touch calib... ");
  touch_calib_load();

#ifdef ESP32_HW
  LOGI(TAG, "Initializing camera receiver...");
  cam_init();
#endif

  LOGI(TAG, "Creating Machine Interfaces... ");
  if (!abort && machine_init()) {
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "\nFAIL: Could not create Machine Task: error");
    abort = true;
  }

  // --- Start of UI Initialization moved to setup() ---
  init_lvgl();
  // --- End of UI Initialization ---

  LOGI(TAG, "Creating LVGL Task..\n");
#ifdef LVGL_TASK_PSRAM
  BaseType_t create_res = xTaskCreateWithCaps(
      lvgl_task,    // Function that implements the task
      "lvgl_task",  // Task name (for debugging)
      1024 * 12,    // Reduced stack size for runtime loop
      NULL,         // Task input parameter (not used here)
      tskIDLE_PRIORITY + 2,  // Task priority (adjust as needed) - higher than machine task
      &lvgl_task_handle,  // Task handle (optional, can be used to control the
                          // task)
      MALLOC_CAP_SPIRAM);
#else
  BaseType_t create_res = xTaskCreatePinnedToCore(
      lvgl_task,    // Function that implements the task
      "lvgl_task",  // Task name (for debugging)
      1024 * 10,    // Reduced stack size for runtime loop
      NULL,         // Task input parameter (not used here)
      tskIDLE_PRIORITY + 2,  // Task priority (adjust as needed) - higher than machine task
      &lvgl_task_handle,  // Task handle (optional, can be used to control the
                          // task)
      0);
#endif

  if (create_res == pdPASS) {
    LOGI(TAG, "DONE: Created LVGL Task..\n");
  } else {
    LOGE(TAG, "FAIL: Could not create LVGL Task: error %d", create_res);
    abort = true;
  }

  ram_usage();

#ifdef USE_TICK_TASK

#if !defined(configUSE_TICK_HOOK) && !defined(CONFIG_USE_TICK_HOOK)
  // Create tick task if USE_TICK_HOOK is not set.
  xTaskCreatePinnedToCore(
      lv_tick_task,    // Function that implements the task
      "lv_tick_task",  // Task name (for debugging)
      64,              // Stack size (adjust as needed, ESP32 it's bytes)
      NULL,            // Task input parameter (not used here)
      tskIDLE_PRIORITY +
          2,  // Task priority (adjust as needed) - higher than machine task
      NULL,   // Task handle (optional, can be used to control the
              // task)
      0);
  LOGI(TAG, "DONE: Tick Task Pinned...\n");
#elif defined(ESP32_HW)
  esp_register_freertos_tick_hook_for_cpu(&lv_tick_task_esp, 0);
  LOGI(TAG, "DONE: Tick Task...\n");
#endif
#endif

  ram_usage();

#ifdef DWC_MACHINE_MODE
  LOGI(TAG, "Creating DWC Machine Task... ");
  if (!abort && machine_task_run("MachineDWC", &machine_dwc.base,
                                 &machine_dwc_task_handle, TASK_MACHINE_CORE)) {
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "\nFAIL: Could not create DWC Machine Task: error");
    abort = true;
  }
  LOGI(TAG, "Creating DWC Machine GCode Sending Task... ");
  if (!abort && machine_send_task_run("MachineSendTask", &machine_dwc.base,
                                      &machine_send_task_handle,
                                      &machine_send_queue, TASK_MACHINE_CORE,
                                      4 * 1024, tskIDLE_PRIORITY + 5)) {
    machine_dwc.base.gcode_queue = machine_send_queue;
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "FAIL: Could not create DWC GCode Sending Task: error");
    abort = true;
  }
#else  // RRF Mode
  // Run a single machine task for the multi-machine wrapper. Child
  // implementations (RRF/Remote/DWC) are polled through the multi-machine
  // interface which decides which child is active. Running per-child
  // machine tasks causes duplicate polling and undesired probes (e.g.
  // machine_rrf sending probe queries while machine_remote is active).
  LOGI(TAG, "Creating Machine Task (multi-machine)... ");
  if (!abort && machine_task_run("Machine", &machine.base,
                                 &machine_rrf_task, TASK_MACHINE_CORE)) {
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "\nFAIL: Could not create Machine Task: error");
    abort = true;
  }

  ram_usage();

  LOGI(TAG, "Creating RRF Machine State Processing Task... ");
  if (!abort &&
      machine_response_proc_task_run(
          "MachineRRFProc", &machine_rrf.base, &machine_rrf_proc_task_handle,
          &machine_rrf_proc_queue, TASK_MACHINE_CORE,
          6 * 1024)) {  // RRF serial + cJSON on pendant
    if (!machine_rrf_setup_response_processing_task(&machine_rrf,
                                                    machine_rrf_proc_queue)) {
      LOGE(TAG, "Failed to set up response processing queue for RRF task");
    }
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "FAIL: Could not create RRF Machine Processing Task: error");
    abort = true;
  }

  ram_usage();

  LOGI(TAG, "Creating Remote Machine State Processing Task... ");
  if (!abort && machine_response_proc_task_run(
                    "MachineRemoteProc", &machine_remote.base,
                    &machine_remote_proc_task_handle,
                    &machine_remote_proc_queue, TASK_MACHINE_CORE,
                    4 * 1024)) {  // ESP-NOW binary dispatch only — no cJSON
    if (!machine_remote_setup_response_processing_task(
            &machine_remote, machine_remote_proc_queue)) {
      LOGE(TAG, "Failed to set up response processing queue for Remote task");
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
                    "MachineSendTask", &machine.base,  // Send to multi-machine
                    &machine_send_task_handle, &machine_send_queue,
                    TASK_MACHINE_CORE, 2 * 1024, tskIDLE_PRIORITY + 5)) {
    machine.base.gcode_queue = machine_send_queue;
    // Propagate the queue handle to all child machine interfaces
    for (size_t i = 0; i < machine.num_machines; ++i) {
      if (machine.machines[i]) {
        machine.machines[i]->gcode_queue = machine_send_queue;
      }
    }
    LOGI(TAG, "DONE\n");
  } else {
    LOGE(TAG, "FAIL: Could not create Machine GCode Sending Task: error");
    abort = true;
  }
#endif
#endif

  /* Start the adaptive jog accumulator.  Must be called after the G-Code
   * queue has been created and assigned to machine.base.gcode_queue.    */
  LOGI(TAG, "Starting jog accumulator task...");
  if (!abort && !jog_accumulator_init(&machine.base)) {
    LOGE(TAG, "FAIL: Could not start jog accumulator task");
    /* Non-fatal: jog_accumulator_post_clicks() is a no-op when not running,
     * so the pendant still works; the user just loses the batching benefit. */
  } else if (!abort) {
    LOGI(TAG, "DONE: Jog accumulator task running.");
  }

  LOGI(TAG, "Machine loaded..\n");

  if (abort) {
    multi_machine_interface_deinit(&machine);
#ifndef DWC_MACHINE_MODE
    machine_rrf_deinit(&machine_rrf);
    machine_interface_remote_deinit(&machine_remote);
#else
    machine_rrf_deinit(&machine_dwc);
#endif
  }

  // Update the machine, now that it is all set up.
  interface.machine = &machine.base;
}

void loop() {
  // Don't need to loop here.
  // Let FreeRTOS tasks handle their own loops.
  vTaskDelete(NULL);
}

#endif
