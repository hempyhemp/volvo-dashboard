#ifndef KLINE_DATA_H
#define KLINE_DATA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Общий, не привязанный к железу слой для передачи живых данных ЭБУ
// (Январь 5.1.61, K-Line/KWP2000) из src/kline_test.cpp (ESP32-only) в
// lib/ui_demo (общий с PC-симулятором). screen_online.c читает отсюда,
// ничего не зная про UART/GPIO. В PC-симуляторе kline_data_set() никогда
// не вызывается, поэтому connected всегда false и ONLINE-вкладка там
// работает как раньше — на моковых данных.
typedef struct {
  bool connected;
  int32_t rpm;
  int32_t coolant_c;
  int32_t throttle_pct;
  int32_t speed_kmh;
  float voltage;
  int32_t ign_deg;   // угол опережения зажигания, byte[28]*10/2/10 — из
                      // стороннего кода, ещё НЕ проверено на этом ЭБУ
  int32_t boost_raw; // "сырой" 16-бит из byte[34]/[35] — в реф-коде это
                      // MAF (расход воздуха), тут временно используется
                      // как кандидат на MAP/ДАД, единицы НЕ подтверждены
} kline_data_t;

void kline_data_set(const kline_data_t *data);
void kline_data_get(kline_data_t *out);

#ifdef __cplusplus
}
#endif

#endif
