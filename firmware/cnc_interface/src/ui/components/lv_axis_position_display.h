// lv_axis_position_display.h
#ifndef __LV_AXIS_POSITION_DISPLAY_H__
#define __LV_AXIS_POSITION_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "../../lvgl/src/core/lv_obj.h"
#include "../../lvgl/src/core/lv_obj_private.h"
#include "lvgl.h"

#define LV_AXIS_DEFAULT_POS_PRECISION \
  2  // Default number of decimal places for positions

/** Label Parts */
enum {
  LV_PART_POSITION = LV_PART_CUSTOM_FIRST, /**< Style the WCS/Position label */
  LV_PART_AXIS_LABEL,                      /**< Style the axis label */
  LV_PART_TO_GO, /**< Style the Distance-to-Go (delta) label */
  LV_PART_ABS,   /**< Style the Absolute Position label */
  _LV_AXIS_POSITION_DISPLAY_PART_LAST /**< Marker for the last custom part */
};

typedef uint8_t lv_axis_position_display_part_t;

typedef struct {
  lv_obj_t obj;
  lv_obj_t *axis_label;
  lv_obj_t *pos_label;
  lv_obj_t *dpos_label;
  lv_obj_t *apos_label;
  lv_obj_t *adpos_cont;  // Container for dpos and apos
  bool selected;
} lv_axis_position_display_t;

extern const lv_obj_class_t lv_axis_position_display_class;

/** Interface */

lv_obj_t *lv_axis_position_display_create(lv_obj_t *parent,
                                          const char *axis_name);
void lv_axis_position_display_set_abs_pos(lv_obj_t *obj, float pos);
void lv_axis_position_display_set_wcs_pos(lv_obj_t *obj, float pos,
                                          int wcs_index);
void lv_axis_position_display_set_dist_to_move(lv_obj_t *obj, float delta);
bool lv_axis_position_display_is_selected(lv_obj_t *obj);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*__LV_AXIS_POSITION_DISPLAY_H__*/