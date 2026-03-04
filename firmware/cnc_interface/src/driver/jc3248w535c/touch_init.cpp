// Touch hardware initialisation and LVGL indev read callback for the
// JC3248W535C (AXS15231B integrated touch, I2C address 0x3B).
//
// Three implementations are selected at build time via flags:
//
//   ESP32_LVGL_ESP_DISP defined
//       → esp_panel::drivers::TouchAXS15231B, bus created by the library.
//
//   JC3248W535C_ESP_DISP_TOUCH defined (without ESP32_LVGL_ESP_DISP)
//       → esp_panel::drivers::TouchAXS15231B, I2C bus created manually.
//
//   JC3248W535C_TOUCH_DRV defined
//       → AXS15231B_Touch Arduino-style class.
//
//   (default – neither of the above)
//       → Raw I2C via esp-idf i2c_master APIs.

#ifdef JC3248W535C

#include "touch_init.h"
#include "debug.h"

#include "esp_heap_caps.h"

static const char *TAG = "JC3248W535C/touch";

#define DEBUG_TOUCH 0

/* ── Build-path includes ───────────────────────────────────────────────────*/

#if defined(ESP32_LVGL_ESP_DISP) || defined(JC3248W535C_ESP_DISP_TOUCH)

#include <memory>
#include "driver/i2c.h"
#include "esp_display_panel.hpp"
#include "drivers/touch/esp_panel_touch_axs15231b.hpp"

static std::shared_ptr<esp_panel::drivers::Touch> s_touch = nullptr;

#elif defined(JC3248W535C_TOUCH_DRV)

#include "driver/axs15231b_touch.h"

#define TFT_rot   1
#define TFT_res_W 480
#define TFT_res_H 320

static AXS15231B_Touch s_touch(Touch_SCL, Touch_SDA, Touch_INT, Touch_ADDR, TFT_rot);

#else // raw I2C

#include "driver/i2c.h"

#endif

/* ── AXS15231B raw I2C touch-read trigger command ──────────────────────────
 * Write this 11-byte payload, then read 8 bytes back.
 * Data layout of the 8-byte response (AXS15231B datasheet):
 *   [0] gesture   [1] num_touches
 *   [2] x_h ([3:0] = x[11:8])  [3] x_l (x[7:0])
 *   [4] y_h ([3:0] = y[11:8])  [5] y_l (y[7:0])
 *   [6] pressure  [7] area
 */
#if !defined(ESP32_LVGL_ESP_DISP) && !defined(JC3248W535C_ESP_DISP_TOUCH) && \
    !defined(JC3248W535C_TOUCH_DRV)
static const uint8_t AXS_TOUCH_READ_CMD[11] = {
    0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00
};
#endif

/* ── touch_hw_init ─────────────────────────────────────────────────────────*/

