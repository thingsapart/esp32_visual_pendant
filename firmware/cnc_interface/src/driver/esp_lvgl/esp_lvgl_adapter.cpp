#include "esp_lvgl_adapter.h"

#include <string.h>

#include "lvgl.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "ui/touch_calib/touch_calib.h"

#if (CONFIG_IDF_TARGET_ESP32P4 && \
     ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0))
#include "esp_lcd_mipi_dsi.h"
#endif

// This is required to get the PPA wait function prototype
#if LV_USE_GPU_ESP32_P4_PPA
/* Declare the PPA wait function. Prefer an explicit prototype instead of
 * relying on a specific include path to avoid include path issues during
 * cross-build. */
#ifdef __cplusplus
extern "C" void lv_draw_ppa_wait_for_finish(void);
#else
extern void lv_draw_ppa_wait_for_finish(void);
#endif

/* Provide a weak fallback implementation that calls the generic wait
 * function when LVGL was not built with PPA support. If LVGL provides a
 * strong definition, the linker will prefer it and this weak symbol will
 * be ignored. */
#if defined(__GNUC__)
#ifdef __cplusplus
extern "C" void lv_draw_ppa_wait_for_finish(void) __attribute__((weak));
extern "C" void lv_draw_ppa_wait_for_finish(void) { lv_draw_wait_for_finish(); }
#else
void lv_draw_ppa_wait_for_finish(void) __attribute__((weak));
void lv_draw_ppa_wait_for_finish(void) { lv_draw_wait_for_finish(); }
#endif
#endif
#endif

#ifndef CONFIG_LV_DRAW_BUF_ALIGN
#define CONFIG_LV_DRAW_BUF_ALIGN 1
#endif

static const char *TAG = "esp_lvgl_adapter";

/*******************************************************************************
 * Types definitions
 *******************************************************************************/

typedef enum {
  LVGL_PORT_DISP_TYPE_OTHER,
  LVGL_PORT_DISP_TYPE_DSI,
  LVGL_PORT_DISP_TYPE_RGB,
} lvgl_port_disp_type_t;

typedef struct {
  lvgl_port_disp_type_t disp_type;
  esp_lcd_panel_handle_t panel_handle;
  lv_color_t *draw_buffs[2];
  lv_display_t *disp_drv;
  SemaphoreHandle_t trans_sem;
  struct {
    unsigned int direct_mode : 1;
    unsigned int full_refresh : 1;
  } flags;
} lvgl_port_display_ctx_t;

typedef struct {
  esp_lcd_touch_handle_t handle;
  lv_indev_t *indev;
  struct {
    float x;
    float y;
  } scale;
} lvgl_port_touch_ctx_t;

/*******************************************************************************
 * Function prototypes
 *******************************************************************************/

static void lvgl_port_flush_callback(lv_display_t *drv, const lv_area_t *area,
                                     uint8_t *color_map);
static void lvgl_port_touchpad_read(lv_indev_t *indev_drv,
                                    lv_indev_data_t *data);

#if (CONFIG_IDF_TARGET_ESP32P4 && \
     ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0))
static bool lvgl_port_flush_dpi_vsync_ready_callback(
    esp_lcd_panel_handle_t panel_io, esp_lcd_dpi_panel_event_data_t *edata,
    void *user_ctx);
#endif

/*******************************************************************************
 * Public API functions
 *******************************************************************************/

