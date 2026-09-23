#include "screen_engine.h"
#include "kline_data.h"
#include "fonts/font_cyrillic_16.h"
#include <stdio.h>

// Вкладка МОТОР — плитки, которых нет на ONLINE.
// Источники (см. docs/KLINE.md):
//   Абс. давление (MAP)  — SID 0x23, XDATA 0xF9A0 (один байт),
//                          P = 12.5 + 241·F9A0/255 кПа (калибровка прошивки)
//   Буст                 — MAP минус атмосфера, запомненная на RPM=0
//   Расход воздуха       — кадр 0x0F, XDATA 0xF808 (GBC), десятые кг/ч
//   Расход топлива л/ч   — РАСЧЁТ: воздух / AFR / плотность бензина.
//                          В ЭБУ не хранится.

typedef struct {
  lv_obj_t *value;
} tile_t;

static tile_t t_map, t_boost, t_lph, t_kgh, t_gbc, t_air;

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

static void set_int(tile_t *t, bool ok, long v, const char *suffix) {
  if (!ok) {
    lv_label_set_text(t->value, "--");
    return;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%ld%s", v, suffix);
  lv_label_set_text(t->value, buf);
}

// Показать значение×100 как X.XX (для давления в кПа с сотыми).
static void set_x100(tile_t *t, bool ok, long v_x100, const char *suffix) {
  if (!ok) {
    lv_label_set_text(t->value, "--");
    return;
  }
  const char *sign = (v_x100 < 0) ? "-" : "";
  long a = (v_x100 < 0) ? -v_x100 : v_x100;
  char buf[24];
  snprintf(buf, sizeof(buf), "%s%ld.%02ld%s", sign, a / 100, a % 100, suffix);
  lv_label_set_text(t->value, buf);
}

// Показать значение×10 как X.X с суффиксом (для расхода).
static void set_x10(tile_t *t, bool ok, long v_x10, const char *suffix) {
  if (!ok) {
    lv_label_set_text(t->value, "--");
    return;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%ld.%ld%s", v_x10 / 10, v_x10 % 10, suffix);
  lv_label_set_text(t->value, buf);
}

// Корень вкладки. Таймеры LVGL тикают независимо от того, какая вкладка
// открыта, поэтому без этой проверки экран, которого не видно, всё равно
// дёргал бы kline_data_get и переписывал подписи — впустую. Проверяем
// видимость и выходим сразу (оптимизация отзывчивости 2026-09-19).
static lv_obj_t *s_tab_root = NULL;

static void update_cb(lv_timer_t *timer) {
  if (s_tab_root && !lv_obj_is_visible(s_tab_root)) return;
  (void)timer;
  kline_data_t kd;
  kline_data_get(&kd);

  // Абс. давление (MAP) — 0xF9A0, кПа с сотыми.
  set_x100(&t_map, kd.connected && kd.map_valid, (long)kd.map_kpa_x100, "");
  // Буст — манометрическое давление в барах (MAP минус атмосфера).
  set_x100(&t_boost, kd.connected && kd.map_valid,
           (long)(kd.map_kpa_x100 - kd.baro_kpa_x100), "");
  // Расход топлива л/ч (расчёт) — при работающем моторе.
  set_x10(&t_lph, kd.connected && kd.rpm > 0, (long)kd.fuel_lph_x10, "");
  // Расход воздуха кг/ч — GBC в десятых кг/ч.
  set_x10(&t_kgh, kd.connected, (long)kd.air_kgh_x10, "");
  // GBC (сырое) — оставлено для сверки с ИОН.
  set_int(&t_gbc, kd.connected, (long)kd.gbc, "");
  // Темп. воздуха (ДТВ) — из кадра, даром.
  set_int(&t_air, kd.connected && kd.air_temp_valid, (long)kd.air_temp_c, " C");
}

void screen_engine_create(lv_obj_t *parent) {
  s_tab_root = parent;
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(parent, 0, 0);

  // 2 колонки x 3 ряда плиток 150x58.
  t_map = make_tile(parent, 6, 6, 150, 58, "АБС.ДАВЛ, kPa");
  t_boost = make_tile(parent, 164, 6, 150, 58, "БУСТ, бар");
  t_lph = make_tile(parent, 6, 68, 150, 58, "РАСХОД, л/ч");
  t_kgh = make_tile(parent, 164, 68, 150, 58, "ВОЗДУХ, кг/ч");
  t_gbc = make_tile(parent, 6, 130, 150, 58, "GBC (сырое)");
  t_air = make_tile(parent, 164, 130, 150, 58, "T ВОЗД, C");

  // lv_obj_t *note = lv_label_create(parent);
  // lv_label_set_text(note, "Расход — расчёт из воздуха ЭБУ; сверить с ИОН");
  // lv_obj_set_style_text_font(note, &font_cyrillic_16, 0);
  // lv_obj_set_style_text_color(note, lv_color_hex(0x6C6C77), 0);
  // lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 8, -4);

  lv_timer_create(update_cb, 250, NULL);
}
