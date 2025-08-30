/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP LVGL port
 */

#pragma once

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port_disp.h"
#include "esp_lvgl_port_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#if LVGL_VERSION_MAJOR == 8
#include "esp_lvgl_port_compatibility.h"
#endif

/**
 * @brief LVGL Port task event type
 */
typedef enum {
  LVGL_PORT_EVENT_DISPLAY = 0x01,
  LVGL_PORT_EVENT_TOUCH = 0x02,
  LVGL_PORT_EVENT_USER = 0x80,
} lvgl_port_event_type_t;

/**
 * @brief LVGL Port task events
 */
typedef struct {
  lvgl_port_event_type_t type;
  void *param;
} lvgl_port_event_t;

/**
 * @brief Init configuration structure
 */
typedef struct {
  int task_priority;        /*!< LVGL task priority */
  int task_stack;           /*!< LVGL task stack size */
  int task_affinity;        /*!< LVGL task pinned to core (-1 is no affinity) */
  int task_max_sleep_ms;    /*!< Maximum sleep in LVGL task */
  unsigned task_stack_caps; /*!< LVGL task stack memory capabilities (see
                               esp_heap_caps.h) */
  int timer_period_ms;      /*!< LVGL timer tick period in ms */
} lvgl_port_cfg_t;

/**
 * @brief LVGL port configuration structure
 *
 */
#define ESP_LVGL_PORT_INIT_CONFIG()                                \
  {                                                                \
      .task_priority = 4,                                          \
      .task_stack = 7168,                                          \
      .task_affinity = -1,                                         \
      .task_max_sleep_ms = 500,                                    \
      .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT, \
      .timer_period_ms = 5,                                        \
  }

/**
 * @brief Initialize LVGL portation
 *
 * @note This function initialize LVGL and create timer and task for LVGL right
 * working.
 *
 * @return
 *      - ESP_OK                    on success
 *      - ESP_ERR_INVALID_ARG       if some of the create_args are not valid
 *      - ESP_ERR_INVALID_STATE     if esp_timer library is not initialized yet
 *      - ESP_ERR_NO_MEM            if memory allocation fails
 */
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg);

/**
 * @brief Deinitialize LVGL portation
 *
 * @note This function deinitializes LVGL and stops the task if running.
 * Some deinitialization will be done after the task will be stopped.
 *
 * @return
 *      - ESP_OK                    on success
 *      - ESP_ERR_TIMEOUT           when stopping the LVGL task times out
 */
esp_err_t lvgl_port_deinit(void);

/**
 * @brief Take LVGL mutex
 *
 * @param timeout_ms Timeout in [ms]. 0 will block indefinitely.
 * @return
 *      - true  Mutex was taken
 *      - false Mutex was NOT taken
 */
bool lvgl_port_lock(uint32_t timeout_ms);

/**
 * @brief Give LVGL mutex
 *
 */
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
