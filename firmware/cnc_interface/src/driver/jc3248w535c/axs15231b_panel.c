/*
 * AXS15231B LCD panel driver — LCD-only implementation.
 * Adapted from NorthernMan54/JC3248W535EN (Apache-2.0, Espressif Systems)
 *
 * Init sequence taken from esp_bsp.c in the reference project.
 * Only compiled when JC3248W535C is defined.
 *
 * Bug-fixes applied vs. the original hand-rolled version:
 *   1. draw_bitmap: skip RASET in QSPI mode.
 *      The AXS15231B ignores the RASET command over QSPI and tracks rows
 *      implicitly through its internal write pointer.  Sending RASET
 *      corrupted that state and caused every partial flush to end up at
 *      the wrong Y position.
 *   2. draw_bitmap: use RAMWRC (0x3C) for y_start > 0.
 *      RAMWR (0x2C) resets the write pointer to row 0 every time.  For
 *      partial dirty-rects (y_start > 0) we must use RAMWRC which
 *      continues from where the CASET/implicit RASET left off.
 *      Together these two bugs caused the "brief flash then 98% black"
 *      symptom: only the initial full-screen flush (y=0, RAMWR) rendered
 *      correctly; all subsequent partial redraws overwrote row 0.
 */
#if defined(JC3248W535C) || defined(HW_AXS15231B)

#include <stdlib.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "axs15231b_panel.h"

#define LCD_OPCODE_WRITE_CMD    (0x02ULL)
#define LCD_OPCODE_WRITE_COLOR  (0x32ULL)

/* 0x3C = Memory Write Continue — advances from current write-pointer position
 * without resetting to row 0.  Not defined in esp_lcd_panel_commands.h. */
#ifndef LCD_CMD_RAMWRC
#define LCD_CMD_RAMWRC          (0x3C)
#endif

static const char *TAG = "axs15231b";

/* ── Init command array (from BSP, display-on ending) ─────────────────── */
static const axs15231b_lcd_init_cmd_t vendor_specific_init_default[] = {
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}, 8, 0},
    {0xA0, (uint8_t[]){0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00}, 17, 0},
    {0xA2, (uint8_t[]){0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xf9, 0x10, 0x02, 0xff, 0xff, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A}, 31, 0},
    {0xD0, (uint8_t[]){0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12}, 30, 0},
    {0xA3, (uint8_t[]){0xA0, 0x06, 0xAa, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55}, 22, 0},
    {0xC1, (uint8_t[]){0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0d, 0x00, 0xFF, 0x40}, 30, 0},
    {0xC3, (uint8_t[]){0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01}, 11, 0},
    {0xC4, (uint8_t[]){0x00, 0x24, 0x33, 0x80, 0x00, 0xea, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50}, 29, 0},
    {0xC5, (uint8_t[]){0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00}, 23, 0},
    {0xC6, (uint8_t[]){0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22}, 20, 0},
    {0xC7, (uint8_t[]){0x50, 0x32, 0x28, 0x00, 0xa2, 0x80, 0x8f, 0x00, 0x80, 0xff, 0x07, 0x11, 0x9c, 0x67, 0xff, 0x24, 0x0c, 0x0d, 0x0e, 0x0f}, 20, 0},
    {0xC9, (uint8_t[]){0x33, 0x44, 0x44, 0x01}, 4, 0},
    {0xCF, (uint8_t[]){0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08}, 27, 0},
    {0xD5, (uint8_t[]){0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00}, 30, 0},
    {0xD6, (uint8_t[]){0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00}, 30, 0},
    {0xD7, (uint8_t[]){0x03, 0x01, 0x0b, 0x09, 0x0f, 0x0d, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F}, 19, 0},
    {0xD8, (uint8_t[]){0x02, 0x00, 0x0a, 0x08, 0x0e, 0x0c, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19}, 12, 0},
    {0xD9, (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDD, (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDF, (uint8_t[]){0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}, 8, 0},
    {0xE0, (uint8_t[]){0x3B, 0x28, 0x10, 0x16, 0x0c, 0x06, 0x11, 0x28, 0x5c, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D}, 17, 0},
    {0xE1, (uint8_t[]){0x37, 0x28, 0x10, 0x16, 0x0b, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F}, 17, 0},
    {0xE2, (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE3, (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F}, 17, 0},
    {0xE4, (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE5, (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F}, 17, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30}, 16, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x85}, 4, 0},
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0},
    {0x13, (uint8_t[]){0x00}, 0, 0},          /* Normal Display Mode On  */
    {0x11, (uint8_t[]){0x00}, 0, 120},         /* Sleep Out               */
    {0x2C, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0}, /* RAM Write Start */
};

/* ── Internal panel structure ───────────────────────────────────────────── */
typedef struct {
    esp_lcd_panel_t       base;
    esp_lcd_panel_io_handle_t io;
    int                   reset_gpio_num;
    int                   x_gap;
    int                   y_gap;
    uint8_t               fb_bits_per_pixel;
    uint8_t               madctl_val;
    uint8_t               colmod_val;
    const axs15231b_lcd_init_cmd_t *init_cmds;
    uint16_t              init_cmds_size;
    struct {
        unsigned int use_qspi_interface : 1;
        unsigned int reset_level        : 1;
    } flags;
} axs15231b_panel_t;

