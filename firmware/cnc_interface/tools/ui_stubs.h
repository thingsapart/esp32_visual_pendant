#ifndef UI_STUBS_H
#define UI_STUBS_H

#define LV_PROBING_WIZARD_H #ifdef __cplusplus
#define LV_CAM_STREAM_H #include "lvgl.h"
#define LV_CAM_POSITIONING_H #include "lvgl.h"

typedef enum {
  LV_PROBING_WIZARD_MODE_RECTANGLE = 0,
  LV_PROBING_WIZARD_MODE_CIRCLE = 1,
  LV_PROBING_WIZARD_MODE_CORNER = 2,
} lv_probing_wizard_mode_t;

typedef enum {
  LV_PROBING_CORNER_NONE = 0,
  LV_PROBING_CORNER_FRONT_LEFT = 1,
  LV_PROBING_CORNER_FRONT_RIGHT = 2,
  LV_PROBING_CORNER_BACK_LEFT = 3,
  LV_PROBING_CORNER_BACK_RIGHT = 4,
} lv_probing_wizard_corner_t;

typedef enum {
  HIGHLIGHT_NONE = 0,
  HIGHLIGHT_CORNER_BL = 1,
  HIGHLIGHT_CORNER_BR = 2,
  HIGHLIGHT_CORNER_FL = 4,
  HIGHLIGHT_CORNER_FR = 8,
  HIGHLIGHT_PROBE_POINT_0 = 16,
  HIGHLIGHT_PROBE_POINT_1 = 32,
  HIGHLIGHT_PROBE_POINT_2 = 64,
  HIGHLIGHT_PROBE_POINT_3 = 128,
  HIGHLIGHT_OUTLINE = 256,
  HIGHLIGHT_CENTER = 512,
  HIGHLIGHT_Z_PROBE = 1024,
} highlight_part_t;

typedef enum {
  ACTION_AWAIT_START = 0,
  ACTION_JOG_AND_CONFIRM = 1,
  ACTION_SELECT_CORNER = 2,
  ACTION_PROBE_POINT = 3,
  ACTION_PROBE_Z_TOP = 4,
  ACTION_MESSAGE = 5,
  ACTION_COMPLETE = 6,
} lv_probing_action_type_t;

typedef enum {
  LV_CAM_STREAM_FIT_CONTAIN = 0,
  LV_CAM_STREAM_FIT_COVER = 1,
  LV_CAM_STREAM_FIT_FILL = 2,
  LV_CAM_STREAM_FIT_NONE = 3,
} lv_cam_stream_fit_t;

typedef enum {
  LV_CAM_POS_MODE_NONE = 0,
  LV_CAM_POS_MODE_POINT = 1,
  LV_CAM_POS_MODE_RECTANGLE = 2,
  LV_CAM_POS_MODE_CIRCLE = 3,
} lv_cam_pos_mode_t;

static inline typedef lv_probing_wizard_point_float_t(*get_current_jogged_position_cb_t)( arg0) {
  return (typedef)0;
}

static inline typedef void(*set_wcs_origin_cb_t)(lv_obj_t * arg0, uint8_t arg1, float arg2, float arg3, float arg4, bool arg5) {
  return (typedef)0;
}

static inline void lv_probing_wizard_set_z_top_deferred(lv_obj_t * arg0, float arg1) {
  (void)0;
}

static inline void lv_probing_wizard_report_final_result_deferred(lv_obj_t * arg0, float arg1, float arg2) {
  (void)0;
}

static inline void lv_probing_wizard_report_full_result_deferred(lv_obj_t * arg0, float arg1, float arg2, const lv_probing_wizard_details_t * arg3) {
  (void)0;
}

static inline void lv_probing_wizard_advance_step_deferred(lv_obj_t * arg0) {
  (void)0;
}

static inline void lv_probing_wizard_set_active_step_deferred(lv_obj_t * arg0, int8_t arg1) {
  (void)0;
}

static inline lv_obj_t * lv_probing_wizard_create(lv_obj_t * arg0) {
  return (lv_obj_t *)0;
}

static inline void lv_probing_wizard_set_mode(lv_obj_t * arg0, lv_probing_wizard_mode_t arg1, bool arg2) {
  (void)0;
}

static inline void lv_probing_wizard_set_corner_type(lv_obj_t * arg0, lv_probing_wizard_corner_t arg1) {
  (void)0;
}

static inline void lv_probing_wizard_set_active_step(lv_obj_t * arg0, int8_t arg1, bool arg2) {
  (void)0;
}

