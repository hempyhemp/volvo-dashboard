#ifndef FONT_CYRILLIC_16_H
#define FONT_CYRILLIC_16_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Шрифт сгенерирован из Segoe UI (есть в комплекте Windows) через
// официальную утилиту lv_font_conv (https://github.com/lvgl/lv_font_conv):
//
//   npx lv_font_conv --font segoeui.ttf --size 16 --bpp 4 --no-compress \
//       -r 0x20-0x7E -r 0x400-0x45F \
//       --format lvgl --lv-font-name font_cyrillic_16 -o font_cyrillic_16.c
//
// --no-compress обязателен: в lv_conf.h LV_USE_FONT_COMPRESSED = 0
// (стандартное значение шаблона), а lv_font_conv по умолчанию сжимает
// битмапы через RLE — при несовпадении глифы просто не рисуются.
//
// Диапазоны: 0x20-0x7E — ASCII (латиница, цифры, пунктуация),
//            0x400-0x45F — кириллица (А-я, Ё/ё и т.п.).
// Встроенные шрифты LVGL (Montserrat и др.) кириллицу не содержат —
// поэтому для русского текста нужен именно такой, отдельно собранный шрифт.
extern const lv_font_t font_cyrillic_16;

#ifdef __cplusplus
}
#endif

#endif