/* ── Forward declarations ──────────────────────────────────────────────── */
static esp_err_t panel_axs15231b_del(esp_lcd_panel_t *panel);
static esp_err_t panel_axs15231b_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_axs15231b_init(esp_lcd_panel_t *panel);
static esp_err_t panel_axs15231b_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_axs15231b_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_axs15231b_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_axs15231b_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_axs15231b_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_axs15231b_disp_off(esp_lcd_panel_t *panel, bool off);

/* ── QSPI command helpers ───────────────────────────────────────────────── */
static esp_err_t tx_param(axs15231b_panel_t *p, esp_lcd_panel_io_handle_t io,
                           int lcd_cmd, const void *param, size_t param_size)
{
    if (p->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
    }
    return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(axs15231b_panel_t *p, esp_lcd_panel_io_handle_t io,
                           int lcd_cmd, const void *param, size_t param_size)
{
    if (p->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_COLOR << 24;
    }
    return esp_lcd_panel_io_tx_color(io, lcd_cmd, param, param_size);
}

/* ── Panel creation ─────────────────────────────────────────────────────── */
esp_err_t esp_lcd_new_panel_axs15231b(const esp_lcd_panel_io_handle_t io,
                                      const esp_lcd_panel_dev_config_t *panel_dev_config,
                                      esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    axs15231b_panel_t *axs = NULL;

    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel,
                      ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    axs = calloc(1, sizeof(axs15231b_panel_t));
    ESP_GOTO_ON_FALSE(axs, ESP_ERR_NO_MEM, err, TAG, "no mem for axs15231b panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure RST GPIO failed");
    }

    switch (panel_dev_config->color_space) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        axs->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        axs->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color order");
        break;
    }

    uint8_t fb_bits = 0;
    switch (panel_dev_config->bits_per_pixel) {
    case 16: axs->colmod_val = 0x55; fb_bits = 16; break;
    case 18: axs->colmod_val = 0x66; fb_bits = 24; break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported bpp");
        break;
    }

    axs->io                 = io;
    axs->fb_bits_per_pixel  = fb_bits;
    axs->reset_gpio_num     = panel_dev_config->reset_gpio_num;
    axs->flags.reset_level  = panel_dev_config->flags.reset_active_high;

    if (panel_dev_config->vendor_config) {
        const axs15231b_vendor_config_t *vc = (const axs15231b_vendor_config_t *)panel_dev_config->vendor_config;
        axs->init_cmds               = vc->init_cmds;
        axs->init_cmds_size          = vc->init_cmds_size;
        axs->flags.use_qspi_interface = vc->flags.use_qspi_interface;
    }

    axs->base.del          = panel_axs15231b_del;
    axs->base.reset        = panel_axs15231b_reset;
    axs->base.init         = panel_axs15231b_init;
    axs->base.draw_bitmap  = panel_axs15231b_draw_bitmap;
    axs->base.invert_color = panel_axs15231b_invert_color;
    axs->base.set_gap      = panel_axs15231b_set_gap;
    axs->base.mirror       = panel_axs15231b_mirror;
    axs->base.swap_xy      = panel_axs15231b_swap_xy;
    axs->base.disp_on_off  = panel_axs15231b_disp_off;
    *ret_panel = &(axs->base);

    ESP_LOGI(TAG, "new AXS15231B panel @%p (v%d.%d.%d)", axs,
             ESP_LCD_AXS15231B_VER_MAJOR, ESP_LCD_AXS15231B_VER_MINOR, ESP_LCD_AXS15231B_VER_PATCH);
    return ESP_OK;

err:
    if (axs) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(axs);
    }
    return ret;
}

/* ── Panel operations ───────────────────────────────────────────────────── */
static esp_err_t panel_axs15231b_del(esp_lcd_panel_t *panel)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    if (axs->reset_gpio_num >= 0) gpio_reset_pin(axs->reset_gpio_num);
    free(axs);
    return ESP_OK;
}

