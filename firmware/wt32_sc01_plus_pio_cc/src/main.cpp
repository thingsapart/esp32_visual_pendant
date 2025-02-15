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
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "Arduino.h"

SET_LOOP_TASK_STACK_SIZE(1024 * 32);

static const char *TAG = "ESP32_CNC_HMI";

// #define LGFX_USE_V1
#include <LovyanGFX.hpp>

// SETUP LGFX PARAMETERS FOR WT32-SC01-PLUS
class LGFX : public lgfx::LGFX_Device
{

lgfx::Panel_ST7796      _panel_instance;
lgfx::Bus_Parallel8     _bus_instance;
lgfx::Light_PWM         _light_instance;
lgfx::Touch_FT5x06      _touch_instance; // FT5206, FT5306, FT5406, FT6206, FT6236, FT6336, FT6436

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();

      cfg.freq_write = 20000000;
      cfg.pin_wr = 47; // pin number connecting WR
      cfg.pin_rd = -1; // pin number connecting RD
      cfg.pin_rs = 0;  // Pin number connecting RS(D/C)
      cfg.pin_d0 = 9;  // pin number connecting D0
      cfg.pin_d1 = 46; // pin number connecting D1
      cfg.pin_d2 = 3;  // pin number connecting D2
      cfg.pin_d3 = 8;  // pin number connecting D3
      cfg.pin_d4 = 18; // pin number connecting D4
      cfg.pin_d5 = 17; // pin number connecting D5
      cfg.pin_d6 = 16; // pin number connecting D6
      cfg.pin_d7 = 15; // pin number connecting D7
      //cfg.i2s_port = I2S_NUM_0; // (I2S_NUM_0 or I2S_NUM_1)

      _bus_instance.config(cfg);                    // Apply the settings to the bus.
      _panel_instance.setBus(&_bus_instance);       // Sets the bus to the panel.
    }

    { // Set display panel control.
      auto cfg = _panel_instance.config(); // Get the structure for display panel settings.

      cfg.pin_cs = -1;   // Pin number to which CS is connected (-1 = disable)
      cfg.pin_rst = 4;   // pin number where RST is connected (-1 = disable)
      cfg.pin_busy = -1; // pin number to which BUSY is connected (-1 = disable)

      // * The following setting values ​​are set to general default values ​​for each panel, and the pin number (-1 = disable) to which BUSY is connected, so please try commenting out any unknown items.

      cfg.memory_width = 320;  // Maximum width supported by driver IC
      cfg.memory_height = 480; // Maximum height supported by driver IC
      cfg.panel_width = 320;   // actual displayable width
      cfg.panel_height = 480;  // actual displayable height
      cfg.offset_x = 0;        // Panel offset in X direction
      cfg.offset_y = 0;        // Panel offset in Y direction
      cfg.offset_rotation = 0;  // was 2
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;     // was false
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true; // was false something to do with SD?

      _panel_instance.config(cfg);
    }

    { // Set backlight control. (delete if not necessary)
      auto cfg = _light_instance.config(); // Get the structure for backlight configuration.

      cfg.pin_bl = 45;     // pin number to which the backlight is connected
      cfg.invert = false;  // true to invert backlight brightness
      cfg.freq = 44100;    // backlight PWM frequency
      cfg.pwm_channel = 0; // PWM channel number to use (7??)

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance); // Sets the backlight to the panel.
    }

//*
    { // Configure settings for touch screen control. (delete if not necessary)
      auto cfg = _touch_instance.config();

      cfg.x_min = 0;   // Minimum X value (raw value) obtained from the touchscreen
      cfg.x_max = 319; // Maximum X value (raw value) obtained from the touchscreen
      cfg.y_min = 0;   // Minimum Y value obtained from touchscreen (raw value)
      cfg.y_max = 479; // Maximum Y value (raw value) obtained from the touchscreen
      cfg.pin_int = 7; // pin number to which INT is connected
      cfg.bus_shared = false; // set true if you are using the same bus as the screen
      cfg.offset_rotation = 0;

      // For I2C connection
      cfg.i2c_port = 0;    // Select I2C to use (0 or 1)
      cfg.i2c_addr = 0x38; // I2C device address number
      cfg.pin_sda = 6;     // pin number where SDA is connected
      cfg.pin_scl = 5;     // pin number to which SCL is connected
      cfg.freq = 400000;   // set I2C clock

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance); // Set the touchscreen to the panel.
    }
//*/
    setPanel(&_panel_instance); // Sets the panel to use.
  }
};

#include <lvgl.h>

LGFX tft;

#define screenWidth 480
#define screenHeight 320

// lv debugging can be set in lv_conf.h
#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char * buf)
{
    Serial.printf(buf);
    Serial.flush();
}
#endif


