/*******************************************************************************
 * Size: 14 px
 * Bpp: 1
 * Opts: --bpp 1 --size 14 --no-compress --stride 1 --align 1 --font kodemono-medium.ttf --range 32-127 --format lvgl -o font_kode_14.c
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif



#ifndef FONT_KODE_14
#define FONT_KODE_14 1
#endif

#if FONT_KODE_14

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xfe, 0x40,

    /* U+0022 "\"" */
    0x99, 0x99,

    /* U+0023 "#" */
    0x12, 0x32, 0x7f, 0x24, 0x24, 0x24, 0xfe, 0x6c,
    0x48, 0x48,

    /* U+0024 "$" */
    0x10, 0x23, 0xf4, 0x8, 0x10, 0x1f, 0x1, 0x2,
    0x6, 0xf, 0xf1, 0x2, 0x0,

    /* U+0025 "%" */
    0x61, 0xb1, 0x81, 0x80, 0x80, 0xc0, 0xc0, 0x40,
    0x40, 0x63, 0x61, 0x80,

    /* U+0026 "&" */
    0x3c, 0x44, 0x68, 0x38, 0x30, 0x7b, 0xcf, 0x86,
    0x86, 0xfa,

    /* U+0027 "'" */
    0xf0,

    /* U+0028 "(" */
    0x13, 0x6c, 0x88, 0x88, 0x88, 0x8c, 0x63, 0x10,

    /* U+0029 ")" */
    0x8c, 0x63, 0x11, 0x11, 0x11, 0x13, 0x6c, 0x80,

    /* U+002A "*" */
    0x25, 0x7e, 0xe5, 0x28,

    /* U+002B "+" */
    0x20, 0x82, 0x3f, 0x20, 0x80,

    /* U+002C "," */
    0xf5, 0x40,

    /* U+002D "-" */
    0xf8,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0x0, 0x30, 0x82, 0x8, 0x41, 0x4, 0x30, 0x82,
    0x18, 0x41, 0x4, 0x0,

    /* U+0030 "0" */
    0xfa, 0x18, 0x63, 0x96, 0xde, 0x71, 0x85, 0xf0,

    /* U+0031 "1" */
    0x31, 0xcd, 0x4, 0x10, 0x41, 0x4, 0x13, 0xf0,

    /* U+0032 "2" */
    0xfd, 0x8, 0x18, 0x61, 0x86, 0x8, 0x20, 0x81,
    0xfc,

    /* U+0033 "3" */
    0xfc, 0x31, 0x8c, 0x38, 0x30, 0x41, 0x7, 0xf0,

    /* U+0034 "4" */
    0xc, 0x38, 0x92, 0x2c, 0x4f, 0xc1, 0x2, 0x4,
    0x8,

    /* U+0035 "5" */
    0xfe, 0x8, 0x3e, 0xc, 0x10, 0x41, 0x7, 0xf0,

    /* U+0036 "6" */
    0xfa, 0x8, 0x20, 0xff, 0x18, 0x61, 0x85, 0xe0,

    /* U+0037 "7" */
    0xfc, 0x10, 0x84, 0x21, 0x4, 0x10, 0x41, 0x0,

    /* U+0038 "8" */
    0x7d, 0x18, 0x61, 0x79, 0x28, 0x61, 0x85, 0xe0,

    /* U+0039 "9" */
    0xfa, 0x38, 0x71, 0x7c, 0x10, 0x41, 0xf, 0xe0,

    /* U+003A ":" */
    0xf0, 0x3c,

    /* U+003B ";" */
    0xfc, 0x0, 0x36, 0x48, 0x0,

    /* U+003C "<" */
    0x0, 0xcc, 0xcc, 0x61, 0x86, 0x18, 0x0,

    /* U+003D "=" */
    0xfc, 0x0, 0x3f,

    /* U+003E ">" */
    0x6, 0x18, 0x61, 0x8c, 0xcc, 0xc0, 0x0,

    /* U+003F "?" */
    0x7d, 0x8a, 0x8, 0x20, 0x82, 0x4, 0x0, 0x0,
    0x20,

    /* U+0040 "@" */
    0x3c, 0x42, 0x81, 0x9d, 0xa5, 0xa5, 0xa5, 0x9f,
    0x80, 0x40, 0x3c,

    /* U+0041 "A" */
    0x3d, 0x18, 0x61, 0xfe, 0x18, 0x61, 0x86, 0x10,

    /* U+0042 "B" */
    0xfe, 0x18, 0xe6, 0xfa, 0x18, 0x61, 0x87, 0xf0,

    /* U+0043 "C" */
    0xfa, 0x18, 0x20, 0x82, 0x8, 0x20, 0x85, 0xe0,

    /* U+0044 "D" */
    0xfa, 0x28, 0x61, 0x86, 0x18, 0x61, 0x87, 0xf0,

    /* U+0045 "E" */
    0xff, 0x2, 0x4, 0xf, 0x90, 0x20, 0x40, 0x80,
    0xfc,

    /* U+0046 "F" */
    0x7e, 0x8, 0x20, 0xf2, 0x8, 0x20, 0x82, 0x0,

    /* U+0047 "G" */
    0xfa, 0x18, 0x20, 0x9e, 0x18, 0x61, 0x85, 0xf0,

    /* U+0048 "H" */
    0x86, 0x18, 0x61, 0xfe, 0x18, 0x61, 0x86, 0x10,

    /* U+0049 "I" */
    0xfc, 0x41, 0x4, 0x10, 0x41, 0x4, 0x13, 0xf0,

    /* U+004A "J" */
    0xf8, 0x42, 0x10, 0x84, 0x21, 0x8, 0x98,

    /* U+004B "K" */
    0x87, 0x1a, 0x65, 0x8f, 0x1a, 0x26, 0x46, 0x85,
    0xc,

    /* U+004C "L" */
    0x82, 0x8, 0x20, 0x82, 0x8, 0x20, 0x85, 0xf0,

    /* U+004D "M" */
    0x87, 0x3f, 0x6d, 0x86, 0x18, 0x61, 0x86, 0x10,

    /* U+004E "N" */
    0xc6, 0x9b, 0x67, 0x8e, 0x38, 0x61, 0x86, 0x10,

    /* U+004F "O" */
    0xfa, 0x18, 0x61, 0x86, 0x18, 0x61, 0x85, 0xf0,

    /* U+0050 "P" */
    0xfa, 0x18, 0x61, 0xfe, 0x8, 0x20, 0x82, 0x0,

    /* U+0051 "Q" */
    0xfa, 0x18, 0x61, 0x86, 0x18, 0x61, 0x89, 0xf0,
    0x0,

    /* U+0052 "R" */
    0xfa, 0x38, 0x61, 0xfa, 0x28, 0x61, 0x86, 0x10,

    /* U+0053 "S" */
    0xfa, 0x8, 0x20, 0x78, 0x10, 0x41, 0x87, 0xf0,

    /* U+0054 "T" */
    0xfe, 0x20, 0x40, 0x81, 0x2, 0x4, 0x8, 0x10,
    0x20,

    /* U+0055 "U" */
    0x86, 0x18, 0x61, 0x86, 0x18, 0x61, 0x85, 0xf0,

    /* U+0056 "V" */
    0x86, 0x18, 0x61, 0x86, 0x18, 0x72, 0x50, 0xc0,

    /* U+0057 "W" */
    0x83, 0x6, 0xc, 0x19, 0x32, 0x64, 0xc9, 0x9a,
    0xd8,

    /* U+0058 "X" */
    0x86, 0x18, 0x62, 0x79, 0x38, 0x61, 0x86, 0x10,

    /* U+0059 "Y" */
    0x83, 0x5, 0x19, 0x63, 0x82, 0x4, 0x8, 0x10,
    0x20,

    /* U+005A "Z" */
    0xfc, 0x10, 0x42, 0x38, 0x86, 0x30, 0x83, 0xf0,

    /* U+005B "[" */
    0xf8, 0x88, 0x88, 0x88, 0x88, 0x88, 0x8f,

    /* U+005C "\\" */
    0x41, 0x4, 0x18, 0x20, 0x82, 0x4, 0x10, 0x40,
    0x82, 0x8, 0x30, 0x40,

    /* U+005D "]" */
    0xf1, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1f,

    /* U+005E "^" */
    0x30, 0x71, 0xa2, 0x6c, 0x50, 0x80,

    /* U+005F "_" */
    0xfe,

    /* U+0060 "`" */
    0x98, 0x80,

    /* U+0061 "a" */
    0x7c, 0x10, 0x4f, 0x47, 0x1c, 0x5f,

    /* U+0062 "b" */
    0x82, 0x8, 0x3e, 0x86, 0x18, 0x61, 0x86, 0x1f,
    0xc0,

    /* U+0063 "c" */
    0xfa, 0x18, 0x20, 0x82, 0x8, 0x5e,

    /* U+0064 "d" */
    0x4, 0x10, 0x7f, 0x86, 0x18, 0x61, 0x86, 0x17,
    0xc0,

    /* U+0065 "e" */
    0xfa, 0x38, 0xbc, 0x82, 0x8, 0x5e,

    /* U+0066 "f" */
    0x3d, 0x14, 0x10, 0xf9, 0x4, 0x10, 0x41, 0x0,

    /* U+0067 "g" */
    0x7e, 0x18, 0x61, 0xfa, 0xf, 0xe1, 0x86, 0x37,
    0x80,

    /* U+0068 "h" */
    0x82, 0x8, 0x3e, 0x86, 0x18, 0x61, 0x86, 0x18,
    0x40,

    /* U+0069 "i" */
    0x10, 0x40, 0x0, 0x60, 0x41, 0x4, 0x10, 0x41,
    0x3f,

    /* U+006A "j" */
    0x20, 0x0, 0x71, 0x11, 0x11, 0x12, 0xc0,

    /* U+006B "k" */
    0x82, 0x8, 0x26, 0xb3, 0xcd, 0x26, 0x8a, 0x18,
    0x40,

    /* U+006C "l" */
    0xe0, 0x82, 0x8, 0x20, 0x82, 0x8, 0x20, 0x81,
    0xc0,

    /* U+006D "m" */
    0xed, 0x6e, 0x4c, 0x98, 0x30, 0x60, 0xc1,

    /* U+006E "n" */
    0xfa, 0x18, 0x61, 0x86, 0x18, 0x61,

    /* U+006F "o" */
    0xfa, 0x18, 0x61, 0x86, 0x18, 0x5f,

    /* U+0070 "p" */
    0xfa, 0x18, 0x61, 0x86, 0x18, 0x7f, 0x82, 0x8,
    0x0,

    /* U+0071 "q" */
    0xfe, 0x18, 0x61, 0x86, 0x18, 0x5f, 0x4, 0x10,
    0x40,

    /* U+0072 "r" */
    0xdc, 0x40, 0x81, 0x2, 0x4, 0x8, 0x7c,

    /* U+0073 "s" */
    0xfe, 0x8, 0x1f, 0x4, 0x18, 0x7f,

    /* U+0074 "t" */
    0x41, 0x4, 0x3e, 0x41, 0x4, 0x10, 0x41, 0x13,
    0xc0,

    /* U+0075 "u" */
    0x86, 0x18, 0x61, 0x86, 0x18, 0x5f,

    /* U+0076 "v" */
    0x86, 0x18, 0x61, 0x85, 0x27, 0xc,

    /* U+0077 "w" */
    0x93, 0x26, 0x4c, 0x99, 0x32, 0x5f, 0x36,

    /* U+0078 "x" */
    0x86, 0x18, 0x9e, 0x4e, 0x18, 0x61,

    /* U+0079 "y" */
    0x86, 0x18, 0x61, 0x86, 0x1c, 0x5f, 0x4, 0x2f,
    0x0,

    /* U+007A "z" */
    0xfc, 0x10, 0x84, 0x31, 0x8c, 0x3f,

    /* U+007B "{" */
    0x1c, 0x41, 0x4, 0x10, 0x86, 0x18, 0x20, 0x41,
    0x4, 0x10, 0x70,

    /* U+007C "|" */
    0xff, 0xfc,

    /* U+007D "}" */
    0xe0, 0x82, 0x8, 0x20, 0x41, 0x86, 0x10, 0x82,
    0x8, 0x23, 0x80,

    /* U+007E "~" */
    0x1, 0xfc, 0x20
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 134, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 134, .box_w = 1, .box_h = 10, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 3, .adv_w = 134, .box_w = 4, .box_h = 4, .ofs_x = 2, .ofs_y = 6},
    {.bitmap_index = 5, .adv_w = 134, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 15, .adv_w = 134, .box_w = 7, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 28, .adv_w = 134, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 40, .adv_w = 134, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 50, .adv_w = 134, .box_w = 1, .box_h = 4, .ofs_x = 4, .ofs_y = 6},
    {.bitmap_index = 51, .adv_w = 134, .box_w = 4, .box_h = 15, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 59, .adv_w = 134, .box_w = 4, .box_h = 15, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 67, .adv_w = 134, .box_w = 5, .box_h = 6, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 71, .adv_w = 134, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 76, .adv_w = 134, .box_w = 2, .box_h = 5, .ofs_x = 3, .ofs_y = -3},
    {.bitmap_index = 78, .adv_w = 134, .box_w = 5, .box_h = 1, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 79, .adv_w = 134, .box_w = 2, .box_h = 2, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 80, .adv_w = 134, .box_w = 6, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 92, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 100, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 134, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 117, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 134, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 134, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 150, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 166, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 174, .adv_w = 134, .box_w = 2, .box_h = 7, .ofs_x = 3, .ofs_y = 1},
    {.bitmap_index = 176, .adv_w = 134, .box_w = 3, .box_h = 11, .ofs_x = 3, .ofs_y = -3},
    {.bitmap_index = 181, .adv_w = 134, .box_w = 5, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 188, .adv_w = 134, .box_w = 6, .box_h = 4, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 191, .adv_w = 134, .box_w = 5, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 198, .adv_w = 134, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 207, .adv_w = 134, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 218, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 226, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 234, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 242, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 250, .adv_w = 134, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 259, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 267, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 275, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 283, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 134, .box_w = 5, .box_h = 11, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 298, .adv_w = 134, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 307, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 315, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 323, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 331, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 339, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 347, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 356, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 364, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 372, .adv_w = 134, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 381, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 389, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 397, .adv_w = 134, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 406, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 414, .adv_w = 134, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 423, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 431, .adv_w = 134, .box_w = 4, .box_h = 14, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 438, .adv_w = 134, .box_w = 6, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 450, .adv_w = 134, .box_w = 4, .box_h = 14, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 457, .adv_w = 134, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 463, .adv_w = 134, .box_w = 7, .box_h = 1, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 464, .adv_w = 134, .box_w = 3, .box_h = 3, .ofs_x = 3, .ofs_y = 9},
    {.bitmap_index = 466, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 472, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 481, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 487, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 496, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 502, .adv_w = 134, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 510, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 519, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 528, .adv_w = 134, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 537, .adv_w = 134, .box_w = 4, .box_h = 13, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 544, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 553, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 562, .adv_w = 134, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 569, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 575, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 581, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 590, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 599, .adv_w = 134, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 606, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 612, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 621, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 627, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 633, .adv_w = 134, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 640, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 646, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 655, .adv_w = 134, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 661, .adv_w = 134, .box_w = 6, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 672, .adv_w = 134, .box_w = 1, .box_h = 14, .ofs_x = 4, .ofs_y = -3},
    {.bitmap_index = 674, .adv_w = 134, .box_w = 6, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 685, .adv_w = 134, .box_w = 7, .box_h = 3, .ofs_x = 1, .ofs_y = 3}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif

};

extern const lv_font_t lv_font_montserrat_14;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t font_kode_14 = {
#else
lv_font_t font_kode_14 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .static_bitmap = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_14,
#endif
    .user_data = NULL,
};



#endif /*#if FONT_KODE_14*/
