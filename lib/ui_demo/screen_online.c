#include "screen_online.h"
#include "images/img_cat_bg.h"
#include "kline_data.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================
// Вкладка ONLINE — черновик приборки.
//
// На реальном железе (сборка с ARDUINO) — только настоящие данные с
// K-Line: RPM/COOLANT/VOLT показываются, когда kline_data.connected,
// иначе "--"; остальные параметры (BOOST/AIR_T/OIL_P/POWER/IGN) — байты
// для них ещё не найдены (см. docs/KLINE.md), поэтому всегда "--".
// Никогда не подставляем случайные цифры вместо реальных показаний —
// на приборке это выглядело бы как настоящая телеметрия.
//
// В PC-симуляторе (сборка без ARDUINO) реального K-Line не будет никогда
// — там все параметры как и раньше моковые (случайное блуждание в
// реалистичных пределах), для разработки/просмотра интерфейса без платы.
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
  bool scale10;  // true → реальное значение = current/10 (1 знак после точки)
  bool scale100; // true → реальное значение = current/100 (2 знака, для буста)
  // --- очередь перерисовки дуг (см. arcs_pump) ---
  // ДОБАВЛЯТЬ ТОЛЬКО В КОНЕЦ: params[...] ниже заполняются ПОЗИЦИОННЫМИ
  // инициализаторами, и любое поле, вставленное в середину, молча сдвинет
  // все значения (строка единиц измерения уехала бы в числовое поле).
  int32_t pending;       // значение, которое ещё не отдано дуге
  bool dirty;            // true = дуга ждёт перерисовки
  int32_t shown;         // что реально стоит на дуге сейчас
} param_t;

#define PARAM_COUNT 8
// P_POWER занял место бывшего OIL_T: температуры масла у Января нет и
// не будет (нет такого входа), а мощность считается из расхода воздуха.
enum { P_BOOST, P_RPM, P_AIR_T, P_COOLANT, P_VOLT, P_OIL_P, P_POWER, P_IGN };

// Атмосферное давление — точка отсчёта манометрического давления наддува.
// БОЛЬШЕ НЕ КОНСТАНТА: kline_test.cpp запоминает реальное показание ДАД на
// неработающем моторе (RPM=0) и кладёт его в kd.baro_kpa_x100, поэтому на
// заглушенной машине буст ровно 0.00 бар на любой высоте и погоде.

static param_t params[PARAM_COUNT];

static void format_value(param_t *p, char *buf, size_t buf_size) {
  // Корректный знак для отрицательных дробных значений (например,
  // разрежение во впуске -0.70bar): без выноса минуса current=-70
  // печаталось бы как "0.-70". Работаем с модулем, знак отдельно.
  long v = (long)p->current;
  const char *sign = (v < 0) ? "-" : "";
  long av = (v < 0) ? -v : v;
  if (p->scale100) {
    lv_snprintf(buf, buf_size, "%s%ld.%02ld%s", sign, av / 100, av % 100,
                p->unit);
  } else if (p->scale10) {
    lv_snprintf(buf, buf_size, "%s%ld.%ld%s", sign, av / 10, av % 10, p->unit);
  } else {
    lv_snprintf(buf, buf_size, "%ld%s", v, p->unit);
  }
}

