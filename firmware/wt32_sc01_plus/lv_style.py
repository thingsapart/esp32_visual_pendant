import lvgl as lv

def create_container(parent):
    obj = lv.obj(parent)
    obj.set_style_bg_opa(0, lv.STATE.DEFAULT)
    obj.set_style_border_opa(0, lv.STATE.DEFAULT)

    return obj

def style_container_blank(obj):
    obj.set_style_bg_opa(0, lv.STATE.DEFAULT)
    obj.set_style_border_opa(0, lv.STATE.DEFAULT)
    obj.set_style_pad_all(0, lv.STATE.DEFAULT)
    obj.set_style_margin_all(0, lv.STATE.DEFAULT)

def style_container(container):
    style(container, {
        'size': [lv.pct(100), lv.SIZE_CONTENT],
        'padding': 0,
        'margin': 0,
        'border_width': 0
        })
    return container

def no_margin_pad_border(obj):
    style(obj, {
        'padding': 0,
        'margin': 0,
        'border_width': 0
        })
    return obj

def container_col(parent, pad_col=0, pad_row=0):
    container = lv.obj(parent)
    flex_col(container, pad_row=pad_row, pad_col=pad_col)
    style_container(container)
    return container

def container_row(parent, pad_col=0, pad_row=0):
    container = lv.obj(parent)
    flex_row(container, pad_row=pad_row, pad_col=pad_col)
    style_container(container)
    return container

def ignore_layout(obj):
    obj.add_flag(lv.obj.FLAG.IGNORE_LAYOUT)

def button_matrix_ver(labels):
    return list([j for i in labels for j in [i, '\n']])[:-1]

def flex_col(obj, pad_col=0, pad_row=None):
    obj.center()
    obj.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    obj.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
    if pad_row is not None: obj.set_style_pad_row(pad_row, lv.STATE.DEFAULT)
    obj.set_style_pad_column(pad_col, lv.STATE.DEFAULT)

def flex_row(obj, pad_row=None, pad_col=None):
    obj.center()
    obj.set_flex_flow(lv.FLEX_FLOW.ROW)
    obj.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
    if pad_row is not None: obj.set_style_pad_row(pad_row, lv.STATE.DEFAULT)
    if pad_col is not None: obj.set_style_pad_column(pad_col, lv.STATE.DEFAULT)

def style_pad(obj, v):
    obj.set_style_pad_top(v, lv.STATE.DEFAULT)
    obj.set_style_pad_left(v, lv.STATE.DEFAULT)
    obj.set_style_pad_right(v, lv.STATE.DEFAULT)
    obj.set_style_pad_bottom(v, lv.STATE.DEFAULT)

def style_margin(obj, v):
    obj.set_style_margin_top(v, lv.STATE.DEFAULT)
    obj.set_style_margin_left(v, lv.STATE.DEFAULT)
    obj.set_style_margin_right(v, lv.STATE.DEFAULT)
    obj.set_style_margin_bottom(v, lv.STATE.DEFAULT)

def non_scrollable(obj):
    obj.remove_flag(lv.obj.FLAG.SCROLLABLE)

# 'AMBER', 'BLUE', 'BLUE_GREY', 'BROWN', 'CYAN', 'DEEP_ORANGE', 'DEEP_PURPLE', 'GREEN', 'GREY', 'INDIGO', 'LAST', 'LIGHT_BLUE', 'LIGHT_GREEN', 'LIME', 'NONE', 'ORANGE', 'PINK', 'PURPLE', 'RED', 'TEAL', 'YELLOW'
def color(name, palette = 'main'):
    c = lv.PALETTE.__dict__[name]
    if palette == 'lighten':
        return lv.palette_lighten(c)
    elif palette == 'lighten':
        return lv.palette_lighten(c)
    else:
        return lv.palette_main(c)

def _style_spread(v):
    if isinstance(v, list):
        if len(v) == 1:
            return [v[0], v[0], v[0], v[0]]
        if len(v) == 2:
            return [v[0], v[1], v[0], v[1]]
        if len(v) == 3:
            return [v[0], v[1], [2], v[1]]
        return v[0:4]
    else:
        return [v, v, v, v]

