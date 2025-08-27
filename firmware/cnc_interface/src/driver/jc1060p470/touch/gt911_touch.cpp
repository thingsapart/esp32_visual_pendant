#ifdef JC1060P470

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h" // New I2C master driver header
#include "esp_lcd_touch_gt911.h"
#include "gt911_touch.h"
#include "Arduino.h"

#include "debug.h"

#define CONFIG_LCD_HRES TFT_HEIGHT
#define CONFIG_LCD_VRES TFT_WIDTH

// Define the I2C port to be used
#define I2C_MASTER_NUM I2C_NUM_0

static const char *TAG = "gt911";

esp_lcd_touch_handle_t tp;
// The tp_io_handle is still needed for the esp_lcd_touch layer
esp_lcd_panel_io_handle_t tp_io_handle;

// The bus handle should be stored if you plan to deinitialize it later.
// For this example, it's created and used within begin(), which is sufficient.
// i2c_master_bus_handle_t i2c_bus_handle;

uint16_t touch_strength[1];
uint8_t touch_cnt = 0;

gt911_touch::gt911_touch(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin)
{
    _sda = sda_pin;
    _scl = scl_pin;
    _rst = rst_pin;
    _int = int_pin;
}

void gt911_touch::begin()
{
    Serial.println("GT911 Touch begin");
    LOGI(TAG, "Initialize I2C bus using new driver");

    // 1. Create I2C master bus configuration
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = (gpio_num_t)_sda,
        .scl_io_num = (gpio_num_t)_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, // Default value
        .flags = {
            .enable_internal_pullup = true,
        }
    };

    // 2. Create I2C master bus handle
    i2c_master_bus_handle_t i2c_bus_handle;
    ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));

    LOGI(TAG, "I2C bus created on port %d", I2C_MASTER_NUM);

    // 3. Create I2C panel IO configuration for the touch device
    // The ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() macro sets the device address.
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    // In the new driver, clock speed is a device-specific property.
    // It must be set in the panel IO config.
    tp_io_config.scl_speed_hz = 400000; // 400kHz

    LOGI(TAG, "Initialize touch IO (I2C)");
    // 4. Create the panel IO handle, passing the new bus handle
    // The esp_lcd component will now add the GT911 as a device to the bus.
    ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle));

    // --- The rest of the initialization is the same ---

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = CONFIG_LCD_HRES,
        .y_max = CONFIG_LCD_VRES,
        .rst_gpio_num = (gpio_num_t)_rst,
        .int_gpio_num = (gpio_num_t)_int,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    LOGI(TAG, "Initialize touch controller gt911");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));
}

bool gt911_touch::getTouch(uint16_t *x, uint16_t *y)
{
    // This function does not need to change as it uses the higher-level
    // esp_lcd_touch API, which abstracts the underlying I2C communication.
    ERROR_CHECK(esp_lcd_touch_read_data(tp));
    bool touch_pressed = esp_lcd_touch_get_coordinates(tp, x, y, touch_strength, &touch_cnt, 1);
    if (touch_cnt > 0)
    {
        // LOGI(TAG, "Touch detected"); // This can be very noisy
        return true;
    }
    // 'touch_pressed' only indicates if the INT line is active, not necessarily new coordinates.
    // 'touch_cnt > 0' is the most reliable check.
    return false;
}

void gt911_touch::set_rotation(uint8_t r){
    // This function does not need to change.
    switch(r){
    case 0:
        esp_lcd_touch_set_swap_xy(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
        esp_lcd_touch_set_mirror_y(tp, false);
        break;
    case 1:
        esp_lcd_touch_set_swap_xy(tp, false);
        esp_lcd_touch_set_mirror_x(tp, true);
        esp_lcd_touch_set_mirror_y(tp, true);
        break;
    case 2:
        esp_lcd_touch_set_swap_xy(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
        esp_lcd_touch_set_mirror_y(tp, false);
        break;
    case 3:
        esp_lcd_touch_set_swap_xy(tp, false);
        esp_lcd_touch_set_mirror_x(tp, true);
        esp_lcd_touch_set_mirror_y(tp, true);
        break;
    }
}

#endif // JC1060P470