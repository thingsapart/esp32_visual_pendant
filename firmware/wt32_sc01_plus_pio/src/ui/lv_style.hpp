#ifndef UI_LV_STYLE_H
#define UI_LV_STYLE_H

#include <Arduino.h>
#include <lvgl.h>

#include <map>
#include <string>
#include <vector>

int s_color(uint8_t r, uint8_t g, uint8_t b);
lv_color_t t_color(int c);

lv_obj_t* create_container(lv_obj_t* parent);
void style_container_blank(lv_obj_t* obj);
lv_obj_t* style_container(lv_obj_t* container);
lv_obj_t* no_margin_pad_border(lv_obj_t* obj);
lv_obj_t* container_col(lv_obj_t* parent, int pad_col, int pad_row);
lv_obj_t* container_row(lv_obj_t* parent, int pad_col, int pad_row);
void ignore_layout(lv_obj_t* obj);
std::vector<std::string> button_matrix_ver(const std::vector<std::string>& labels);
void flex_col(lv_obj_t* obj, int pad_col, int pad_row, bool wrap);
void flex_row(lv_obj_t* obj, int pad_row, int pad_col, bool wrap);
void style_pad(lv_obj_t* obj, int v);
void style_margin(lv_obj_t* obj, int v);
void non_scrollable(lv_obj_t* obj);
int pl_color(const char* name, const char* palette = nullptr);
void style(lv_obj_t* obj, const std::map<std::string, int> styles, lv_state_t state = LV_STATE_DEFAULT);
//void register_png();
//lv_image_dsc_t* load_png2(const char* path);
lv_image_dsc_t* load_png(const char* path, int w, int h);
lv_image_dsc_t* load_bmp(const char* path, int w, int h);
void dbg_layout(lv_obj_t* obj);
//template <typename T>
//class wrap_data {
//public:
//    wrap_data(lv_obj_t* parent, void* data);
//
//private:
//    void* data;
//};

int st_color(lv_color_t c);
int s_color(uint8_t r, uint8_t g, uint8_t b);
lv_color_t t_color(int c);

int st_quad(uint8_t top, uint8_t right, uint8_t bottom, uint8_t left);
int st_tup(uint8_t top_bot, uint8_t right_left);

#endif