def style(obj, styles, state = lv.STATE.DEFAULT):
    # print('style', styles)
    for k, v in styles.items():
        if isinstance(v, dict):
            for kk, vv in v.items():
                key = k + '_' + kk
                style(obj, { key: vv }, state)
            next

        if k == 'margin':
            vv = _style_spread(v)
            obj.set_style_margin_top(vv[0], state)
            obj.set_style_margin_right(vv[1], state)
            obj.set_style_margin_bottom(vv[2], state)
            obj.set_style_margin_left(vv[3], state)
        elif k == 'padding':
            vv = _style_spread(v)
            obj.set_style_pad_top(vv[0], state)
            obj.set_style_pad_right(vv[1], state)
            obj.set_style_pad_bottom(vv[2], state)
            obj.set_style_pad_left(vv[3], state)
        elif k == 'opacity':
            obj.set_style_opa(v, state)
        elif k == 'x':
            obj.set_x(v, state)
        elif k == 'y':
            obj.set_y(v, state)
        elif k == 'width':
            obj.set_width(v)
        elif k == 'height':
            obj.set_height(v)
        elif k == 'max_width':
            obj.set_max_width(v, state)
        elif k == 'max_height':
            obj.set_max_height(v, state)
        elif k == 'min_width':
            obj.set_min_width(v, state)
        elif k == 'min_height':
            obj.set_min_height(v, state)
        elif k == 'content_width':
            obj.set_content_width(v, state)
        elif k == 'content_height':
            obj.set_content_height(v, state)
        elif k == 'size':
            obj.set_size(v[0], v[1])
        elif k == 'pos':
            obj.set_pos(v[0], v[1], state)
        elif k == 'align':
            obj.set_align(v[0], v[1], v[2], state)
        elif k == 'transform_zoom':
            obj.set_style_transform_zoom(v, state)
        elif k == 'transform_angle':
            obj.set_style_transform_angle(v, state)
        elif k == 'bg_color':
            obj.set_style_bg_color(v, state)
        elif k == 'bg_opa':
            obj.set_style_bg_opa(v, state)
        elif k == 'bg_grad_color':
            obj.set_style_bg_grad_color(v, state)
        elif k == 'bg_grad_dir':
            obj.set_style_bg_grad_dir(v, state)
        elif k == 'bg_grad':
            obj.set_style_bg_grad(v, state)
        elif k == 'bg_img_src':
            obj.set_style_bg_img_src(v, state)
        elif k == 'bg_img_opa':
            obj.set_style_bg_img_opa(v, state)
        elif k == 'bg_img_tiled':
            obj.set_style_bg_img_tiled(v, state)
        elif k == 'radius':
            obj.set_style_radius(v, state)
        elif k == 'border_color':
            obj.set_style_border_color(v, state)
        elif k == 'border_width':
            obj.set_style_border_width(v, state)
        elif k == 'border_opa':
            obj.set_style_border_opa(v, state)
        elif k == 'border_side':
            obj.set_style_border_side(v, state)
        elif k == 'border_post':
            obj.set_style_border_post(v, state)
        elif k == 'outline_width':
            obj.set_style_outline_width(v, state)
        elif k == 'outline_color':
            obj.set_style_outline_color(v, state)
        elif k == 'outline_opa':
            obj.set_style_outline_opa(v, state)
        elif k == 'outline_pad':
            obj.set_style_outline_pad(v, state)
        elif k == 'shadow':
            if v['width']: obj.set_style_shadow_width(v['width'], state)
            if v['x']: obj.set_style_shadow_ofs_x(v['x'], state)
            if v['y']: obj.set_style_shadow_ofs_y(v['y'], state)
            if v['spread']: obj.set_style_shadow_spread(v['spread'], state)
            if v['color']: obj.set_style_shadow_color(v['y'], state)
            if v['opa']: obj.set_style_shadow_opa(v['y'], state)
        elif k == 'shadow_ofs_x':
            obj.set_style_shadow_ofs_x(v, state)
        elif k == 'shadow_ofs_y':
            obj.set_style_shadow_ofs_y(v, state)
        elif k == 'shadow_width':
            obj.set_style_shadow_width(v, state)
        elif k == 'shadow_ofs_x':
            obj.set_style_shadow_ofs_x(v, state)
        elif k == 'shadow_ofs_y':
            obj.set_style_shadow_ofs_y(v, state)
        elif k == 'shadow_spread':
            obj.set_style_shadow_spread(v, state)
        elif k == 'shadow_color':
            obj.set_style_shadow_color(v, state)
        elif k == 'shadow_opa':
            obj.set_style_shadow_opa(v, state)
        else:
            print('Unknown style:', k, v)

# Register PNG image decoder
def register_png():
    from imagetools import get_png_info, open_png

    decoder = lv.img.decoder_create()
    decoder.info_cb = get_png_info
    decoder.open_cb = open_png

# Create an image from the png file
def load_png2(path):
    try:
        with open(path,'rb') as f:
            png_data = f.read()
    except:
        print("Could not load image ", path)
        return None

    return lv.image_dsc_t({
      'data_size': len(png_data),
      'data': png_data
    })

def load_png(path, w, h):
    try:
        with open(path,'rb') as f:
            img_data = f.read()
    except:
        print("Could not load image ", path)
        return None

    return lv.image_dsc_t(
        {
            "header": {"w": w, "h": h, "cf": lv.COLOR_FORMAT.ARGB8888},
            "data_size": len(img_data),
            "data": img_data,
        }
    )

def load_bmp(path, w, h):
    try:
        with open(path,'rb') as f:
            img_data = f.read()
    except:
        print("Could not load image ", path)
        return None

    return lv.image_dsc_t(
        {
            "header": {"w": w, "h": h, "cf": lv.COLOR_FORMAT.ARGB8888},
            "data_size": len(img_data),
            "data": img_data,
        }
    )

def dbg_layout(obj):
    style(obj, { 'bg_color': lv.color_make(222, 0, 0), 'bg_opa':255 })
