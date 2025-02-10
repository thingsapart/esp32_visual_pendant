#include "ui/lv_style.hpp"
#include <stdio.h>

enum tag_quad_uint7 { q_singular = 0, q_tuple = 1, q_quad = 2 };
union quat_uint7 {
    int full_value;
    struct {
        unsigned int top : 7;
        unsigned int bottom : 7;
        unsigned int right : 7;
        unsigned int left : 7;

        tag_quad_uint7 tag : 2;
    } parts;
};

lv_obj_t* create_container(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    return obj;
}

void style_container_blank(lv_obj_t* obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_margin_all(obj, 0, LV_STATE_DEFAULT);
}

lv_obj_t* style_container(lv_obj_t* container) {
    style(container, {
        {"size_0", LV_PCT(100)},
        {"size_1", LV_SIZE_CONTENT},
        {"padding", 0},
        {"margin", 0},
        {"border_width", 0}
    }, LV_STATE_DEFAULT);
    return container;
}

lv_obj_t* no_margin_pad_border(lv_obj_t* obj) {
    style(obj, {
        {"padding", 0},
        {"margin", 0},
        {"border_width", 0}
    }, LV_STATE_DEFAULT);
    return obj;
}

lv_obj_t* container_col(lv_obj_t* parent, int pad_col, int pad_row) {
    lv_obj_t* container = lv_obj_create(parent);
    flex_col(container, pad_col, pad_row, false);
    style_container(container);
    return container;
}

lv_obj_t* container_row(lv_obj_t* parent, int pad_col, int pad_row) {
    lv_obj_t* container = lv_obj_create(parent);
    flex_row(container, pad_row, pad_col, false);
    style_container(container);
    return container;
}

void ignore_layout(lv_obj_t* obj) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

std::vector<std::string> button_matrix_ver(const std::vector<std::string>& labels) {
    std::vector<std::string> result;
    for (const auto& label : labels) {
        result.push_back(label);
        result.push_back("\n");
    }
    if (!result.empty()) {
        result.pop_back();  // Remove the last newline
    }
    return result;
}