bool touch_hw_init() {
#if defined(ESP32_LVGL_ESP_DISP)

    // ── ESP32_Display_Panel path ──────────────────────────────────────────
    esp_panel::drivers::BusI2C::Config i2c_cfg = {
        .host_id = (i2c_port_t)I2C_TOUCH_PORT,
        .host = esp_panel::drivers::BusI2C::HostPartialConfig{
            .sda_io_num    = TOUCH_SDA,
            .scl_io_num    = TOUCH_SCL,
            .sda_pullup_en = true,
            .scl_pullup_en = true,
            .clk_speed     = I2C_TOUCH_FREQUENCY,
        },
        .control_panel = ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(AXS15231B),
    };
    esp_panel::drivers::BusFactory::Config touch_bus_cfg(i2c_cfg);

    esp_panel::drivers::TouchAXS15231B::Config touch_cfg;
    touch_cfg.device = esp_panel::drivers::Touch::DevicePartialConfig{
        .x_max        = 480,
        .y_max        = 320,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
    };

    s_touch = std::make_shared<esp_panel::drivers::TouchAXS15231B>(
        touch_bus_cfg, touch_cfg);
    if (s_touch && s_touch->init() && s_touch->begin()) {
        s_touch->swapXY(true);
        LOGI(TAG, "touch ready (esp_panel, factory bus)");
        return true;
    }

    LOGW(TAG, "touch factory init failed, falling back to manual I2C bus");
    esp_panel::drivers::BusI2C::ControlPanelFullConfig cp_cfg =
        ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(AXS15231B);
    auto i2c_bus = std::make_shared<esp_panel::drivers::BusI2C>(
        TOUCH_SCL, TOUCH_SDA, cp_cfg);
    if (!i2c_bus->init() || !i2c_bus->begin()) {
        LOGE(TAG, "failed to init manual I2C bus");
        return false;
    }
    s_touch.reset();
    s_touch = std::make_shared<esp_panel::drivers::TouchAXS15231B>(
        i2c_bus.get(), touch_cfg);
    if (!s_touch || !s_touch->init() || !s_touch->begin()) {
        LOGE(TAG, "touch init failed (manual bus)");
        return false;
    }
    s_touch->swapXY(true);
    LOGI(TAG, "touch ready (esp_panel, manual bus)");
    return true;

#elif defined(JC3248W535C_ESP_DISP_TOUCH)

    // ── ESP32_Display_Panel touch driver only (IDF-native LCD path) ───────
    esp_panel::drivers::BusI2C::Config i2c_cfg = {
        .host_id = (i2c_port_t)I2C_TOUCH_PORT,
        .host = esp_panel::drivers::BusI2C::HostPartialConfig{
            .sda_io_num    = TOUCH_SDA,
            .scl_io_num    = TOUCH_SCL,
            .sda_pullup_en = true,
            .scl_pullup_en = true,
            .clk_speed     = I2C_TOUCH_FREQUENCY,
        },
        .control_panel = ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(AXS15231B),
    };
    esp_panel::drivers::BusFactory::Config touch_bus_cfg(i2c_cfg);

    esp_panel::drivers::TouchAXS15231B::Config touch_cfg;
    touch_cfg.device = esp_panel::drivers::Touch::DevicePartialConfig{
        .x_max        = 480,
        .y_max        = 320,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
    };

    s_touch = std::make_shared<esp_panel::drivers::TouchAXS15231B>(
        touch_bus_cfg, touch_cfg);
    if (s_touch && s_touch->init() && s_touch->begin()) {
        s_touch->swapXY(true);
        LOGI(TAG, "touch ready (esp_panel touch, factory bus)");
        return true;
    }

    LOGW(TAG, "touch factory init failed, falling back to manual I2C bus");
    esp_panel::drivers::BusI2C::ControlPanelFullConfig cp_cfg =
        ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(AXS15231B);
    auto i2c_bus = std::make_shared<esp_panel::drivers::BusI2C>(
        TOUCH_SCL, TOUCH_SDA, cp_cfg);
    if (!i2c_bus->init() || !i2c_bus->begin()) {
        LOGE(TAG, "failed to init manual I2C bus");
        return false;
    }
    s_touch.reset();
    s_touch = std::make_shared<esp_panel::drivers::TouchAXS15231B>(
        i2c_bus.get(), touch_cfg);
    if (!s_touch || !s_touch->init() || !s_touch->begin()) {
        LOGE(TAG, "touch init failed (manual bus)");
        return false;
    }
    s_touch->swapXY(true);
    LOGI(TAG, "touch ready (esp_panel touch only, manual bus)");
    return true;

#elif defined(JC3248W535C_TOUCH_DRV)

    // ── Arduino-style AXS15231B touch class ──────────────────────────────
    if (!s_touch.begin()) {
        LOGE(TAG, "AXS15231B_Touch::begin() failed");
        return false;
    }
    LOGI(TAG, "touch ready (AXS15231B_Touch driver)");
    return true;

#else

    // ── Raw I2C via ESP-IDF i2c_master ────────────────────────────────────
    // Install the I2C driver for the touch I2C bus.
    i2c_config_t i2c_cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TOUCH_SDA,
        .scl_io_num       = TOUCH_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed    = I2C_TOUCH_FREQUENCY,
        },
    };
    esp_err_t err = i2c_param_config((i2c_port_t)I2C_TOUCH_PORT, &i2c_cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "i2c_param_config: %s", esp_err_to_name(err));
        return false;
    }
    err = i2c_driver_install((i2c_port_t)I2C_TOUCH_PORT,
                              I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        LOGE(TAG, "i2c_driver_install: %s", esp_err_to_name(err));
        return false;
    }
    LOGI(TAG, "touch ready (raw I2C, port %d, addr 0x%02X)",
         I2C_TOUCH_PORT, I2C_TOUCH_ADDRESS);
    return true;

