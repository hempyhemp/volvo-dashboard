#include "screen_debug.h"
#include "kline_data.h"
#include "fonts/font_cyrillic_16.h"
#include <stdio.h>

// Вкладка DEBUG: статус K-Line и живые данные с ЭБУ Январь 5.1.61.
// Значения (RPM/темп/дроссель/напряжение) читаются напрямую из общего
// hardware-agnostic слоя kline_data.h — так же, как это делает
// screen_online.c. Строка статуса (kline_result_label) двигается
// отдельно: её обновляет src/kline_test.cpp (ESP32-only) через
// screen_debug_set_kline_result — там текст про попытки подключения и
// ручную диагностику (self-echo и т.п.), которых в kline_data_t нет.

static lv_obj_t *kline_result_label;
static void (*kline_test_cb)(void) = NULL;

typedef struct {
  lv_obj_t *value;
} tile_t;

static tile_t tile_rpm, tile_temp, tile_throttle, tile_volt;

static void kline_btn_event_cb(lv_event_t *e) {
  (void)e;
  if (kline_test_cb != NULL) {
    kline_test_cb();
  } else {
    // PC-симулятор: ESP32-only модуль kline_test.cpp сюда не слинкован.
    screen_debug_set_kline_result("K-Line: недоступно в симуляторе", false);
  }
}

static tile_t make_tile(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, lv_coord_t h, const char *caption) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C24), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 6, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *value = lv_label_create(card);
  lv_label_set_text(value, "--");
  lv_obj_set_style_text_font(value, &font_cyrillic_16, 0);
  lv_obj_set_style_text_color(value, lv_color_hex(0x66CCFF), 0);
  lv_obj_align(value, LV_ALIGN_TOP_LEFT, 8, 4);

  lv_obj_t *label = lv_label_create(card);
  lv_label_set_text(label, caption);
  lv_obj_set_style_text_font(label, &font_cyrillic_16, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0x8C8C97), 0);
  lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 8, -4);

  tile_t t = {value};
  return t;
}

static void kline_tiles_update_cb(lv_timer_t *timer) {
  (void)timer;
  kline_data_t kd;
  kline_data_get(&kd);

  if (!kd.connected) {
    lv_label_set_text(tile_rpm.value, "--");
    lv_label_set_text(tile_temp.value, "--");
    lv_label_set_text(tile_throttle.value, "--");
    lv_label_set_text(tile_volt.value, "--");
    return;
  }

  // lv_label_set_text_fmt() зовёт lv_snprintf(), а в этом проекте LVGL
  // собран с LV_USE_FLOAT=0 — её "%f" не работает (даёт мусор вроде
  // "fV"). Поэтому напряжение форматируем обычным C snprintf() и отдаём
  // в LVGL уже готовую строку.
  lv_label_set_text_fmt(tile_rpm.value, "%ld", (long)kd.rpm);
  lv_label_set_text_fmt(tile_temp.value, "%ldC", (long)kd.coolant_c);
  lv_label_set_text_fmt(tile_throttle.value, "%ld%%", (long)kd.throttle_pct);

  char volt_buf[16];
  snprintf(volt_buf, sizeof(volt_buf), "%.2fV", (double)kd.voltage);
  lv_label_set_text(tile_volt.value, volt_buf);
}

void screen_debug_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(parent, 0, 0);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "K-Line - ЭБУ Январь 5.1.61");
  lv_obj_set_style_text_font(title, &font_cyrillic_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x8C8C97), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 6);

  kline_result_label = lv_label_create(parent);
  lv_label_set_text(kline_result_label, "K-Line: жду ЭБУ (авто)...");
  lv_obj_set_style_text_font(kline_result_label, &font_cyrillic_16, 0);
  lv_obj_set_style_text_color(kline_result_label, lv_color_hex(0xCFCFE0), 0);
  lv_obj_align(kline_result_label, LV_ALIGN_TOP_LEFT, 10, 26);

  // Плитки живых данных — 2x2, значения крупным моно-подобным шрифтом.
  tile_rpm = make_tile(parent, 8, 52, 150, 58, "ОБОРОТЫ, RPM");
  tile_temp = make_tile(parent, 162, 52, 150, 58, "ТЕМП. ОЖ, C");
  tile_throttle = make_tile(parent, 8, 114, 150, 58, "ДРОССЕЛЬ, %");
  tile_volt = make_tile(parent, 162, 114, 150, 58, "НАПРЯЖЕНИЕ, В");

  lv_timer_create(kline_tiles_update_cb, 200, NULL);

  lv_obj_t *kline_btn = lv_button_create(parent);
  lv_obj_set_pos(kline_btn, 8, 176);
  lv_obj_set_size(kline_btn, 304, 26);
  lv_obj_set_style_bg_color(kline_btn, lv_color_hex(0x2C6BFF), 0);
  lv_obj_add_event_cb(kline_btn, kline_btn_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *kline_btn_label = lv_label_create(kline_btn);
  lv_label_set_text(kline_btn_label, "K-Line: переподключить");
  lv_obj_set_style_text_font(kline_btn_label, &font_cyrillic_16, 0);
  lv_obj_center(kline_btn_label);
}

void screen_debug_set_kline_test_cb(void (*cb)(void)) {
  kline_test_cb = cb;
}

void screen_debug_set_kline_result(const char *text, bool success) {
  if (kline_result_label == NULL) {
    return;
  }
  lv_label_set_text(kline_result_label, text);
  lv_obj_set_style_text_color(
      kline_result_label,
      success ? lv_color_hex(0x00FF00) : lv_color_hex(0xCFCFE0), 0);
}
