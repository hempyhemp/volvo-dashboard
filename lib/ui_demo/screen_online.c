#include "screen_online.h"
#include "images/img_cat_bg.h"
#include <stdbool.h>
#include <stdlib.h>

// ============================================================
// Вкладка ONLINE — черновик будущей приборки.
// Все параметры сейчас МОКОВЫЕ (случайное блуждание в реалистичных
// пределах) — реального обмена с ЭБУ по K-Line ещё нет, это только
// прикидка внешнего вида и анимаций.
//
// Иерархия важности параметров (по месту на экране и типу индикатора):
//   - главный, по центру, дуга большего размера: BOOST (давление турбины)
//   - второстепенные, дуги поменьше:  RPM, AIR TEMP
//   - третьестепенные, просто цифры: COOLANT, VOLT, OIL P, OIL T, IGN
// ============================================================

typedef enum {
  KIND_ARC,
  KIND_LABEL,
} indicator_kind_t;

typedef struct {
  indicator_kind_t kind;
  lv_obj_t *widget;      // lv_arc; NULL для KIND_LABEL
  lv_obj_t *value_label; // текст значения (внутри дуги/бара или сам по себе)
  int32_t current, min, max, step;
  const char *unit;
  bool scale10; // true → реальное значение = current/10 (для дробных величин)
} param_t;

#define PARAM_COUNT 8
enum { P_BOOST, P_RPM, P_AIR_T, P_COOLANT, P_VOLT, P_OIL_P, P_OIL_T, P_IGN };

static param_t params[PARAM_COUNT];

static void format_value(param_t *p, char *buf, size_t buf_size) {
  if (p->scale10) {
    lv_snprintf(buf, buf_size, "%ld.%ld%s", (long)(p->current / 10),
                (long)(p->current % 10), p->unit);
  } else {
    lv_snprintf(buf, buf_size, "%ld%s", (long)p->current, p->unit);
  }
}

static void anim_arc_exec_cb(void *var, int32_t v) {
  lv_arc_set_value((lv_obj_t *)var, v);
}

static void apply_new_value(param_t *p, int32_t new_value) {
  switch (p->kind) {
    case KIND_ARC: {
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, p->widget);
      lv_anim_set_values(&a, p->current, new_value);
      lv_anim_set_duration(&a, 450);
      lv_anim_set_exec_cb(&a, anim_arc_exec_cb);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
      lv_anim_start(&a);
      break;
    }
    case KIND_LABEL:
      // Третьестепенные параметры — без анимации, просто меняем текст.
      break;
  }

  p->current = new_value;

  char buf[24];
  format_value(p, buf, sizeof(buf));
  lv_label_set_text(p->value_label, buf);
}

static void mock_update_timer_cb(lv_timer_t *timer) {
  (void)timer;
  for (int i = 0; i < PARAM_COUNT; i++) {
    param_t *p = &params[i];
    int32_t delta = (rand() % (2 * p->step + 1)) - p->step;
    int32_t next = p->current + delta;
    if (next < p->min) next = p->min;
    if (next > p->max) next = p->max;
    apply_new_value(p, next);
  }
}

// --- Главный/второстепенный индикатор: дуга ---
static void make_arc(param_t *p, lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                      lv_coord_t size, lv_color_t color, int arc_width,
                      const lv_font_t *value_font) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, p->min, p->max);
  lv_arc_set_value(arc, p->current);
  lv_obj_set_size(arc, size, size);
  lv_obj_set_pos(arc, x, y);

  lv_obj_set_style_arc_width(arc, arc_width, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x24242A), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, arc_width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

  lv_obj_t *value_label = lv_label_create(arc);
  if (value_font) {
    lv_obj_set_style_text_font(value_label, value_font, 0);
  }
  lv_obj_set_style_text_color(value_label, lv_color_white(), 0);
  lv_obj_center(value_label);

  p->widget = arc;
  p->value_label = value_label;
}