/* Declare buffer for 1/10 screen size; BYTES_PER_PIXEL will be 2 for RGB565. */
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
static uint8_t buf1[screenWidth * screenHeight / 10 * BYTES_PER_PIXEL];

/* Display flushing */
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t * px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((lgfx::rgb565_t *)px_map, w * h);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

/* Initialize the input device driver */
lv_indev_t * indev = NULL;
lv_display_t * display1 = NULL;

/* Initialize a second indev for the encoder wheel */
lv_indev_t * indev_encoder = NULL;        /* Create input device connected to Default Display. */

/*Read the touchpad*/
#define DEBUG_TOUCH 0
#define DEBUG_ENCODER 0

void touch_indev_read(lv_indev_t * indev, lv_indev_data_t * data) {
    uint16_t touchX, touchY;
    bool touched = tft.getTouch(&touchX, &touchY);
    if (!touched) { data->state = LV_INDEV_STATE_REL; }
    else {
      data->state = LV_INDEV_STATE_PR;
      data->point.x = touchX;
      data->point.y = touchY;

      #if DEBUG_TOUCH != 0
      Serial.print( "Data x " ); Serial.println( touchX );
      Serial.print( "Data y " ); Serial.println( touchY );
      #endif
    }
}

#include "machine/encoder.hpp"

void encoder_indev_read(lv_indev_t * indev, lv_indev_data_t * data) {
  if (!encoder.isUiMode()) {
    data->enc_diff = 0;
  } else {
    data->enc_diff = encoder.readAndReset();
    
    if (data->enc_diff > 0) {
      #if DEBUG_ENCODER != 0
        Serial.print( "Data ENC delta: " ); Serial.println( data->enc_diff );
      #endif
    }
  }
  data->state = LV_INDEV_STATE_RELEASED;
}

//************************************************************************************
//  SETUP AND LOOP
//************************************************************************************

#include "machine/machine_interface.h"
#include "machine/machine_rrf.h"
#include "ui/interface.h"
#include "debug.h"

static machine_rrf_t machine;
static interface_t interface;

void test_screen() {
    lv_obj_t * label1 = lv_label_create(lv_screen_active());
    lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);     /*Break the long lines*/
    lv_label_set_text(label1, "Recolor is not supported for v9 now.");
    lv_obj_set_width(label1, 150);  /*Set smaller width to make the lines wrap*/
    lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t * label2 = lv_label_create(lv_screen_active());
    lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);     /*Circular scroll*/
    lv_obj_set_width(label2, 150);
    lv_label_set_text(label2, "It is a circularly scrolling text. ");
    lv_obj_align(label2, LV_ALIGN_CENTER, 0, 40);
}

#include <esp_task_wdt.h>

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

void print_backtrace_info(const esp_core_dump_summary_t *coredump_summary)
{
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

extern "C" void test_ui(lv_obj_t *screen);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  delay(1000);
  Serial.println("TESTING...");

  _d(0, "LV_INIT");
  lv_init();

  tft.begin();
  tft.setRotation(1);
  tft.setBrightness(255);

  display1 = lv_display_create(screenWidth, screenHeight);
  indev = lv_indev_create();
  indev_encoder = lv_indev_create();

  #if LV_USE_LOG != 0
    lv_log_register_print_cb( my_print ); /* register print function for debugging */
  #endif

  /* Set display buffer for display `display1`. */
  lv_display_set_buffers(display1, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display1, my_disp_flush);

  /*Initialize the input device driver*/
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_indev_read);

  lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(indev_encoder, encoder_indev_read);

  _d(0, "Machine loaded..\n");
  print_reset_reason(); 

  // start the UI
  machine_rrf_init(&machine, 200, MACH_UART_PIN_TX, MACH_UART_PIN_RX);

  interface_init(&interface, &machine.base);

  // test_ui(lv_screen_active());
  //test_screen();

  _d(0, "Interface loaded..\n");

  _df(0, "Loop task stack size high: %d\n", uxTaskGetStackHighWaterMark(NULL))
}

static unsigned int ctr = 0;
void loop() {
  auto time_start = millis();
  uint32_t sleep_time = lv_task_handler();
  delay(sleep_time);
  auto time_end = millis();
  if ((ctr++ % 10) == 0) {
    _df(0, "Loop task stack size high: %d\n", uxTaskGetStackHighWaterMark(NULL))
  }
  if (!encoder.isUiMode()) {
    int diff = encoder.readAndReset();
    if (diff != 0) { 
      // interface->tab_jog->jog_dial->setValue(encoder.position());
      auto dial = interface.tab_jog->jog_dial;
      if (jog_dial_axis_selected(dial)) { jog_dial_apply_diff(dial, diff); }

      _df(0, "ENCODER DIFF %d", diff); 
    }
  }

  lv_tick_inc(time_end - time_start);
}
