#include "ui_demo.h"
#include "lvgl.h"
#include "fonts/font_cyrillic_16.h"

// Этот файл не знает, где именно он рисуется — на SDL-окне в PC-симуляторе
// или на реальном TFT_eSPI/ILI9341 через LVGL. Поэтому здесь нет ничего
// специфичного для железа (GPIO, SPI и т.п.) — только LVGL-виджеты.

static lv_obj_t *uptime_label;

void ui_demo_create(void) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *frame = lv_obj_create(scr);
  lv_obj_remove_style_all(frame);
  lv_obj_set_size(frame, lv_pct(100), lv_pct(100));
  lv_obj_set_style_border_width(frame, 2, 0);
  lv_obj_set_style_border_color(frame, lv_color_white(), 0);
  lv_obj_center(frame);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "ESP32-2432S028");
  lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 20);

  lv_obj_t *hello = lv_label_create(scr);
  lv_label_set_text(hello, "HELLO");
  lv_obj_set_style_text_color(hello, lv_color_hex(0xFFFF00), 0);
  lv_obj_align(hello, LV_ALIGN_TOP_LEFT, 20, 60);

  // Кириллица — обычные встроенные шрифты LVGL (Montserrat и т.п.) её не
  // содержат, поэтому явно задаём кастомный шрифт (lib/ui_demo/fonts/).
  lv_obj_t *name = lv_label_create(scr);
  lv_label_set_text(name, "Сашааа");
  lv_obj_set_style_text_color(name, lv_color_hex(0xFFFF00), 0);
  lv_obj_set_style_text_font(name, &font_cyrillic_16, 0);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 20, 95);

  lv_obj_t *ok = lv_label_create(scr);
  lv_label_set_text(ok, "DISPLAY OK");
  lv_obj_set_style_text_color(ok, lv_color_hex(0x00FFFF), 0);
  lv_obj_align(ok, LV_ALIGN_TOP_LEFT, 20, 135);

  uptime_label = lv_label_create(scr);
  lv_label_set_text(uptime_label, "Uptime: 0 s");
  lv_obj_set_style_text_color(uptime_label, lv_color_white(), 0);
  lv_obj_align(uptime_label, LV_ALIGN_TOP_LEFT, 20, 175);
}

void ui_demo_update_uptime(uint32_t seconds) {
  if (uptime_label == NULL) {
    return;
  }
  lv_label_set_text_fmt(uptime_label, "Uptime: %lu s", (unsigned long)seconds);
}
