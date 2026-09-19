// LVGL configuration for the host simulator. Mirrors the device's generated lv_conf.h
// (firmware/.esphome/build/hallboard/src/lv_conf.h) wherever a setting changes what is drawn or
// which widget exists, and differs only where the host has no ESP32 behind it. The differences,
// all deliberate:
//
//   LV_COLOR_16_SWAP        0 here, 1 on the device. Byte order in the frame buffer only; the
//                           simulator reads the buffer itself, so it wants native order.
//   LV_USE_STDLIB_MALLOC    CLIB here; the device hands LVGL ESPHome's allocator.
//   LV_USE_TINY_TTF         on here only. The device rasterises its fonts at build time in
//                           ESPHome; the simulator has to do it at run time from the TTFs.
//   LV_USE_LODEPNG          on here only, to write the PNGs.
//   LV_FONT_MONTSERRAT_*    the same set the device enables, so the direct references in
//                           hb_ui.h resolve to the same bitmaps.
//
// Everything else below is the device's value.
#pragma once
// lv_conf_internal.h warns unless this is defined, its check that the include really happened.
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_CHROMA_KEY lv_color_make(0, 4, 0)

#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 0
#define LV_BUILD_DEMOS 0
#define LV_BUILD_EXAMPLES 0
#define LV_DEF_REFR_PERIOD 16
#define LV_DRAW_BUF_ALIGN 32
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_GRADIENT_MAX_STOPS 2

#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB

#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 0
#define LV_DRAW_SW_SUPPORT_A8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_RGB888 1
#define LV_DRAW_SW_SUPPORT_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_USE_DRAW_SW 1

#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_USE_FILE_LINE 0
#define LV_LOG_USE_TIMESTAMP 0
#define LV_USE_LOG 1
// LVGL's default assert handler is `while(1)`, which on a host is a hang and not a failure. The
// simulator would rather stop with a message on the first bad argument.
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();

// Fonts. The four custom sizes (120 clock, 56 wordmark, 64 pair, 48 icons) come from TinyTTF at
// run time; the rest are the same built-in bitmaps the device compiles in.
#define LV_FONT_DEFAULT &lv_font_montserrat_24
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_USE_FONT 1
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0

#define LV_USE_LODEPNG 1

// Widgets, exactly the device's set. Turning one on that the device has off would let a change to
// hb_ui.h compile here and fail on the board.
#define LV_LABEL_LONG_TXT_HINT 0
#define LV_LABEL_TEXT_SELECTION 0
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_ARCLABEL 0
#define LV_USE_BAR 0
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX_BTN 0
#define LV_USE_BUTTON 1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 1
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_CONTAINER 0
#define LV_USE_DROPDOWN 0
#define LV_USE_DROPDOWN_LIST 0
#define LV_USE_FLEX 0
#define LV_USE_GRADIENT 1
#define LV_USE_GRID 0
#define LV_USE_IMAGE 1
#define LV_USE_KEYBOARD 1
#define LV_USE_KEY_LISTENER 1
#define LV_USE_LABEL 1
#define LV_USE_LED 0
#define LV_USE_LINE 1
#define LV_USE_LIST 0
#define LV_USE_LV_TILEVIEW_TILE_T 0
#define LV_USE_LZ4 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_OBJ 1
#define LV_USE_OBJ_ID_BUILTIN 0
#define LV_USE_OBJ_PROPERTY_NAME 0
#define LV_USE_OBSERVER 0
#define LV_USE_PAGE 0
#define LV_USE_QRCODE 1
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_STYLE 1
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 1
#define LV_USE_THORVG 0
#define LV_USE_TILEVIEW 0
#define LV_USE_USER_DATA 1
#define LV_USE_WIN 0
#define LV_WIDGETS_HAS_DEFAULT_VALUE 0

// The default theme is on, as it is on the device. It never reaches a page: every panel in
// hb_ui.h is built by lv_container_create, whose own object class the theme does not match.
#define LV_USE_THEME 1
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_MONO 0
#define LV_USE_THEME_SIMPLE 0
#define LV_THEME_DEFAULT_GROW 0

// No touchscreen, no display driver and no profiler on the host.
#define LV_USE_TOUCHSCREEN 0
#define LV_USE_GENERIC_MIPI 0
#define LV_USE_PROFILER_BUILTIN 0
#define LV_PROFILER_BUILTIN_DEFAULT_ENABLE 0
#define LV_USE_FREERTOS_TASK_NOTIFY 0
#define LV_FILE_EXPLORER_QUICK_ACCESS 0
#define LV_IME_PINYIN_USE_DEFAULT_DICT 0
#define LV_IME_PINYIN_USE_K9_MODE 0