static esp_err_t panel_axs15231b_reset(esp_lcd_panel_t *panel)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    esp_lcd_panel_io_handle_t io = axs->io;

    if (axs->reset_gpio_num >= 0) {
        gpio_set_level(axs->reset_gpio_num, !axs->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(axs->reset_gpio_num, axs->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(axs->reset_gpio_num, !axs->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        tx_param(axs, io, LCD_CMD_SWRESET, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

static esp_err_t panel_axs15231b_init(esp_lcd_panel_t *panel)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    esp_lcd_panel_io_handle_t io = axs->io;

    ESP_RETURN_ON_ERROR(tx_param(axs, io, LCD_CMD_SLPOUT, NULL, 0), TAG, "SLPOUT failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(tx_param(axs, io, LCD_CMD_MADCTL, (uint8_t[]){axs->madctl_val}, 1), TAG, "MADCTL failed");
    ESP_RETURN_ON_ERROR(tx_param(axs, io, LCD_CMD_COLMOD, (uint8_t[]){axs->colmod_val}, 1), TAG, "COLMOD failed");

    const axs15231b_lcd_init_cmd_t *init_cmds = axs->init_cmds ? axs->init_cmds : vendor_specific_init_default;
    uint16_t init_cmds_size = axs->init_cmds
                            ? axs->init_cmds_size
                            : (uint16_t)(sizeof(vendor_specific_init_default) / sizeof(axs15231b_lcd_init_cmd_t));

    for (int i = 0; i < init_cmds_size; i++) {
        bool overwrite = false;
        switch (init_cmds[i].cmd) {
        case LCD_CMD_MADCTL: overwrite = true; axs->madctl_val = ((uint8_t *)init_cmds[i].data)[0]; break;
        case LCD_CMD_COLMOD: overwrite = true; axs->colmod_val = ((uint8_t *)init_cmds[i].data)[0]; break;
        default: break;
        }
        if (overwrite) {
            ESP_LOGW(TAG, "Init cmd 0x%02X overrides earlier MADCTL/COLMOD", init_cmds[i].cmd);
        }
        ESP_RETURN_ON_ERROR(tx_param(axs, io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes),
                            TAG, "send init cmd failed");
        if (init_cmds[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
        }
    }
    ESP_LOGI(TAG, "AXS15231B init commands sent");
    return ESP_OK;
}

static esp_err_t panel_axs15231b_draw_bitmap(esp_lcd_panel_t *panel,
                                              int x_start, int y_start,
                                              int x_end, int y_end,
                                              const void *color_data)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end));
    esp_lcd_panel_io_handle_t io = axs->io;

    x_start += axs->x_gap;
    x_end   += axs->x_gap;
    y_start += axs->y_gap;
    y_end   += axs->y_gap;

    /* Column address set — always sent in both SPI and QSPI modes. */
    tx_param(axs, io, LCD_CMD_CASET, (uint8_t[]){
        (x_start >> 8) & 0xFF, x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF, (x_end - 1) & 0xFF,
    }, 4);

    /* FIX 1: Row address set must NOT be sent in QSPI mode.
     * The AXS15231B ignores RASET over QSPI — it tracks the row
     * implicitly.  Sending it anyway corrupts the write-pointer state
     * and causes subsequent partial redraws to land at wrong Y positions.
     * In plain SPI mode RASET is still required. */
    if (!axs->flags.use_qspi_interface) {
        tx_param(axs, io, LCD_CMD_RASET, (uint8_t[]){
            (y_start >> 8) & 0xFF, y_start & 0xFF,
            ((y_end - 1) >> 8) & 0xFF, (y_end - 1) & 0xFF,
        }, 4);
    }

    size_t len = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) * axs->fb_bits_per_pixel / 8;

    /* FIX 2: Use RAMWRC (0x3C) for y_start > 0.
     * RAMWR (0x2C) resets the internal write pointer to row 0 every call.
     * For partial dirty-rects that don't start at y=0 we must use RAMWRC
     * ("Memory Write Continue") so pixels go to the correct Y position.
     * y_start == 0 still uses RAMWR to anchor the pointer at the top. */
    if (y_start == 0) {
        tx_color(axs, io, LCD_CMD_RAMWR, color_data, len);
    } else {
        tx_color(axs, io, LCD_CMD_RAMWRC, color_data, len);
    }

    return ESP_OK;
}

static esp_err_t panel_axs15231b_invert_color(esp_lcd_panel_t *panel, bool invert)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    tx_param(axs, axs->io, invert ? LCD_CMD_INVON : LCD_CMD_INVOFF, NULL, 0);
    return ESP_OK;
}

static esp_err_t panel_axs15231b_mirror(esp_lcd_panel_t *panel, bool mx, bool my)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    if (mx) axs->madctl_val |=  LCD_CMD_MX_BIT;
    else    axs->madctl_val &= ~LCD_CMD_MX_BIT;
    if (my) axs->madctl_val |=  LCD_CMD_MY_BIT;
    else    axs->madctl_val &= ~LCD_CMD_MY_BIT;
    tx_param(axs, axs->io, LCD_CMD_MADCTL, (uint8_t[]){axs->madctl_val}, 1);
    return ESP_OK;
}

static esp_err_t panel_axs15231b_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    if (swap_axes) axs->madctl_val |=  LCD_CMD_MV_BIT;
    else           axs->madctl_val &= ~LCD_CMD_MV_BIT;
    tx_param(axs, axs->io, LCD_CMD_MADCTL, (uint8_t[]){axs->madctl_val}, 1);
    return ESP_OK;
}

static esp_err_t panel_axs15231b_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    axs->x_gap = x_gap;
    axs->y_gap = y_gap;
    return ESP_OK;
}

/* NOTE: ESP-IDF calls this via esp_lcd_panel_disp_on_off(handle, on_off)
 * where on_off=true means "turn display ON". */
static esp_err_t panel_axs15231b_disp_off(esp_lcd_panel_t *panel, bool on_off)
{
    axs15231b_panel_t *axs = __containerof(panel, axs15231b_panel_t, base);
    tx_param(axs, axs->io, on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
    return ESP_OK;
}

#endif /* JC3248W535C */
