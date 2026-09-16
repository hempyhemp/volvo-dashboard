#include "ui_demo.h"
#include "lvgl.h"
#include "screen_debug.h"
#include "screen_online.h"

// Этот файл не знает, где именно он рисуется — на SDL-окне в PC-симуляторе
// или на реальном TFT_eSPI/ILI9341 через LVGL. Поэтому здесь нет ничего
// специфичного для железа (GPIO, SPI и т.п.) — только LVGL-виджеты.
//
// Здесь только переключение вкладок; содержимое каждой вкладки — в
// screen_debug.c (техническая проверка) и screen_online.c (приборка).

// Тёмная тема для панели вкладок: по умолчанию LVGL рисует её светлой,
// что не подходит для приборки. Красим саму панель и каждую кнопку
// вручную (у lv_tabview кнопки — обычные lv_button, отдельного API для
// их темы нет).
static void style_tab_bar_dark(lv_obj_t *tv) {
  lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tv);

  lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x121218), 0);
  lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tab_bar, 0, 0);

  uint32_t btn_count = lv_obj_get_child_count(tab_bar);
  for (uint32_t i = 0; i < btn_count; i++) {
    lv_obj_t *btn = lv_obj_get_child(tab_bar, i);

    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);

    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C1C24), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x8C8C97), 0);

    // Активная вкладка — акцентный цвет вместо стандартного светлого.
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C6BFF), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_CHECKED);
  }
}

void ui_demo_create(void) {
  lv_obj_t *tv = lv_tabview_create(lv_screen_active());
  lv_tabview_set_tab_bar_position(tv, LV_DIR_TOP);
  lv_tabview_set_tab_bar_size(tv, 32);

  lv_obj_t *tab_debug = lv_tabview_add_tab(tv, "DEBUG");
  lv_obj_t *tab_online = lv_tabview_add_tab(tv, "ONLINE");

  style_tab_bar_dark(tv);

  screen_debug_create(tab_debug);
  screen_online_create(tab_online);
}