esp_err_t esp_lvgl_adapter_init_display(
    lv_display_t *disp, const lvgl_port_display_cfg_t *disp_cfg,
    const lvgl_port_display_dsi_cfg_t *dsi_cfg) {
  esp_err_t ret = ESP_OK;
  lvgl_port_display_ctx_t *disp_ctx = NULL;
  lv_color_t *buf1 = NULL;
  lv_color_t *buf2 = NULL;
  uint32_t buffer_size_bytes = 0;
  SemaphoreHandle_t trans_sem = NULL;

  ESP_GOTO_ON_FALSE(disp && disp_cfg && dsi_cfg, ESP_ERR_INVALID_ARG, err, TAG,
                    "Invalid arguments");
  ESP_GOTO_ON_FALSE(disp_cfg->panel_handle, ESP_ERR_INVALID_ARG, err, TAG,
                    "Panel handle cannot be NULL");

  disp_ctx = (lvgl_port_display_ctx_t *)malloc(sizeof(lvgl_port_display_ctx_t));
  ESP_GOTO_ON_FALSE(disp_ctx, ESP_ERR_NO_MEM, err, TAG,
                    "Failed to allocate display context");
  memset(disp_ctx, 0, sizeof(lvgl_port_display_ctx_t));

  disp_ctx->panel_handle = disp_cfg->panel_handle;
  disp_ctx->disp_drv = disp;
  disp_ctx->disp_type =
      LVGL_PORT_DISP_TYPE_DSI;  // Hardcoded for JC1060P470 for now

  if (dsi_cfg->flags.avoid_tearing) {
#if (CONFIG_IDF_TARGET_ESP32P4 && \
     ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0))
    buffer_size_bytes =
        disp_cfg->hres * disp_cfg->vres * ((LV_COLOR_DEPTH) / 8);
    size_t num_buf = disp_cfg->double_buffer ? 2 : 1;
    ESP_GOTO_ON_ERROR(
        esp_lcd_dpi_panel_get_frame_buffer(disp_cfg->panel_handle, num_buf,
                                           (void **)&buf1, (void **)&buf2),
        err, TAG, "Get DSI buffers failed");
    trans_sem = xSemaphoreCreateCounting(1, 1);
    ESP_GOTO_ON_FALSE(trans_sem, ESP_ERR_NO_MEM, err, TAG,
                      "Failed to create transport semaphore");
    disp_ctx->trans_sem = trans_sem;
#else
    ESP_GOTO_ON_FALSE(
        false, ESP_ERR_NOT_SUPPORTED, err, TAG,
        "MIPI-DSI avoid_tearing is only supported on ESP32-P4 with IDF >= 5.3");
#endif
  } else {
    ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG,
                      "Only avoid_tearing mode is supported by this adapter");
  }

  // Configure the existing display object
  lv_display_set_driver_data(disp, disp_ctx);
  lv_display_set_flush_cb(disp, lvgl_port_flush_callback);

  if (disp_cfg->flags.direct_mode) {
    disp_ctx->flags.direct_mode = 1;
    lv_display_set_buffers(disp, buf1, buf2, buffer_size_bytes,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
  } else if (disp_cfg->flags.full_refresh) {
    disp_ctx->flags.full_refresh = 1;
    lv_display_set_buffers(disp, buf1, buf2, buffer_size_bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
  } else {
    lv_display_set_buffers(disp, buf1, buf2, buffer_size_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
  }

#if (CONFIG_IDF_TARGET_ESP32P4 && \
     ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0))
  if (dsi_cfg->flags.avoid_tearing) {
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_refresh_done = lvgl_port_flush_dpi_vsync_ready_callback,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(
                          disp_ctx->panel_handle, &cbs, disp),
                      err, TAG, "Failed to register vsync callback");
  }
#endif

  return ESP_OK;

err:
  if (disp_ctx) free(disp_ctx);
  if (trans_sem) vSemaphoreDelete(trans_sem);
  return ret;
}

esp_err_t esp_lvgl_adapter_init_touch(lv_indev_t *indev,
                                      const lvgl_port_touch_cfg_t *touch_cfg) {
  ESP_RETURN_ON_FALSE(indev && touch_cfg, ESP_ERR_INVALID_ARG, TAG,
                      "Invalid arguments");
  ESP_RETURN_ON_FALSE(touch_cfg->handle, ESP_ERR_INVALID_ARG, TAG,
                      "Touch handle cannot be NULL");

  lvgl_port_touch_ctx_t *touch_ctx =
      (lvgl_port_touch_ctx_t *)malloc(sizeof(lvgl_port_touch_ctx_t));
  ESP_RETURN_ON_FALSE(touch_ctx, ESP_ERR_NO_MEM, TAG,
                      "Failed to allocate touch context");

  touch_ctx->handle = touch_cfg->handle;
  touch_ctx->scale.x = (touch_cfg->scale.x ? touch_cfg->scale.x : 1);
  touch_ctx->scale.y = (touch_cfg->scale.y ? touch_cfg->scale.y : 1);
  touch_ctx->indev = indev;

  // Configure the existing indev object
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvgl_port_touchpad_read);
  lv_indev_set_driver_data(indev, touch_ctx);
  lv_indev_set_display(indev, touch_cfg->disp);

  return ESP_OK;
}