static void apply_new_value(param_t *p, int32_t new_value) {
  switch (p->kind) {
    case KIND_ARC: {
      // Дугу здесь НЕ трогаем — только ставим в очередь. Перерисовку
      // разносит по времени arcs_pump() ниже, по одной дуге за такт.
      //
      // Почему так (замеры 2026-09-19). Сначала убрали анимацию — помогло,
      // но всплески LVGL остались 55..70 мс. Счётчики вывода показали, что
      // в дисплей уходит всего около 30 тыс. пикселей в секунду и лишь 10 мс
      // на это тратится, то есть шина и DMA ни при чём. Считается: три дуги
      // вместе занимают около 28 500 пикселей, и они перерисовывались ОДНИМ
      // куском — отсюда и десятки миллисекунд в одном вызове. Отрисовка
      // сглаженной дуги стоит примерно 2 мкс на пиксель, и это не лечится
      // настройками, только уменьшением площади за раз.
      p->pending = new_value;
      p->dirty = true;
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

// Разносим перерисовку дуг по времени: за один такт обновляем НЕ БОЛЬШЕ
// ОДНОЙ дуги. Раньше все три перерисовывались в одном вызове — получался
// один провал на 55..70 мс, который глаз видит как рывок. По одной дуге за
// такт тот же объём работы разбивается на три коротких куска.
//
// Плюс мёртвая зона: дуга рисует 270 градусов на весь диапазон, поэтому
// изменение, которое не сдвигает её даже на градус, перерисовывать незачем.
// На холостых обороты постоянно дрожат (1120/1200/1680), и без этого дуга
// дёргалась бы впустую.
static void arcs_pump(void) {
  static int next = 0;
  for (int i = 0; i < PARAM_COUNT; i++) {
    param_t *p = &params[(next + i) % PARAM_COUNT];
    if (p->kind != KIND_ARC || !p->widget || !p->dirty) continue;
    int32_t span = p->max - p->min;
    if (span <= 0) span = 1;
    int32_t moved = (p->pending - p->shown) * 270 / span;
    if (moved < 0) moved = -moved;
    p->dirty = false;
    // Порог 3 градуса из 270, то есть около 1% шкалы — на глаз незаметно.
    // Замер с платы показал, почему 1 градуса мало: у дуги БУСТА диапазон
    // 300 сотых бара на 270 градусов, то есть один градус = 1.1 кПа, а
    // давление постоянно дрожит на ±1 кПа (F80C скачет 41/42/43). Дуга
    // перерисовывалась почти каждый такт впустую, по ~50 мс за раз.
    // Накопление не теряется: pending обновляется каждый такт, и как только
    // расхождение перевалит порог — дуга перерисуется.
    if (moved < 3) continue;
    lv_arc_set_value(p->widget, p->pending);
    p->shown = p->pending;
    next = (next + i + 1) % PARAM_COUNT;
    return;                              // ровно одна дуга за такт
  }
}

static void show_dash(param_t *p) {
  lv_label_set_text(p->value_label, "--");
  if (p->kind == KIND_ARC) {
    lv_arc_set_value(p->widget, p->min);
  }
  p->current = p->min;
}

static void show_volt(param_t *p, bool connected, float voltage) {
  if (!connected) {
    show_dash(p);
    return;
  }
  // Точное напряжение с 2 знаками после запятой — в обход scale10-
  // механизма format_value() (тот даёт только 1 знак, из-за чего
  // 11.95В отображалось как округлённые "12.0").
  p->current = (int32_t)(voltage * 100 + 0.5f);
  char buf[24];
  snprintf(buf, sizeof(buf), "%.2f%s", (double)voltage, p->unit);
  lv_label_set_text(p->value_label, buf);
}

// Корень вкладки. Таймеры LVGL тикают независимо от того, какая вкладка
// открыта, поэтому без этой проверки невидимый экран всё равно дёргал бы
// kline_data_get, переписывал подписи и ЗАПУСКАЛ АНИМАЦИИ дуг — самая
// дорогая отрисовка в проекте, и всё впустую (оптимизация 2026-09-19).
static lv_obj_t *s_tab_root = NULL;

#ifdef ARDUINO
// Реальное железо: показываем только то, что реально знаем с K-Line.
// Никогда не подставляем случайные "правдоподобные" цифры вместо
// отсутствующих данных — на приборке это выглядело бы как настоящие
// показания. RPM/COOLANT/VOLT — подтверждены. IGN и BOOST — байты
// НЕ подтверждены (кандидаты из стороннего кода, см. docs/KLINE.md),
// но уже подключены по просьбе пользователя для проверки на месте;
// формулы/масштаб могут поменяться. OIL_P — байтов нет и не будет
// вообще, всегда "--".
static void update_timer_cb(lv_timer_t *timer) {
  if (s_tab_root && !lv_obj_is_visible(s_tab_root)) return;
  arcs_pump();
  (void)timer;

  kline_data_t kd;
  kline_data_get(&kd);

  if (kd.connected) {
    apply_new_value(&params[P_RPM], kd.rpm);
    apply_new_value(&params[P_COOLANT], kd.coolant_c);
    apply_new_value(&params[P_IGN], kd.ign_deg_x10);
    // BOOST — МАНОМЕТРИЧЕСКОЕ давление (относительно атмосферы) из абс. MAP.
    // MAP считает kline_test.cpp из байта F9A0 по калибровке ДАД прошивки
    // (12.5 + 241·F9A0/255 кПа), опора baro — тот же ДАД на заглушенном
    // моторе. Сотые доли бара = (MAP - baro) в кПа:
    //   ХХ ~45 кПа при баро 95  -> -50 -> "-0.50bar" (разрежение)
    //   зажигание, мотор стоит  ->   0 -> "0.00bar"
    //   наддув ~168 кПа         ->  73 -> "0.73bar"
    if (kd.map_valid) {
      int32_t boost_cbar = (kd.map_kpa_x100 - kd.baro_kpa_x100) / 100;
      apply_new_value(&params[P_BOOST], boost_cbar);
    } else {
      show_dash(&params[P_BOOST]);
    }
    // AIR_T (ДТВ) читается отдельной командой (SID 0x23, XDATA 0xF885),
    // приходит не каждый цикл — показываем, только когда реально прочитан.
    if (kd.air_temp_valid) {
      apply_new_value(&params[P_AIR_T], kd.air_temp_c);
    } else {
      show_dash(&params[P_AIR_T]);
    }
    // МОЩНОСТЬ — оценка текущей отдачи по расходу воздуха. Пока мотор не
    // крутится, воздуха нет и показывать нечего.
    if (kd.rpm > 0) {
      apply_new_value(&params[P_POWER], kd.power_hp);
    } else {
      show_dash(&params[P_POWER]);
    }
  } else {
    show_dash(&params[P_RPM]);
    show_dash(&params[P_COOLANT]);
    show_dash(&params[P_IGN]);
    show_dash(&params[P_BOOST]);
    show_dash(&params[P_AIR_T]);
    show_dash(&params[P_POWER]);
  }
  show_volt(&params[P_VOLT], kd.connected, kd.voltage);
}
#else
// PC-симулятор: реального K-Line тут никогда не будет — оставляем
// моковое случайное блуждание по всем параметрам, как и раньше, для
// разработки/просмотра интерфейса без платы.
static void update_timer_cb(lv_timer_t *timer) {
  if (s_tab_root && !lv_obj_is_visible(s_tab_root)) return;
  arcs_pump();
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
#endif

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
  p->shown = p->current;   // что реально стоит на дуге (для мёртвой зоны)
  p->pending = p->current;
  p->dirty = false;
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
  s_tab_root = parent;
  lv_obj_set_style_pad_all(parent, 0, 0);

  // Фон — фото котика, временно закомментировано, вернули градиент (см.
  // ниже). Раскомментировать, когда снова понадобится:
  // lv_obj_t *bg_img = lv_image_create(parent);
  // lv_image_set_src(bg_img, &img_cat_bg);
  // lv_obj_set_pos(bg_img, 0, 0);
  //
  // lv_obj_t *overlay = lv_obj_create(parent);
  // lv_obj_remove_style_all(overlay);
  // lv_obj_remove_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  // lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
  // lv_obj_set_pos(overlay, 0, 0);
  // lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  // lv_obj_set_style_bg_opa(overlay, 110, 0); // ~43% затемнение

  // Фон — тёмный вертикальный градиент (тот, что был до фото котика).
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x14141C), 0);
  lv_obj_set_style_bg_grad_color(parent, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);

  srand((unsigned int)lv_tick_get());

  // BOOST — манометрическое давление наддува/разрежения в бар с точностью
  // до сотых (scale100 → current в сотых долях бара). Диапазон дуги
  // -100..200 = -1.00..+2.00 бар: слева разрежение (ХХ ~ -0.70 бар),
  // справа наддув. Физический источник — абсолютное давление MAP в кПа
  // (1 кПа = 0.01 бар, см. update_timer_cb).
  params[P_BOOST] =
      (param_t){KIND_ARC, NULL, NULL, 0, -100, 200, 5, "bar", false, true};
  params[P_RPM] = (param_t){KIND_ARC, NULL, NULL, 900, 0, 7000, 250, "", false};
  params[P_AIR_T] =
      (param_t){KIND_ARC, NULL, NULL, 22, -20, 90, 2, "C", false, false};
  params[P_COOLANT] = (param_t){KIND_LABEL, NULL, NULL, 88, 60, 115, 2, "C", false};
  params[P_VOLT] = (param_t){KIND_LABEL, NULL, NULL, 138, 110, 148, 2, "V", true};
  params[P_OIL_P] = (param_t){KIND_LABEL, NULL, NULL, 28, 5, 60, 3, "bar", true};
  params[P_POWER] = (param_t){KIND_LABEL, NULL, NULL, 0, 0, 400, 5, "", false};
  // УОЗ хранится ×10 (шаг у ЭБУ — полградуса), поэтому scale10 = true.
  params[P_IGN] = (param_t){KIND_LABEL, NULL, NULL, 120, -50, 400, 5, "deg", true};

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
  const char *tertiary_names[] = {"WATER", "VOLT", "OIL P", "hp", "IGN"};
  int tertiary_idx[] = {P_COOLANT, P_VOLT, P_OIL_P, P_POWER, P_IGN};
  lv_coord_t col_w = 64;
  for (int i = 0; i < 5; i++) {
    make_label(&params[tertiary_idx[i]], parent, tertiary_names[i],
               i * col_w, 172, col_w);
  }

  // Проставляем стартовые тексты значений (после создания виджетов).
#ifdef ARDUINO
  // Реальное железо: до первого успешного опроса K-Line данных ещё нет —
  // сразу показываем "--", а не моковые стартовые числа.
  for (int i = 0; i < PARAM_COUNT; i++) {
    show_dash(&params[i]);
  }
#else
  for (int i = 0; i < PARAM_COUNT; i++) {
    char buf[24];
    format_value(&params[i], buf, sizeof(buf));
    lv_label_set_text(params[i].value_label, buf);
  }
#endif

  // 120 мс: за такт перерисовывается максимум ОДНА дуга (см. arcs_pump),
  // поэтому частый такт не страшен — он не складывает работу в один кусок,
  // а наоборот размазывает её. Каждая дуга при этом обновляется примерно
  // раз в треть секунды, что для приборки более чем живо.
  lv_timer_create(update_timer_cb, 120, NULL);
}
