/*******************************************************************************
 * Size: 24 px
 * Bpp: 2
 * Opts: --bpp 2 --size 24 --no-compress --font 7segment.ttf --range 0-255 --format lvgl -o lcd_7_segment_24.c
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

#ifndef LCD_7_SEGMENT_24
#define LCD_7_SEGMENT_24 1
#endif

#if LCD_7_SEGMENT_24

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0022 "\"" */
    0x0, 0x0, 0x20, 0xd0, 0x2, 0xc3, 0xc0, 0xf,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf1, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x0, 0x3c, 0x10, 0x0,
    0x10,

    /* U+0027 "'" */
    0x0, 0x34, 0x3c, 0x3c, 0x3c, 0x7c, 0x7c, 0x78,
    0x10,

    /* U+0028 "(" */
    0xb, 0xff, 0x4d, 0xff, 0xc3, 0xd5, 0x40, 0xf0,
    0x0, 0x3c, 0x0, 0xf, 0x0, 0x7, 0xc0, 0x1,
    0xf0, 0x0, 0x10, 0x0, 0x4, 0x0, 0xb, 0x80,
    0x2, 0xe0, 0x0, 0xb8, 0x0, 0x2d, 0x0, 0xf,
    0x40, 0x3, 0xd5, 0x40, 0x9f, 0xfc, 0xf, 0xff,
    0x40,

    /* U+0029 ")" */
    0xb, 0xff, 0x90, 0xf, 0xfe, 0xc0, 0x5, 0x4f,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x0, 0x3c, 0x0, 0x0,
    0x10, 0x0, 0x1, 0x40, 0x0, 0x1e, 0x0, 0x0,
    0xb8, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x3d, 0x0, 0x54, 0xf4, 0xf, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+002C "," */
    0x15, 0xeb, 0xae, 0xbb, 0xdf, 0x6c, 0x10,

    /* U+002D "-" */
    0x5, 0x50, 0x3f, 0xfd, 0x2f, 0xfc,

    /* U+002E "." */
    0xb, 0xe0,

    /* U+0030 "0" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1f, 0x0, 0x3c, 0x10, 0x0,
    0x10, 0x40, 0x1, 0x4b, 0x80, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xd0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x54, 0xf4, 0x9f, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0031 "1" */
    0x8, 0x2c, 0x3c, 0x3c, 0x3c, 0x3c, 0x7c, 0x3c,
    0x4, 0x14, 0x78, 0xb8, 0xb8, 0xb8, 0xf4, 0xf4,
    0xb0, 0x10,

    /* U+0032 "2" */
    0xb, 0xff, 0x90, 0xf, 0xfe, 0xc0, 0x5, 0x4f,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x55, 0x7c, 0xf, 0xff,
    0x40, 0xaf, 0xfc, 0xb, 0x80, 0x0, 0x2e, 0x0,
    0x0, 0xb8, 0x0, 0x2, 0xd0, 0x0, 0xf, 0x40,
    0x0, 0x3d, 0x54, 0x0, 0x9f, 0xfc, 0x0, 0xff,
    0xf4, 0x0,

    /* U+0033 "3" */
    0xb, 0xff, 0x90, 0xf, 0xfe, 0xc0, 0x5, 0x4f,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x55, 0x7c, 0xf, 0xff,
    0x40, 0x2f, 0xfe, 0x40, 0x0, 0x1e, 0x0, 0x0,
    0xb8, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x3d, 0x0, 0x54, 0xf4, 0xf, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0034 "4" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x83, 0x40, 0xb,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0xf, 0x1f, 0x0, 0x7c, 0x79, 0x55,
    0xf0, 0x7f, 0xfd, 0x0, 0xbf, 0xf9, 0x0, 0x0,
    0x78, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x2e, 0x0, 0x0, 0xf4, 0x0, 0x3, 0xd0, 0x0,
    0x7, 0x0, 0x0, 0x4, 0x0,

    /* U+0035 "5" */
    0xb, 0xff, 0x43, 0x7f, 0xf0, 0x3d, 0x54, 0x3,
    0xc0, 0x0, 0x3c, 0x0, 0x3, 0xc0, 0x0, 0x7c,
    0x0, 0x7, 0x95, 0x40, 0x1f, 0xff, 0x40, 0xbf,
    0xf9, 0x0, 0x1, 0xe0, 0x0, 0x2e, 0x0, 0x2,
    0xe0, 0x0, 0x2e, 0x0, 0x3, 0xd0, 0x15, 0x3d,
    0xf, 0xfe, 0xc3, 0xff, 0xe4,

    /* U+0036 "6" */
    0xb, 0xff, 0x43, 0x7f, 0xf0, 0x3d, 0x54, 0x3,
    0xc0, 0x0, 0x3c, 0x0, 0x3, 0xc0, 0x0, 0x7c,
    0x0, 0x7, 0x95, 0x40, 0x1f, 0xff, 0x42, 0xbf,
    0xf9, 0xb8, 0x1, 0xeb, 0x80, 0x2e, 0xb8, 0x2,
    0xeb, 0x40, 0x2e, 0xf4, 0x3, 0xdf, 0x55, 0x3d,
    0x9f, 0xfe, 0xc3, 0xff, 0xe4,

    /* U+0037 "7" */
    0x2f, 0xfe, 0x40, 0xff, 0xec, 0x1, 0x53, 0xc0,
    0x0, 0x3c, 0x0, 0x3, 0xc0, 0x0, 0x3c, 0x0,
    0x7, 0xc0, 0x0, 0x7c, 0x0, 0x1, 0x40, 0x0,
    0x0, 0x0, 0x7, 0x80, 0x0, 0xb8, 0x0, 0xb,
    0x80, 0x0, 0xb8, 0x0, 0xf, 0x40, 0x0, 0xf4,
    0x0, 0xb, 0x40, 0x0, 0x60, 0x0, 0x0, 0x0,

    /* U+0038 "8" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x55, 0x7c, 0x1f, 0xff,
    0x40, 0xaf, 0xfe, 0x4b, 0x80, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xd0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x54, 0xf4, 0x9f, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0039 "9" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x55, 0x7c, 0x1f, 0xff,
    0x40, 0x2f, 0xfe, 0x40, 0x0, 0x1e, 0x0, 0x0,
    0xb8, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x3d, 0x0, 0x54, 0xf4, 0xf, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+003A ":" */
    0x14, 0x7c, 0x7c, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x10, 0xf4, 0xf4,

    /* U+003D "=" */
    0x1, 0x54, 0x3, 0xff, 0xd0, 0xbf, 0xf0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x1, 0x50, 0x3, 0xff, 0x3, 0xff,
    0xd0,

    /* U+003F "?" */
    0xb, 0xff, 0x90, 0xf, 0xfe, 0xc0, 0x5, 0x4f,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x0, 0x7c, 0xb, 0xff,
    0x50, 0x7f, 0xfd, 0xb, 0x95, 0x40, 0x2e, 0x0,
    0x0, 0xb8, 0x0, 0x2, 0xe0, 0x0, 0xf, 0x40,
    0x0, 0x3d, 0x0, 0x0, 0xd0, 0x0, 0x0, 0x0,
    0x0, 0x0,

    /* U+0041 "A" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1f, 0x0, 0x7c, 0x2b, 0xff,
    0x50, 0x7f, 0xfd, 0xb, 0x95, 0x5e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xe0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x0, 0xf4, 0xd0, 0x2, 0xd0, 0x0,
    0x6, 0x0, 0x0, 0x0, 0x0,

    /* U+0042 "B" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x55, 0x7c, 0x1f, 0xff,
    0x40, 0xaf, 0xfe, 0x4b, 0x80, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xd0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x54, 0xf4, 0x9f, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0043 "C" */
    0xb, 0xff, 0x4d, 0xff, 0xc3, 0xd5, 0x40, 0xf0,
    0x0, 0x3c, 0x0, 0xf, 0x0, 0x7, 0xc0, 0x1,
    0xf0, 0x0, 0x10, 0x0, 0x4, 0x0, 0xb, 0x80,
    0x2, 0xe0, 0x0, 0xb8, 0x0, 0x2d, 0x0, 0xf,
    0x40, 0x3, 0xd5, 0x40, 0x9f, 0xfc, 0xf, 0xff,
    0x40,

    /* U+0044 "D" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1f, 0x0, 0x3c, 0x10, 0x0,
    0x10, 0x40, 0x1, 0x4b, 0x80, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xd0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x54, 0xf4, 0x9f, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0045 "E" */
    0xb, 0xff, 0x4d, 0xff, 0xc3, 0xd5, 0x40, 0xf0,
    0x0, 0x3c, 0x0, 0xf, 0x0, 0x7, 0xc0, 0x1,
    0xe5, 0x50, 0x1f, 0xff, 0x4a, 0xff, 0xcb, 0x80,
    0x2, 0xe0, 0x0, 0xb8, 0x0, 0x2d, 0x0, 0xf,
    0x40, 0x3, 0xd5, 0x40, 0x9f, 0xfc, 0xf, 0xff,
    0x40,

    /* U+0046 "F" */
    0xb, 0xff, 0x4d, 0xff, 0xc3, 0xd5, 0x40, 0xf0,
    0x0, 0x3c, 0x0, 0xf, 0x0, 0x7, 0xc0, 0x1,
    0xf0, 0x0, 0x2b, 0xff, 0x7, 0xff, 0xdb, 0x95,
    0x42, 0xe0, 0x0, 0xb8, 0x0, 0x2e, 0x0, 0xf,
    0x40, 0x3, 0xd0, 0x0, 0xd0, 0x0, 0x0, 0x0,
    0x0,

    /* U+0047 "G" */
    0xb, 0xff, 0x43, 0x7f, 0xf0, 0x3d, 0x54, 0x3,
    0xc0, 0x0, 0x3c, 0x0, 0x3, 0xc0, 0x0, 0x7c,
    0x0, 0x7, 0xc0, 0x0, 0x10, 0x0, 0x1, 0x0,
    0x5, 0xb8, 0x1, 0xeb, 0x80, 0x2e, 0xb8, 0x2,
    0xeb, 0x40, 0x2e, 0xf4, 0x3, 0xdf, 0x55, 0x3d,
    0x9f, 0xfe, 0xc3, 0xff, 0xe4,

    /* U+0048 "H" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x83, 0x40, 0xb,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0xf, 0x1f, 0x0, 0x7c, 0x79, 0x55,
    0xf0, 0x7f, 0xfd, 0x2, 0xbf, 0xf9, 0x2e, 0x0,
    0x78, 0xb8, 0x2, 0xe2, 0xe0, 0xb, 0x8b, 0x40,
    0x2e, 0x3d, 0x0, 0xf4, 0xf0, 0x3, 0xd2, 0x0,
    0x7, 0x0, 0x0, 0x4, 0x0,

    /* U+0049 "I" */
    0x8, 0x2c, 0x3c, 0x3c, 0x3c, 0x3c, 0x7c, 0x3c,
    0x4, 0x14, 0x78, 0xb8, 0xb8, 0xb8, 0xf4, 0xf4,
    0xb0, 0x10,

    /* U+004A "J" */
    0x0, 0x0, 0x20, 0x0, 0x2, 0xc0, 0x0, 0xf,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x0, 0x3c, 0x0, 0x0,
    0x10, 0x80, 0x1, 0x4b, 0x80, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xd0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x54, 0xf4, 0x9f, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+004B "K" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x83, 0x40, 0xb,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0xf, 0x1f, 0x0, 0x7c, 0x79, 0x55,
    0xf0, 0x7f, 0xfd, 0x2, 0xbf, 0xf0, 0x2e, 0x0,
    0x0, 0xb8, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x40,
    0x0, 0x3d, 0x0, 0x0, 0xf5, 0x50, 0x2, 0x7f,
    0xf0, 0x3, 0xff, 0xd0, 0x0,

    /* U+004C "L" */
    0x0, 0x0, 0x34, 0x0, 0x3c, 0x0, 0x3c, 0x0,
    0x3c, 0x0, 0x7c, 0x0, 0x7c, 0x0, 0x78, 0x0,
    0x10, 0x0, 0x20, 0x0, 0xb8, 0x0, 0xb8, 0x0,
    0xb8, 0x0, 0xb4, 0x0, 0xf4, 0x0, 0xf5, 0x50,
    0x9f, 0xfc, 0x3f, 0xfd,

    /* U+004D "M" */
    0xb, 0xff, 0x40, 0x3f, 0xf0, 0x0, 0x54, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0xb, 0xff, 0x1, 0xff,
    0xf4, 0xb9, 0x55, 0xeb, 0x80, 0x2e, 0xb8, 0x2,
    0xeb, 0x80, 0x2e, 0xf4, 0x3, 0xdf, 0x40, 0x3d,
    0xd0, 0x2, 0xd0, 0x0, 0x18, 0x0, 0x0, 0x0,

    /* U+004E "N" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1f, 0x0, 0x7c, 0x20, 0x0,
    0x50, 0x40, 0x0, 0xb, 0x40, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xe0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x0, 0xf4, 0xd0, 0x2, 0xd0, 0x0,
    0x6, 0x0, 0x0, 0x0, 0x0,

    /* U+004F "O" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1f, 0x0, 0x3c, 0x10, 0x0,
    0x10, 0x40, 0x1, 0x4b, 0x80, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xd0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x54, 0xf4, 0x9f, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0050 "P" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1f, 0x0, 0x7c, 0x2b, 0xff,
    0x50, 0x7f, 0xfd, 0xb, 0x95, 0x40, 0x2e, 0x0,
    0x0, 0xb8, 0x0, 0x2, 0xe0, 0x0, 0xf, 0x40,
    0x0, 0x3d, 0x0, 0x0, 0xd0, 0x0, 0x0, 0x0,
    0x0, 0x0,

    /* U+0051 "Q" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x55, 0x7c, 0x1f, 0xff,
    0x40, 0x2f, 0xfc, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x54, 0x0, 0xf, 0xfc, 0x0, 0xff,
    0xf4, 0x0,

    /* U+0052 "R" */
    0xb, 0xff, 0x4d, 0xff, 0xc3, 0xd5, 0x40, 0xf0,
    0x0, 0x3c, 0x0, 0xf, 0x0, 0x7, 0xc0, 0x1,
    0xf0, 0x0, 0x20, 0x0, 0x4, 0x0, 0xb, 0x40,
    0x2, 0xe0, 0x0, 0xb8, 0x0, 0x2e, 0x0, 0xf,
    0x40, 0x3, 0xd0, 0x0, 0xd0, 0x0, 0x0, 0x0,
    0x0,

    /* U+0053 "S" */
    0xb, 0xff, 0x43, 0x7f, 0xf0, 0x3d, 0x54, 0x3,
    0xc0, 0x0, 0x3c, 0x0, 0x3, 0xc0, 0x0, 0x7c,
    0x0, 0x7, 0x95, 0x40, 0x1f, 0xff, 0x40, 0xbf,
    0xf9, 0x0, 0x1, 0xe0, 0x0, 0x2e, 0x0, 0x2,
    0xe0, 0x0, 0x2e, 0x0, 0x3, 0xd0, 0x15, 0x3d,
    0xf, 0xfe, 0xc3, 0xff, 0xe4,

    /* U+0054 "T" */
    0x2f, 0xfe, 0x40, 0xff, 0xec, 0x1, 0x53, 0xc0,
    0x0, 0x3c, 0x0, 0x3, 0xc0, 0x0, 0x3c, 0x0,
    0x7, 0xc0, 0x0, 0x7c, 0x0, 0x1, 0x40, 0x0,
    0x0, 0x0, 0x7, 0x80, 0x0, 0xb8, 0x0, 0xb,
    0x80, 0x0, 0xb8, 0x0, 0xf, 0x40, 0x0, 0xf4,
    0x0, 0xb, 0x40, 0x0, 0x60, 0x0, 0x0, 0x0,

    /* U+0055 "U" */
    0x0, 0x0, 0x20, 0xd0, 0x2, 0xc3, 0xc0, 0xf,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf1, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x0, 0x3c, 0x10, 0x0,
    0x10, 0x80, 0x1, 0x4b, 0x80, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xd0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x54, 0xf4, 0x9f, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0056 "V" */
    0x0, 0x0, 0x20, 0xd0, 0x2, 0xc3, 0xc0, 0xf,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf1, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x0, 0x3c, 0x10, 0x0,
    0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x54, 0x0, 0xf, 0xfc, 0x0, 0xff,
    0xf4, 0x0,

    /* U+0057 "W" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x83, 0x40, 0xb,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0xf, 0x1f, 0x0, 0x7c, 0x79, 0x55,
    0xf0, 0x7f, 0xfd, 0x2, 0xbf, 0xf9, 0x2e, 0x0,
    0x78, 0xb8, 0x2, 0xe2, 0xe0, 0xb, 0x8b, 0x40,
    0x2e, 0x3d, 0x0, 0xf4, 0xf5, 0x53, 0xd2, 0x7f,
    0xfb, 0x3, 0xff, 0xe4, 0x0,

    /* U+0058 "X" */
    0xb, 0xff, 0x40, 0xff, 0xc0, 0x5, 0x40, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x5, 0x50, 0xf, 0xff, 0x42, 0xff, 0xc0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x5, 0x40, 0xf, 0xfc, 0xf, 0xff,
    0x40,

    /* U+0059 "Y" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x83, 0x40, 0xb,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0xf, 0x1f, 0x0, 0x7c, 0x79, 0x55,
    0xf0, 0x7f, 0xfd, 0x0, 0xbf, 0xf9, 0x0, 0x0,
    0x78, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x2e, 0x0, 0x0, 0xf4, 0x1, 0x53, 0xd0, 0x3f,
    0xfb, 0x3, 0xff, 0xe4, 0x0,

    /* U+005A "Z" */
    0xb, 0xff, 0x90, 0xf, 0xfe, 0xc0, 0x5, 0x4f,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x55, 0x7c, 0xf, 0xff,
    0x40, 0xaf, 0xfc, 0xb, 0x80, 0x0, 0x2e, 0x0,
    0x0, 0xb8, 0x0, 0x2, 0xd0, 0x0, 0xf, 0x40,
    0x0, 0x3d, 0x54, 0x0, 0x9f, 0xfc, 0x0, 0xff,
    0xf4, 0x0,

    /* U+005B "[" */
    0xb, 0xff, 0x4d, 0xff, 0xc3, 0xd5, 0x40, 0xf0,
    0x0, 0x3c, 0x0, 0xf, 0x0, 0x7, 0xc0, 0x1,
    0xf0, 0x0, 0x10, 0x0, 0x4, 0x0, 0xb, 0x80,
    0x2, 0xe0, 0x0, 0xb8, 0x0, 0x2d, 0x0, 0xf,
    0x40, 0x3, 0xd5, 0x40, 0x9f, 0xfc, 0xf, 0xff,
    0x40,

    /* U+005D "]" */
    0xb, 0xff, 0x90, 0xf, 0xfe, 0xc0, 0x5, 0x4f,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x0, 0x3c, 0x0, 0x0,
    0x10, 0x0, 0x1, 0x40, 0x0, 0x1e, 0x0, 0x0,
    0xb8, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x3d, 0x0, 0x54, 0xf4, 0xf, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+005F "_" */
    0x1, 0x50, 0xf, 0xfc, 0x3f, 0xfd,

    /* U+0060 "`" */
    0x0, 0x34, 0x3c, 0x3c, 0x3c, 0x7c, 0x7c, 0x78,
    0x10,

    /* U+0061 "a" */
    0xb, 0xff, 0x90, 0xf, 0xfe, 0xc0, 0x5, 0x4f,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x55, 0x7c, 0xf, 0xff,
    0x40, 0xaf, 0xfe, 0x4b, 0x80, 0x1e, 0x2e, 0x0,
    0xb8, 0xb8, 0x2, 0xe2, 0xd0, 0xb, 0x8f, 0x40,
    0x3d, 0x3d, 0x54, 0xf4, 0x9f, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0062 "b" */
    0x0, 0x0, 0x3, 0x40, 0x0, 0x3c, 0x0, 0x3,
    0xc0, 0x0, 0x3c, 0x0, 0x7, 0xc0, 0x0, 0x7c,
    0x0, 0x7, 0x95, 0x40, 0x1f, 0xff, 0x42, 0xbf,
    0xf9, 0xb8, 0x1, 0xeb, 0x80, 0x2e, 0xb8, 0x2,
    0xeb, 0x40, 0x2e, 0xf4, 0x3, 0xdf, 0x55, 0x3d,
    0x9f, 0xfe, 0xc3, 0xff, 0xe4,

    /* U+0063 "c" */
    0x1, 0x54, 0x3, 0xff, 0xd2, 0xbf, 0xf2, 0xe0,
    0x0, 0xb8, 0x0, 0x2e, 0x0, 0xb, 0x40, 0x3,
    0xd0, 0x0, 0xf5, 0x50, 0x27, 0xff, 0x3, 0xff,
    0xd0,

    /* U+0064 "d" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0xb,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0xf, 0x0, 0x0, 0x7c, 0x1, 0x55,
    0xf0, 0x3f, 0xfd, 0x2, 0xbf, 0xf9, 0x2e, 0x0,
    0x78, 0xb8, 0x2, 0xe2, 0xe0, 0xb, 0x8b, 0x40,
    0x2e, 0x3d, 0x0, 0xf4, 0xf5, 0x53, 0xd2, 0x7f,
    0xfb, 0x3, 0xff, 0xe4, 0x0,

    /* U+0065 "e" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x55, 0x7c, 0x1f, 0xff,
    0x40, 0xaf, 0xfc, 0xb, 0x80, 0x0, 0x2e, 0x0,
    0x0, 0xb8, 0x0, 0x2, 0xd0, 0x0, 0xf, 0x40,
    0x0, 0x3d, 0x54, 0x0, 0x9f, 0xfc, 0x0, 0xff,
    0xf4, 0x0,

    /* U+0066 "f" */
    0xb, 0xff, 0x4d, 0xff, 0xc3, 0xd5, 0x40, 0xf0,
    0x0, 0x3c, 0x0, 0xf, 0x0, 0x7, 0xc0, 0x1,
    0xf0, 0x0, 0x2b, 0xff, 0x7, 0xff, 0xdb, 0x95,
    0x42, 0xe0, 0x0, 0xb8, 0x0, 0x2e, 0x0, 0xf,
    0x40, 0x3, 0xd0, 0x0, 0xd0, 0x0, 0x0, 0x0,
    0x0,

    /* U+0067 "g" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x55, 0x7c, 0x1f, 0xff,
    0x40, 0x2f, 0xfe, 0x40, 0x0, 0x1e, 0x0, 0x0,
    0xb8, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x3d, 0x0, 0x54, 0xf4, 0xf, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+0068 "h" */
    0x0, 0x0, 0x3, 0x40, 0x0, 0x3c, 0x0, 0x3,
    0xc0, 0x0, 0x3c, 0x0, 0x7, 0xc0, 0x0, 0x7c,
    0x0, 0x7, 0x95, 0x40, 0x1f, 0xff, 0x42, 0xbf,
    0xf9, 0xb8, 0x1, 0xeb, 0x80, 0x2e, 0xb8, 0x2,
    0xeb, 0x40, 0x2e, 0xf4, 0x3, 0xdf, 0x0, 0x3d,
    0x80, 0x1, 0xc0, 0x0, 0x4,

    /* U+0069 "i" */
    0x15, 0xeb, 0xae, 0xbb, 0xdf, 0x6c, 0x10,

    /* U+006A "j" */
    0x0, 0x0, 0x20, 0x0, 0x2, 0xc0, 0x0, 0xf,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x0, 0x3c, 0x0, 0x0,
    0x10, 0x0, 0x1, 0x40, 0x0, 0x1e, 0x0, 0x0,
    0xb8, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x3d, 0x0, 0x54, 0xf4, 0xf, 0xfe, 0xc0, 0xff,
    0xf9, 0x0,

    /* U+006B "k" */
    0xb, 0xff, 0x43, 0x7f, 0xf0, 0x3d, 0x54, 0x3,
    0xc0, 0x0, 0x3c, 0x0, 0x3, 0xc0, 0x0, 0x7c,
    0x0, 0x7, 0xc0, 0x0, 0x2b, 0xff, 0x1, 0xff,
    0xf4, 0xb9, 0x55, 0xeb, 0x80, 0x2e, 0xb8, 0x2,
    0xeb, 0x80, 0x2e, 0xf4, 0x3, 0xdf, 0x40, 0x3d,
    0xd0, 0x2, 0xd0, 0x0, 0x18, 0x0, 0x0, 0x0,

    /* U+006C "l" */
    0x0, 0x0, 0x34, 0x0, 0x3c, 0x0, 0x3c, 0x0,
    0x3c, 0x0, 0x7c, 0x0, 0x7c, 0x0, 0x78, 0x0,
    0x10, 0x0, 0x20, 0x0, 0xb8, 0x0, 0xb8, 0x0,
    0xb8, 0x0, 0xb4, 0x0, 0xf4, 0x0, 0xf5, 0x50,
    0x9f, 0xfc, 0x3f, 0xfd,

    /* U+006D "m" */
    0xb, 0xff, 0x40, 0x3f, 0xf0, 0x0, 0x54, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0xb, 0xff, 0x1, 0xff,
    0xf4, 0xb9, 0x55, 0xeb, 0x80, 0x2e, 0xb8, 0x2,
    0xeb, 0x80, 0x2e, 0xf4, 0x3, 0xdf, 0x40, 0x3d,
    0xd0, 0x2, 0xd0, 0x0, 0x18, 0x0, 0x0, 0x0,

    /* U+006E "n" */
    0x1, 0x54, 0x0, 0xff, 0xf4, 0x2b, 0xff, 0x9b,
    0x80, 0x1e, 0xb8, 0x2, 0xeb, 0x80, 0x2e, 0xb4,
    0x2, 0xef, 0x40, 0x3d, 0xf0, 0x3, 0xd8, 0x0,
    0x1c, 0x0, 0x0, 0x40,

    /* U+006F "o" */
    0x1, 0x54, 0x0, 0xff, 0xf4, 0x2b, 0xff, 0x9b,
    0x80, 0x1e, 0xb8, 0x2, 0xeb, 0x80, 0x2e, 0xb4,
    0x2, 0xef, 0x40, 0x3d, 0xf5, 0x53, 0xd9, 0xff,
    0xec, 0x3f, 0xfe, 0x40,

    /* U+0070 "p" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1f, 0x0, 0x7c, 0x2b, 0xff,
    0x50, 0x7f, 0xfd, 0xb, 0x95, 0x40, 0x2e, 0x0,
    0x0, 0xb8, 0x0, 0x2, 0xe0, 0x0, 0xf, 0x40,
    0x0, 0x3d, 0x0, 0x0, 0xd0, 0x0, 0x0, 0x0,
    0x0, 0x0,

    /* U+0071 "q" */
    0xb, 0xff, 0x90, 0xdf, 0xfe, 0xc3, 0xd5, 0x4f,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1f, 0x0, 0x7c, 0x2b, 0xff,
    0x50, 0x3f, 0xfd, 0x0, 0x15, 0x5e, 0x0, 0x0,
    0xb8, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x3d, 0x0, 0x0, 0xf4, 0x0, 0x2, 0xd0, 0x0,
    0x6, 0x0, 0x0, 0x0, 0x0,

    /* U+0072 "r" */
    0x1, 0x54, 0x3, 0xff, 0xd2, 0xbf, 0xf2, 0xe0,
    0x0, 0xb8, 0x0, 0x2e, 0x0, 0xb, 0x40, 0x3,
    0xd0, 0x0, 0xf0, 0x0, 0x20, 0x0, 0x0, 0x0,
    0x0,

    /* U+0073 "s" */
    0x0, 0x0, 0x3, 0x40, 0x0, 0x3c, 0x0, 0x3,
    0xc0, 0x0, 0x3c, 0x0, 0x7, 0xc0, 0x0, 0x7c,
    0x0, 0x7, 0x95, 0x40, 0x1f, 0xff, 0x40, 0xbf,
    0xf9, 0x0, 0x1, 0xe0, 0x0, 0x2e, 0x0, 0x2,
    0xe0, 0x0, 0x2e, 0x0, 0x3, 0xd0, 0x0, 0x3d,
    0x0, 0x1, 0xc0, 0x0, 0x4,

    /* U+0074 "t" */
    0x0, 0x0, 0xd, 0x0, 0x3, 0xc0, 0x0, 0xf0,
    0x0, 0x3c, 0x0, 0x1f, 0x0, 0x7, 0xc0, 0x1,
    0xe5, 0x50, 0x1f, 0xff, 0x4a, 0xff, 0xcb, 0x80,
    0x2, 0xe0, 0x0, 0xb8, 0x0, 0x2d, 0x0, 0xf,
    0x40, 0x3, 0xd5, 0x40, 0x9f, 0xfc, 0xf, 0xff,
    0x40,

    /* U+0075 "u" */
    0x20, 0x0, 0x5b, 0x80, 0x1e, 0xb8, 0x2, 0xeb,
    0x80, 0x2e, 0xb4, 0x2, 0xef, 0x40, 0x3d, 0xf5,
    0x53, 0xd9, 0xff, 0xec, 0x3f, 0xfe, 0x40,

    /* U+0076 "v" */
    0x0, 0x0, 0x20, 0xd0, 0x2, 0xc3, 0xc0, 0xf,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf1, 0xf0, 0x3,
    0xc7, 0xc0, 0x1f, 0x1e, 0x0, 0x3c, 0x10, 0x0,
    0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x54, 0x0, 0xf, 0xfc, 0x0, 0xff,
    0xf4, 0x0,

    /* U+0077 "w" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x83, 0x40, 0xb,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0xf, 0x1f, 0x0, 0x7c, 0x79, 0x55,
    0xf0, 0x7f, 0xfd, 0x2, 0xbf, 0xf9, 0x2e, 0x0,
    0x78, 0xb8, 0x2, 0xe2, 0xe0, 0xb, 0x8b, 0x40,
    0x2e, 0x3d, 0x0, 0xf4, 0xf5, 0x53, 0xd2, 0x7f,
    0xfb, 0x3, 0xff, 0xe4, 0x0,

    /* U+0078 "x" */
    0xb, 0xff, 0x40, 0xff, 0xc0, 0x5, 0x40, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x5, 0x50, 0xf, 0xff, 0x42, 0xff, 0xc0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x5, 0x40, 0xf, 0xfc, 0xf, 0xff,
    0x40,

    /* U+0079 "y" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x83, 0x40, 0xb,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0xf, 0x1f, 0x0, 0x7c, 0x79, 0x55,
    0xf0, 0x7f, 0xfd, 0x0, 0xbf, 0xf9, 0x0, 0x0,
    0x78, 0x0, 0x2, 0xe0, 0x0, 0xb, 0x80, 0x0,
    0x2e, 0x0, 0x0, 0xf4, 0x1, 0x53, 0xd0, 0x3f,
    0xfb, 0x3, 0xff, 0xe4, 0x0,

    /* U+007A "z" */
    0xb, 0xff, 0x90, 0xf, 0xfe, 0xc0, 0x5, 0x4f,
    0x0, 0x0, 0x3c, 0x0, 0x0, 0xf0, 0x0, 0x3,
    0xc0, 0x0, 0x1f, 0x0, 0x55, 0x7c, 0xf, 0xff,
    0x40, 0xaf, 0xfc, 0xb, 0x80, 0x0, 0x2e, 0x0,
    0x0, 0xb8, 0x0, 0x2, 0xd0, 0x0, 0xf, 0x40,
    0x0, 0x3d, 0x54, 0x0, 0x9f, 0xfc, 0x0, 0xff,
    0xf4, 0x0,

    /* U+007C "|" */
    0x0, 0x34, 0x3c, 0x3c, 0x3c, 0x7c, 0x7c, 0x78,
    0x10, 0x20, 0xb8, 0xb8, 0xb8, 0xb4, 0xf4, 0xf0,
    0x90, 0x0,

    /* U+00B0 "°" */
    0x1, 0x55, 0x0, 0x3f, 0xfe, 0x83, 0xbf, 0xfb,
    0xf, 0x0, 0x3c, 0x3c, 0x0, 0xf0, 0xf0, 0x3,
    0xc7, 0xc0, 0xf, 0x1f, 0x0, 0x7c, 0x79, 0x55,
    0xf0, 0x7f, 0xfd, 0x0, 0xbf, 0xf0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 180, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 180, .box_w = 11, .box_h = 9, .ofs_x = 1, .ofs_y = 9},
    {.bitmap_index = 25, .adv_w = 180, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 9},
    {.bitmap_index = 34, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 75, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 180, .box_w = 3, .box_h = 9, .ofs_x = 8, .ofs_y = 0},
    {.bitmap_index = 132, .adv_w = 180, .box_w = 8, .box_h = 3, .ofs_x = 2, .ofs_y = 8},
    {.bitmap_index = 138, .adv_w = 0, .box_w = 2, .box_h = 3, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 140, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 180, .box_w = 4, .box_h = 18, .ofs_x = 8, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 258, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 308, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 361, .adv_w = 180, .box_w = 10, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 406, .adv_w = 180, .box_w = 10, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 451, .adv_w = 180, .box_w = 10, .box_h = 19, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 499, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 549, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 599, .adv_w = 68, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 610, .adv_w = 180, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 635, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 685, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 738, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 788, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 829, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 879, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 920, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 961, .adv_w = 180, .box_w = 10, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1006, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1059, .adv_w = 180, .box_w = 4, .box_h = 18, .ofs_x = 8, .ofs_y = 0},
    {.bitmap_index = 1077, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1127, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1180, .adv_w = 180, .box_w = 8, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1216, .adv_w = 180, .box_w = 10, .box_h = 19, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 1264, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 1317, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1367, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1417, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1467, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1508, .adv_w = 180, .box_w = 10, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1553, .adv_w = 180, .box_w = 10, .box_h = 19, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 1601, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1651, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1701, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1754, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1795, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1848, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1898, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1939, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1989, .adv_w = 180, .box_w = 8, .box_h = 3, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1995, .adv_w = 180, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 9},
    {.bitmap_index = 2004, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2054, .adv_w = 180, .box_w = 10, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2099, .adv_w = 180, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2124, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2177, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2227, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2268, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2318, .adv_w = 180, .box_w = 10, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2363, .adv_w = 180, .box_w = 3, .box_h = 9, .ofs_x = 8, .ofs_y = 0},
    {.bitmap_index = 2370, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2420, .adv_w = 180, .box_w = 10, .box_h = 19, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 2468, .adv_w = 180, .box_w = 8, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2504, .adv_w = 180, .box_w = 10, .box_h = 19, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 2552, .adv_w = 180, .box_w = 10, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2580, .adv_w = 180, .box_w = 10, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2608, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2658, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 2711, .adv_w = 180, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2736, .adv_w = 180, .box_w = 10, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2781, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2822, .adv_w = 180, .box_w = 10, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2845, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2895, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2948, .adv_w = 180, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2989, .adv_w = 180, .box_w = 11, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 3042, .adv_w = 180, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 3092, .adv_w = 180, .box_w = 4, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 3110, .adv_w = 180, .box_w = 11, .box_h = 11, .ofs_x = 1, .ofs_y = 8}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint8_t glyph_id_ofs_list_0[] = {
    0, 0, 1, 0, 0, 0, 0, 2,
    3, 4, 0, 0, 5, 6, 7, 0,
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 0, 0, 19, 0, 20,
    0, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35,
    36, 37, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 47, 0, 48
};

static const uint16_t unicode_list_2[] = {
    0x0, 0x34
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 62, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = glyph_id_ofs_list_0, .list_length = 62, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    },
    {
        .range_start = 95, .range_length = 28, .glyph_id_start = 50,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 124, .range_length = 53, .glyph_id_start = 78,
        .unicode_list = unicode_list_2, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] =
{
    8, 8
};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] =
{
    45
};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs =
{
    .glyph_ids = kern_pair_glyph_ids,
    .values = kern_pair_values,
    .pair_cnt = 1,
    .glyph_ids_size = 0
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
    .kern_dsc = &kern_pairs,
    .kern_scale = 16,
    .cmap_num = 3,
    .bpp = 2,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lcd_7_segment_24 = {
#else
lv_font_t lcd_7_segment_24 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 20,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LCD_7_SEGMENT_24*/