#endif
}

/* ── touch_indev_read ──────────────────────────────────────────────────────*/

#include "ui/touch_calib/touch_calib.h"

void touch_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {

    float fx = 0.0f, fy = 0.0f;

#if defined(ESP32_LVGL_ESP_DISP)

    esp_panel::drivers::TouchPoint point;
    // ── ESP32_Display_Panel path ──────────────────────────────────────────
    if (!s_touch) {
        data->state = LV_INDEV_STATE_REL;
        goto done;
    }
    if (s_touch->readPoints(&point, 1, 0) > 0) {
        data->state = LV_INDEV_STATE_PR;
        // No LVGL rotation — map portrait hardware coords to landscape explicitly.
        // swapXY(true) gives: point.x = portrait_y ∈ [0,480), point.y = portrait_x ∈ [0,320)
        // 90° CW: landscape_x = portrait_y, landscape_y = (TFT_HEIGHT-1) - portrait_x
        fx = (float)point.x;
        fy = (float)(TFT_HEIGHT - 1) - (float)point.y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

#elif defined(JC3248W535C_ESP_DISP_TOUCH)

    esp_panel::drivers::TouchPoint point;

    // ── esp_panel touch driver only (IDF-native LCD path) ────────────────
    if (!s_touch) {
        data->state = LV_INDEV_STATE_REL;
        goto done;
    }
    if (s_touch->readPoints(&point, 1, 0) > 0) {
        data->state = LV_INDEV_STATE_PR;
        // No LVGL rotation — map portrait hardware coords to landscape explicitly.
        // swapXY(true) gives: point.x = portrait_y ∈ [0,480), point.y = portrait_x ∈ [0,320)
        // 90° CW: landscape_x = portrait_y, landscape_y = (TFT_HEIGHT-1) - portrait_x
        fx = (float)point.x;
        fy = (float)(TFT_HEIGHT - 1) - (float)point.y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

#elif defined(JC3248W535C_TOUCH_DRV)

    // ── Arduino AXS15231B_Touch ───────────────────────────────────────────
    uint16_t x, y;
    if (s_touch.touched()) {
        s_touch.readData(&x, &y);
        fx = x;
        fy = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

#else

    // ── Raw I2C ───────────────────────────────────────────────────────────
    // Write read-trigger command
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_TOUCH_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, AXS_TOUCH_READ_CMD, sizeof(AXS_TOUCH_READ_CMD), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin((i2c_port_t)I2C_TOUCH_PORT, cmd,
                                          pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        data->state = LV_INDEV_STATE_REL;
        goto done;
    }

    // Read 8 bytes of touch data
    uint8_t td[8] = {0};
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_TOUCH_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, td, sizeof(td), I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin((i2c_port_t)I2C_TOUCH_PORT, cmd,
                                pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK || td[1] == 0) {
        data->state = LV_INDEV_STATE_REL;
        goto done;
    }

    // Decode coordinates (native portrait 320×480)
    uint16_t raw_x = ((uint16_t)(td[2] & 0x0F) << 8) | td[3];
    uint16_t raw_y = ((uint16_t)(td[4] & 0x0F) << 8) | td[5];

    // No LVGL rotation — map portrait hardware coords to landscape.
    // 90° CW: landscape_x = raw_y (portrait row), landscape_y = (TFT_HEIGHT-1) - raw_x
    fx = (float)raw_y;
    fy = (float)(TFT_HEIGHT - 1) - (float)raw_x;
    data->state   = LV_INDEV_STATE_PR;

#endif // touch path selection

    // Now apply calibration to the coordinates we just computed.
    touch_calib_apply_inplace(&fx, &fy);
    data->point.x = (lv_coord_t)fx;
    data->point.y = (lv_coord_t)fy;

done:
#if DEBUG_TOUCH != 0
    if (data->state == LV_INDEV_STATE_REL) {
        LOGI(TAG, "TOUCH: RELEASED");
    } else {
        LOGI(TAG, "TOUCH: PRESSED - (%d,%d)", data->point.x, data->point.y);
    }
#endif
}

#endif // JC3248W535C