/*******************************************************************************
 * Private functions
 *******************************************************************************/

static void lvgl_port_flush_callback(lv_display_t *drv, const lv_area_t *area,
                                     uint8_t *color_map) {
  lvgl_port_display_ctx_t *disp_ctx =
      (lvgl_port_display_ctx_t *)lv_display_get_driver_data(drv);
  assert(disp_ctx != NULL);

  if (disp_ctx->flags.direct_mode || disp_ctx->flags.full_refresh) {
    if (lv_disp_flush_is_last(drv)) {
  #if LV_USE_GPU_ESP32_P4_PPA
    /* Wait for PPA to finish rendering before telling the display driver
     * to swap buffers. Use the PPA-specific wait where available. */
    lv_draw_ppa_wait_for_finish();
  #else
    lv_draw_wait_for_finish();
  #endif
      // Before starting a new transfer, wait for the previous one to complete.
      if (disp_ctx->trans_sem) {
        xSemaphoreTake(disp_ctx->trans_sem, portMAX_DELAY);
      }
      esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, 0, 0,
                                lv_disp_get_hor_res(drv),
                                lv_disp_get_ver_res(drv), color_map);
    }
  } else {
    // This path is for non-direct-mode, partial refresh
#if LV_USE_GPU_ESP32_P4_PPA
  lv_draw_ppa_wait_for_finish();
#else
  lv_draw_wait_for_finish();
#endif
    // In partial mode, we assume the transfer is synchronous or handled by
    // lv_disp_flush_ready in the vsync callback. Waiting for a semaphore here
    // could cause deadlocks if not carefully managed with partial updates.
    esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_map);
  }
}

#if (CONFIG_IDF_TARGET_ESP32P4 && \
     ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0))
static bool lvgl_port_flush_dpi_vsync_ready_callback(
    esp_lcd_panel_handle_t panel_io, esp_lcd_dpi_panel_event_data_t *edata,
    void *user_ctx) {
  BaseType_t need_yield = pdFALSE;
  lv_display_t *disp_drv = (lv_display_t *)user_ctx;
  lvgl_port_display_ctx_t *disp_ctx =
      (lvgl_port_display_ctx_t *)lv_display_get_driver_data(disp_drv);

  // This is called from an ISR when the bitmap has been sent to the display.
  // We can now call lv_disp_flush_ready() and signal the main task.
  lv_disp_flush_ready(disp_drv);

  if (disp_ctx->trans_sem) {
    xSemaphoreGiveFromISR(disp_ctx->trans_sem, &need_yield);
  }

  return (need_yield == pdTRUE);
}
#endif

static void lvgl_port_touchpad_read(lv_indev_t *indev_drv,
                                    lv_indev_data_t *data) {
  lvgl_port_touch_ctx_t *touch_ctx =
      (lvgl_port_touch_ctx_t *)lv_indev_get_driver_data(indev_drv);
  assert(touch_ctx && touch_ctx->handle);

  uint16_t touchpad_x[1] = {0};
  uint16_t touchpad_y[1] = {0};
  uint8_t touchpad_cnt = 0;

  esp_lcd_touch_read_data(touch_ctx->handle);
  bool touchpad_pressed = esp_lcd_touch_get_coordinates(
      touch_ctx->handle, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

  if (touchpad_pressed && touchpad_cnt > 0) {
    /* Apply per-driver scale first */
    float fx = touch_ctx->scale.x * (float)touchpad_x[0];
    float fy = touch_ctx->scale.y * (float)touchpad_y[0];
    /* Apply global calibration homography (no-op if none saved) */
    touch_calib_apply_inplace(&fx, &fy);
    data->point.x = (lv_coord_t)fx;
    data->point.y = (lv_coord_t)fy;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