// --- Третьестепенный параметр: просто число, без графики ---
static void make_label(param_t *p, lv_obj_t *parent, const char *name,
                        lv_coord_t x, lv_coord_t y, lv_coord_t w) {
  lv_obj_t *value_label = lv_label_create(parent);
  lv_obj_set_width(value_label, w);
  lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(value_label, lv_color_white(), 0);
  lv_obj_set_pos(value_label, x, y);

  lv_obj_t *name_label = lv_label_create(parent);
  lv_label_set_text(name_label, name); // <-- забытый вызов, отсюда была надпись "Text"
  lv_obj_set_width(name_label, w);
  lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(name_label, lv_color_hex(0x8C8C97), 0);
  lv_obj_align_to(name_label, value_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

  p->widget = NULL;
  p->value_label = value_label;
}

void screen_online_create(lv_obj_t *parent) {
  lv_obj_set_style_pad_all(parent, 0, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_TRANSP, 0);

  // Фон — фото котика (assets/fon.jpg -> lib/ui_demo/images/img_cat_bg.*,
  // сгенерировано скриптом-конвертером в RGB565). Поверх — полупрозрачная
  // тёмная плашка, чтобы цифры было видно на любом фото.
  lv_obj_t *bg_img = lv_image_create(parent);
  lv_image_set_src(bg_img, &img_cat_bg);
  lv_obj_set_pos(bg_img, 0, 0);

  lv_obj_t *overlay = lv_obj_create(parent);
  lv_obj_remove_style_all(overlay);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(overlay, 110, 0); // ~43% затемнение

  srand((unsigned int)lv_tick_get());

  params[P_BOOST] = (param_t){KIND_ARC, NULL, NULL, 4, 0, 20, 2, "bar", true};
  params[P_RPM] = (param_t){KIND_ARC, NULL, NULL, 900, 0, 7000, 250, "", false};
  params[P_AIR_T] = (param_t){KIND_ARC, NULL, NULL, 22, 0, 60, 2, "C", false};
  params[P_COOLANT] = (param_t){KIND_LABEL, NULL, NULL, 88, 60, 115, 2, "C", false};
  params[P_VOLT] = (param_t){KIND_LABEL, NULL, NULL, 138, 110, 148, 2, "V", true};
  params[P_OIL_P] = (param_t){KIND_LABEL, NULL, NULL, 28, 5, 60, 3, "bar", true};
  params[P_OIL_T] = (param_t){KIND_LABEL, NULL, NULL, 95, 60, 140, 2, "C", false};
  params[P_IGN] = (param_t){KIND_LABEL, NULL, NULL, 12, -5, 35, 3, "deg", false};

  // --- Главный параметр: BOOST, дуга по центру ---
  make_arc(&params[P_BOOST], parent, 100, 4, 120, lv_color_hex(0x4CA6FF), 10,
           NULL);
  lv_obj_t *boost_name = lv_label_create(parent);
  lv_label_set_text(boost_name, "BOOST");
  lv_obj_set_style_text_color(boost_name, lv_color_hex(0xCFCFE0), 0);
  lv_obj_align_to(boost_name, params[P_BOOST].widget, LV_ALIGN_OUT_BOTTOM_MID,
                   0, 2);

  // --- Второстепенные: RPM слева, AIR TEMP справа — дуги поменьше,
  // симметрично по бокам от BOOST ---
  make_arc(&params[P_RPM], parent, 8, 22, 84, lv_color_hex(0xFF4C4C), 7, NULL);
  lv_obj_t *rpm_name = lv_label_create(parent);
  lv_label_set_text(rpm_name, "RPM");
  lv_obj_set_style_text_color(rpm_name, lv_color_hex(0xCFCFE0), 0);
  lv_obj_align_to(rpm_name, params[P_RPM].widget, LV_ALIGN_OUT_BOTTOM_MID, 0,
                   2);

  make_arc(&params[P_AIR_T], parent, 228, 22, 84, lv_color_hex(0x66CCFF), 7,
            NULL);
  lv_obj_t *air_t_name = lv_label_create(parent);
  lv_label_set_text(air_t_name, "AIR T");
  lv_obj_set_style_text_color(air_t_name, lv_color_hex(0xCFCFE0), 0);
  lv_obj_align_to(air_t_name, params[P_AIR_T].widget, LV_ALIGN_OUT_BOTTOM_MID,
                   0, 2);

  // --- Третьестепенные: просто числа, в ряд внизу ---
  const char *tertiary_names[] = {"COOLANT", "VOLT", "OIL P", "OIL T", "IGN"};
  int tertiary_idx[] = {P_COOLANT, P_VOLT, P_OIL_P, P_OIL_T, P_IGN};
  lv_coord_t col_w = 64;
  for (int i = 0; i < 5; i++) {
    make_label(&params[tertiary_idx[i]], parent, tertiary_names[i],
               i * col_w, 172, col_w);
  }

  // Проставляем стартовые тексты значений (после создания виджетов).
  for (int i = 0; i < PARAM_COUNT; i++) {
    char buf[24];
    format_value(&params[i], buf, sizeof(buf));
    lv_label_set_text(params[i].value_label, buf);
  }

  lv_timer_create(mock_update_timer_cb, 700, NULL);
}
