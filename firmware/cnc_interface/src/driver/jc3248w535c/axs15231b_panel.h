/*
 * AXS15231B LCD panel driver — LCD-only portion.
 * Adapted from NorthernMan54/JC3248W535EN (Apache-2.0, Espressif Systems)
 *
 * Only the LCD display panel portion is included here.
 * Touch is handled via raw I2C in the parent driver.
 */
#pragma once

#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Version */
#define ESP_LCD_AXS15231B_VER_MAJOR (1)
#define ESP_LCD_AXS15231B_VER_MINOR (0)
#define ESP_LCD_AXS15231B_VER_PATCH (0)

/* ── QSPI bus init macro ────────────────────────────────────────────────── */
/* Expands to a spi_bus_config_t initializer suitable for QSPI. */
#define AXS15231B_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz) \
    {                                           \
        /* spi_bus_config_t declares the data0/1 unions first, */ \
        /* followed by sclk_io_num then data2/3, so the */ \
        /* designators must appear in that order. */         \
        .data0_io_num    = (d0),               \
        .data1_io_num    = (d1),               \
        .sclk_io_num     = (sclk),             \
        .data2_io_num    = (d2),               \
        .data3_io_num    = (d3),               \
        .max_transfer_sz = (max_trans_sz),      \
    }

/* ── QSPI panel IO init macro ───────────────────────────────────────────── */
/* 40 MHz / mode-3 / lcd_cmd_bits=32 (opcode+cmd) / quad_mode. */
#define AXS15231B_PANEL_IO_QSPI_CONFIG(cs, cb, cb_ctx)     \
    {                                                        \
        .cs_gpio_num        = (cs),                         \
        .dc_gpio_num        = -1,                           \
        .spi_mode           = 3,                            \
        .pclk_hz            = 60 * 1000 * 1000,            \
        .trans_queue_depth  = 10,                           \
        .on_color_trans_done = (cb),                        \
        .user_ctx           = (cb_ctx),                     \
        .lcd_cmd_bits       = 32,                           \
        .lcd_param_bits     = 8,                            \
        .flags = {                                          \
            .quad_mode          = true,                     \
        },                                                  \
    }

/* ── Init commands ──────────────────────────────────────────────────────── */
typedef struct {
    int           cmd;
    const void   *data;
    size_t        data_bytes;
    unsigned int  delay_ms;
} axs15231b_lcd_init_cmd_t;

/* ── Vendor config ───────────────────────────────────────────────────────  */
typedef struct {
    const axs15231b_lcd_init_cmd_t *init_cmds;
    uint16_t                        init_cmds_size;
    struct {
        unsigned int use_qspi_interface : 1;
    } flags;
} axs15231b_vendor_config_t;

/* ── Panel creation ─────────────────────────────────────────────────────── */
esp_err_t esp_lcd_new_panel_axs15231b(const esp_lcd_panel_io_handle_t io,
                                      const esp_lcd_panel_dev_config_t *panel_dev_config,
                                      esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
