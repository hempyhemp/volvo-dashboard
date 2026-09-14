#include "screen_debug.h"
#include "fonts/font_cyrillic_16.h"

// Тестовый экран из ЭТАПА 1-3: проверка того, что железо/LVGL/шрифты
// вообще работают. Ничего "приборного" тут нет — это техническая вкладка.

static lv_obj_t *uptime_label;

void screen_debug_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  lv_obj_t *frame = lv_obj_create(parent);
  lv_obj_remove_style_all(frame);
  lv_obj_set_size(frame, lv_pct(100), lv_pct(100));
  lv_obj_set_style_border_width(frame, 2, 0);
  lv_obj_set_style_border_color(frame, lv_color_white(), 0);
  lv_obj_center(frame);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "ESP32-2432S028");
  lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

  lv_obj_t *hello = lv_label_create(parent);
  lv_label_set_text(hello, "HELLO");
  lv_obj_set_style_text_color(hello, lv_color_hex(0xFFFF00), 0);
  lv_obj_align(hello, LV_ALIGN_TOP_LEFT, 10, 45);

  // Кириллица — обычные встроенные шрифты LVGL (Montserrat и т.п.) её не
  // содержат, поэтому явно задаём кастомный шрифт (lib/ui_demo/fonts/).
  lv_obj_t *name = lv_label_create(parent);
  lv_label_set_text(name, "Сашааа");
  lv_obj_set_style_text_color(name, lv_color_hex(0xFFFF00), 0);
  lv_obj_set_style_text_font(name, &font_cyrillic_16, 0);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 10, 75);

  lv_obj_t *ok = lv_label_create(parent);
  lv_label_set_text(ok, "DISPLAY OK");
  lv_obj_set_style_text_color(ok, lv_color_hex(0x00FFFF), 0);
  lv_obj_align(ok, LV_ALIGN_TOP_LEFT, 10, 110);

  uptime_label = lv_label_create(parent);
  lv_label_set_text(uptime_label, "Uptime: 0 s");
  lv_obj_set_style_text_color(uptime_label, lv_color_white(), 0);
  lv_obj_align(uptime_label, LV_ALIGN_TOP_LEFT, 10, 145);
}

void screen_debug_update_uptime(uint32_t seconds) {
  if (uptime_label == NULL) {
    return;
  }
  lv_label_set_text_fmt(uptime_label, "Uptime: %lu s", (unsigned long)seconds);
}