static inline void lv_probing_wizard_probe_intalled(lv_obj_t * arg0) {
  (void)0;
}

static inline void lv_probing_wizard_register_callbacks(lv_obj_t * arg0, get_current_jogged_position_cb_t arg1, execute_probe_cb_t arg2, set_wcs_origin_cb_t arg3, install_probe_tool_cb_t arg4, cancel_probe_cb_t arg5) {
  (void)0;
}

static inline void lv_probing_wizard_set_connected(lv_obj_t * arg0, bool arg1) {
  (void)0;
}

static inline void lv_probing_wizard_report_probe_result(lv_obj_t * arg0, uint8_t arg1, float arg2, float arg3) {
  (void)0;
}

static inline void lv_probing_wizard_report_final_result(lv_obj_t * arg0, float arg1, float arg2) {
  (void)0;
}

static inline void lv_probing_wizard_set_z_top(lv_obj_t * arg0, float arg1) {
  (void)0;
}

static inline void lv_probing_wizard_report_details(lv_obj_t * arg0, const lv_probing_wizard_details_t * arg1) {
  (void)0;
}

static inline void lv_probing_wizard_advance_step(lv_obj_t * arg0) {
  (void)0;
}

static inline const probe_point_t * lv_probing_wizard_get_setup_points(lv_obj_t * arg0) {
  return (const probe_point_t *)0;
}

static inline float lv_probing_wizard_get_z_top(lv_obj_t * arg0) {
  return (float)0;
}

static inline lv_probing_wizard_point_float_t lv_probing_wizard_get_result(lv_obj_t * arg0) {
  return (lv_probing_wizard_point_float_t)0;
}

static inline lv_probing_wizard_mode_t lv_probing_wizard_get_mode(lv_obj_t * arg0) {
  return (lv_probing_wizard_mode_t)0;
}

static inline bool lv_probing_wizard_get_is_inside(lv_obj_t * arg0) {
  return (bool)0;
}

static inline lv_probing_wizard_corner_t lv_probing_wizard_get_corner_type(lv_obj_t * arg0) {
  return (lv_probing_wizard_corner_t)0;
}

static inline void lv_cam_stream_set_receiver(lv_obj_t * arg0, cam_receiver_t * arg1) {
  (void)0;
}

static inline void lv_cam_stream_set_fit(lv_obj_t * arg0, lv_cam_stream_fit_t arg1) {
  (void)0;
}

static inline lv_cam_stream_fit_t lv_cam_stream_get_fit(lv_obj_t * arg0) {
  return (lv_cam_stream_fit_t)0;
}

static inline void lv_cam_stream_set_show_info(lv_obj_t * arg0, bool arg1) {
  (void)0;
}

static inline void lv_cam_stream_get_image_size(lv_obj_t * arg0, uint16_t * arg1, uint16_t * arg2) {
  (void)0;
}

static inline void lv_cam_stream_refresh(lv_obj_t * arg0) {
  (void)0;
}

static inline void lv_cam_positioning_set_receiver(lv_obj_t * arg0, cam_receiver_t * arg1) {
  (void)0;
}

static inline void lv_cam_positioning_set_mode(lv_obj_t * arg0, lv_cam_pos_mode_t arg1) {
  (void)0;
}

static inline lv_cam_pos_mode_t lv_cam_positioning_get_mode(lv_obj_t * arg0) {
  return (lv_cam_pos_mode_t)0;
}

static inline void lv_cam_positioning_set_point_cb(lv_obj_t * arg0, lv_cam_pos_point_cb_t arg1, void * arg2) {
  (void)0;
}

static inline void lv_cam_positioning_set_rect_cb(lv_obj_t * arg0, lv_cam_pos_rect_cb_t arg1, void * arg2) {
  (void)0;
}

static inline void lv_cam_positioning_set_circle_cb(lv_obj_t * arg0, lv_cam_pos_circle_cb_t arg1, void * arg2) {
  (void)0;
}

static inline void lv_cam_positioning_clear_shapes(lv_obj_t * arg0) {
  (void)0;
}

static inline void lv_cam_positioning_set_point(lv_obj_t * arg0, const lv_cam_pos_point_t * arg1) {
  (void)0;
}

static inline bool lv_cam_positioning_pixel_to_physical(lv_obj_t * arg0, int16_t arg1, int16_t arg2, float * arg3, float * arg4) {
  return (bool)0;
}

static inline bool lv_cam_positioning_has_grid(lv_obj_t * arg0) {
  return (bool)0;
}

#endif /* UI_STUBS_H */