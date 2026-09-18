#include "screen_engine.h"
#include "kline_data.h"
#include "fonts/font_cyrillic_16.h"
#include <stdio.h>

// Вкладка МОТОР — 6 плиток параметров двигателя, которых нет на ONLINE.
// Источники (см. docs/KLINE.md):
//   Абс. давление (MAP)      — readData, XDATA 0xF841, кПа (подтверждено)
//   Наполнение (GBC)         — readData, XDATA 0xF808, сырое 16-бит
//   Темп. заряда             — SID 0x23, XDATA 0xF99E, °C
//   Поправка ЦН              — SID 0x23, XDATA 0xF942, сырой байт
//   Коррекция по ОЖ          — SID 0x23, XDATA 0xF99C, сырой байт
//   Коррекция по заряду      — SID 0x23, XDATA 0xF99D, сырой байт
// Расход л/ч/л100 в прошивке НЕ хранится (диагностики его вычисляют) —
// внизу вкладки поясняющая строка.

typedef struct {
  lv_obj_t *value;
} tile_t;

static tile_t t_map, t_gbc, t_tcharge, t_corrcn, t_corroj, t_corrchg;

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

// Коррекция как отклонение от нейтрали (raw 128 = ×1.00 = 0%).
static void set_corr_pct(tile_t *t, bool ok, long raw) {
  if (!ok) {
    lv_label_set_text(t->value, "--");
    return;
  }
  long pct = (raw - 128) * 100 / 128;
  char buf[24];
  snprintf(buf, sizeof(buf), "%+ld%%", pct);
  lv_label_set_text(t->value, buf);
}

static void update_cb(lv_timer_t *timer) {
  (void)timer;
  kline_data_t kd;
  kline_data_get(&kd);

  // Абс. давление (MAP) — из 0xF9A0 (SID 0x23), кПа с сотыми.
  set_x100(&t_map, kd.connected && kd.map_valid, (long)kd.map_kpa_x100, "");
  // GBC (наполнение) — из readData, валидно при connected.
  set_int(&t_gbc, kd.connected, (long)kd.gbc, "");
  // Остальное — из SID 0x23, валидно при ext_valid.
  bool ext = kd.connected && kd.ext_valid;
  set_int(&t_tcharge, ext, (long)kd.charge_temp_c, " C");
  set_int(&t_corrcn, ext, (long)kd.corr_cn, "");        // поправка ЦН, raw
  set_corr_pct(&t_corroj, ext, (long)kd.corr_coolant);  // % от нейтрали
  set_corr_pct(&t_corrchg, ext, (long)kd.corr_charge);
}

void screen_engine_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(parent, 0, 0);

  // 2 колонки x 3 ряда плиток 150x58.
  t_map = make_tile(parent, 6, 6, 150, 58, "АБС.ДАВЛ, kPa");
  t_gbc = make_tile(parent, 164, 6, 150, 58, "НАПОЛН. GBC");
  t_tcharge = make_tile(parent, 6, 68, 150, 58, "T ЗАРЯДА, C");
  t_corrcn = make_tile(parent, 164, 68, 150, 58, "ПОПРАВКА ЦН");
  t_corroj = make_tile(parent, 6, 130, 150, 58, "КОРР. ОЖ");
  t_corrchg = make_tile(parent, 164, 130, 150, 58, "КОРР. ЗАРЯД");

  lv_obj_t *note = lv_label_create(parent);
  lv_label_set_text(note, "Расход л/ч в ЭБУ не хранится (нужна калибровка GBC)");
  lv_obj_set_style_text_font(note, &font_cyrillic_16, 0);
  lv_obj_set_style_text_color(note, lv_color_hex(0x6C6C77), 0);
  lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 8, -4);

  lv_timer_create(update_cb, 250, NULL);
}