void flex_col(lv_obj_t* obj, int pad_col, int pad_row, bool wrap) {
    lv_obj_center(obj);
    lv_flex_flow_t flow = wrap ? LV_FLEX_FLOW_COLUMN_WRAP : LV_FLEX_FLOW_COLUMN;
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (pad_row != -1) lv_obj_set_style_pad_row(obj, pad_row, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(obj, pad_col, LV_STATE_DEFAULT);
}

void flex_row(lv_obj_t* obj, int pad_row, int pad_col, bool wrap) {
    lv_obj_center(obj);
    lv_flex_flow_t flow = wrap ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW;
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (pad_row != -1) lv_obj_set_style_pad_row(obj, pad_row, LV_STATE_DEFAULT);
    if (pad_col != -1) lv_obj_set_style_pad_column(obj, pad_col, LV_STATE_DEFAULT);
}

void style_pad(lv_obj_t* obj, int v) {
    lv_obj_set_style_pad_top(obj, v, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(obj, v, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(obj, v, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(obj, v, LV_STATE_DEFAULT);
}

void style_margin(lv_obj_t* obj, int v) {
    lv_obj_set_style_margin_top(obj, v, LV_STATE_DEFAULT);
    lv_obj_set_style_margin_left(obj, v, LV_STATE_DEFAULT);
    lv_obj_set_style_margin_right(obj, v, LV_STATE_DEFAULT);
    lv_obj_set_style_margin_bottom(obj, v, LV_STATE_DEFAULT);
}

void non_scrollable(lv_obj_t* obj) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

int pl_color(const char* name, const char* palette) {
    lv_palette_t c;

    if (!palette) c = LV_PALETTE_GREY; // Default
    else if (strcmp(name, "AMBER") == 0) c = LV_PALETTE_AMBER;
    else if (strcmp(name, "BLUE") == 0) c = LV_PALETTE_BLUE;
    else if (strcmp(name, "BLUE_GREY") == 0) c = LV_PALETTE_BLUE_GREY;
    else if (strcmp(name, "BROWN") == 0) c = LV_PALETTE_BROWN;
    else if (strcmp(name, "CYAN") == 0) c = LV_PALETTE_CYAN;
    else if (strcmp(name, "DEEP_ORANGE") == 0) c = LV_PALETTE_DEEP_ORANGE;
    else if (strcmp(name, "DEEP_PURPLE") == 0) c = LV_PALETTE_DEEP_PURPLE;
    else if (strcmp(name, "GREEN") == 0) c = LV_PALETTE_GREEN;
    else if (strcmp(name, "GREY") == 0) c = LV_PALETTE_GREY;
    else if (strcmp(name, "INDIGO") == 0) c = LV_PALETTE_INDIGO;
    else if (strcmp(name, "LIGHT_BLUE") == 0) c = LV_PALETTE_LIGHT_BLUE;
    else if (strcmp(name, "LIGHT_GREEN") == 0) c = LV_PALETTE_LIGHT_GREEN;
    else if (strcmp(name, "LIME") == 0) c = LV_PALETTE_LIME;
    else if (strcmp(name, "ORANGE") == 0) c = LV_PALETTE_ORANGE;
    else if (strcmp(name, "PINK") == 0) c = LV_PALETTE_PINK;
    else if (strcmp(name, "PURPLE") == 0) c = LV_PALETTE_PURPLE;
    else if (strcmp(name, "RED") == 0) c = LV_PALETTE_RED;
    else if (strcmp(name, "TEAL") == 0) c = LV_PALETTE_TEAL;
    else if (strcmp(name, "YELLOW") == 0) c = LV_PALETTE_YELLOW;
    

    if (strcmp(palette, "lighten") == 0) return st_color(lv_palette_lighten(c, 3));
    else if (strcmp(palette, "darken") == 0) return st_color(lv_palette_darken(c, 3));
    else return st_color(lv_palette_main(c));
}

std::vector<int> _style_spread(int v) {
    return {v, v, v, v};
}

std::vector<int> _style_spread(const std::vector<int>& v) {
    if (v.size() == 1) return {v[0], v[0], v[0], v[0]};
    if (v.size() == 2) return {v[0], v[1], v[0], v[1]};
    if (v.size() == 3) return {v[0], v[1], v[2], v[1]};
    return {v[0], v[1], v[2], v[3]};
}

void style(lv_obj_t* obj, const std::map<std::string, int> styles, lv_state_t state) {
    for (const auto& [k, v] : styles) {
        if (k == "margin") {
            std::vector<int> vv = _style_spread(v);
            lv_obj_set_style_margin_top(obj, vv[0], state);
            lv_obj_set_style_margin_right(obj, vv[1], state);
            lv_obj_set_style_margin_bottom(obj, vv[2], state);
            lv_obj_set_style_margin_left(obj, vv[3], state);
        } else if (k == "padding") {
            std::vector<int> vv = _style_spread(v);
            lv_obj_set_style_pad_top(obj, vv[0], state);
            lv_obj_set_style_pad_right(obj, vv[1], state);
            lv_obj_set_style_pad_bottom(obj, vv[2], state);
            lv_obj_set_style_pad_left(obj, vv[3], state);
        } else if (k == "opacity") {
            lv_obj_set_style_opa(obj, v, state);
        } else if (k == "x") {
            lv_obj_set_x(obj, v);
        } else if (k == "y") {
            lv_obj_set_y(obj, v);
        } else if (k == "width") {
            lv_obj_set_width(obj, v);
        } else if (k == "height") {
            lv_obj_set_height(obj, v);
        } else if (k == "max_width") {
            lv_obj_set_style_max_width(obj, v, state);
        } else if (k == "max_height") {
            lv_obj_set_style_max_height(obj, v, state);
        } else if (k == "min_width") {
            lv_obj_set_style_min_width(obj, v, state);
        } else if (k == "min_height") {
            lv_obj_set_style_min_height(obj, v, state);
        } else if (k == "content_width") {
            lv_obj_set_content_width(obj, v);
        } else if (k == "content_height") {
            lv_obj_set_content_height(obj, v);
        } else if (k.rfind("size_", 0) == 0) {  // Starts with "size_"
            int index = std::stoi(k.substr(5));  // Get index after "size_"
            if (index == 0) {
                lv_obj_set_size(obj, v, LV_SIZE_CONTENT);
            } else if (index == 1) {
                //Assuming LV_SIZE_CONTENT already set at index 0
            }
        } else if (k == "pos") {
            // Assuming 'v' is a single integer representing both x and y
            lv_obj_set_pos(obj, v, v);
        } else if (k == "align") {
            // Need more info on how align is intended to work
        } else if (k == "transform_zoom") {
            lv_obj_set_style_transform_zoom(obj, v, state);
        } else if (k == "transform_angle") {
            lv_obj_set_style_transform_angle(obj, v, state);
        } else if (k == "bg_color") {
            lv_obj_set_style_bg_color(obj, t_color(v), state);
        } else if (k == "bg_opa") {
            lv_obj_set_style_bg_opa(obj, v, state);
        } else if (k == "bg_grad_color") {
            lv_obj_set_style_bg_grad_color(obj, t_color(v), state);
        } else if (k == "bg_grad_dir") {
            lv_obj_set_style_bg_grad_dir(obj, (lv_grad_dir_t)v, state);
        } else if (k == "bg_grad") {
            //lv_obj_set_style_bg_grad(obj, v, state);
        } else if (k == "bg_img_src") {
            // Assuming 'v' is a pointer to an lv_image_dsc_t or a symbol
            lv_obj_set_style_bg_img_src(obj, (const void*)v, state);
        } else if (k == "bg_img_opa") {
            lv_obj_set_style_bg_image_opa(obj, v, state);
        } else if (k == "bg_img_tiled") {
            lv_obj_set_style_bg_image_tiled(obj, v, state);
        } else if (k == "radius") {
            lv_obj_set_style_radius(obj, v, state);
        } else if (k == "border_color") {
            lv_obj_set_style_border_color(obj, t_color(v), state);
        } else if (k == "border_width") {
            lv_obj_set_style_border_width(obj, v, state);
        } else if (k == "border_opa") {
            lv_obj_set_style_border_opa(obj, v, state);
        } else if (k == "border_side") {
            lv_obj_set_style_border_side(obj, (lv_border_side_t)v, state);
        } else if (k == "border_post") {
            lv_obj_set_style_border_post(obj, v, state);
        } else if (k == "outline_width") {
            lv_obj_set_style_outline_width(obj, v, state);
        } else if (k == "outline_color") {
            lv_obj_set_style_outline_color(obj, t_color(v), state);
        } else if (k == "outline_opa") {
            lv_obj_set_style_outline_opa(obj, v, state);
        } else if (k == "outline_pad") {
            lv_obj_set_style_outline_pad(obj, v, state);
        } else if (k == "shadow_width") {
            lv_obj_set_style_shadow_width(obj, v, state);
        } else if (k == "shadow_ofs_x") {
            lv_obj_set_style_shadow_ofs_x(obj, v, state);
        } else if (k == "shadow_ofs_y") {
            lv_obj_set_style_shadow_ofs_y(obj, v, state);
        } else if (k == "shadow_spread") {
            lv_obj_set_style_shadow_spread(obj, v, state);
        } else if (k == "shadow_color") {
            lv_obj_set_style_shadow_color(obj, t_color(v), state);
        } else if (k == "shadow_opa") {
            lv_obj_set_style_shadow_opa(obj, v, state);
        } else {
            Serial.print("Unknown style key: ");
            Serial.println(k.c_str());
        }
    }
}

lv_image_dsc_t* load_png(const char* path, int w, int h) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        Serial.print("Could not load image ");
        Serial.println(path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    size_t img_data_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* img_data = (uint8_t*)malloc(img_data_size);
    if (!img_data) {
        fclose(f);
        Serial.println("Memory allocation failed");
        return nullptr;
    }

    fread(img_data, 1, img_data_size, f);
    fclose(f);

    lv_image_dsc_t* img_dsc = (lv_image_dsc_t*)malloc(sizeof(lv_image_dsc_t));
    if (!img_dsc) {
        free(img_data);
        Serial.println("Memory allocation failed");
        return nullptr;
    }

    img_dsc->header.w = w;
    img_dsc->header.h = h;
    img_dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    img_dsc->data_size = img_data_size;
    img_dsc->data = img_data;

    return img_dsc;
}

lv_image_dsc_t* load_bmp(const char* path, int w, int h) {
        FILE* f = fopen(path, "rb");
    if (!f) {
        Serial.print("Could not load image ");
        Serial.println(path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    size_t img_data_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* img_data = (uint8_t*)malloc(img_data_size);
    if (!img_data) {
        fclose(f);
        Serial.println("Memory allocation failed");
        return nullptr;
    }

    fread(img_data, 1, img_data_size, f);
    fclose(f);

    lv_image_dsc_t* img_dsc = (lv_image_dsc_t*)malloc(sizeof(lv_image_dsc_t));
    if (!img_dsc) {
        free(img_data);
        Serial.println("Memory allocation failed");
        return nullptr;
    }

    img_dsc->header.w = w;
    img_dsc->header.h = h;
    img_dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    img_dsc->data_size = img_data_size;
    img_dsc->data = img_data;

    return img_dsc;
}

void dbg_layout(lv_obj_t* obj) {
    style(obj, {{"bg_color", s_color(222, 0, 0)}, {"bg_opa", 255}}, LV_STATE_DEFAULT);
}

int st_color(lv_color_t c) {
    return s_color(c.red, c.blue, c.green);
}

int s_color(uint8_t r, uint8_t g, uint8_t b) {
    return (int) (r << 16 + g << 8 + b);
}

lv_color_t t_color(int c) {
    lv_color_t res;
    res.red = c >> 16 & 0xFF;
    res.green = c >> 8 & 0xFF;
    res.red = c & 0xFF;

    return res;
}

void unpack_st_quad_tup(int v, int& top, int& right, int& bottom, int& left) {
    if (v >> 16 & 0xFFFF == 0xFFFF) {
        top = bottom = (v >> 8) & 0xFF;
        right = left = v & 0xFF;
    } else {
        top = v >> 24 & 0xFF;
        right = v >> 16 & 0xFF;
        bottom = v >> 8 & 0xFF;
        left = v & 0xFF;
    }
}

// Values of 0-127 are acceptable.
int st_quad(uint8_t top, uint8_t right, uint8_t bottom, uint8_t left) {
    assert(top <= 127 && right <= 127 && bottom <= 127 && left <= 127);

    quat_uint7 res = { 
        .parts {
            .top = top,
            .bottom = bottom,
            .right = right,
            .left = left,
            .tag = q_quad       
        }
    };

    return res.full_value;
}

// Values of 0-127 are acceptable.
int st_tup(uint8_t top_bot, uint8_t right_left) {
    assert(top_bot <= 127 && right_left <= 127);

    quat_uint7 res = { 
        .parts {
            .top = top_bot,
            .bottom = top_bot,
            .right = right_left,
            .left = right_left,
            .tag = q_quad       
        }
    };

    return res.full_value;
}
