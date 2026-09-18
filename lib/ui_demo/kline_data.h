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
  int32_t air_temp_c;    // температура воздуха на впуске (ДТВ), °C —
                         // читается отдельной командой ReadMemoryByAddress
                         // (SID 0x23) из XDATA 0xF885, в readData её нет
  bool air_temp_valid;   // true, если air_temp_c реально прочитан с ЭБУ

  // --- Параметры вкладки МОТОР ---
  int32_t map_kpa_x100;  // абсолютное давление во впуске, кПа×100 (2 знака)
                         // из XDATA 0xF9A0 (SID 0x23); ИОН показывает то же
  bool map_valid;        // true, если давление реально прочитано
  int32_t gbc;           // цикловое наполнение воздухом (GBC), сырое 16-бит
                         // из readData [36]-[37] (XDATA 0xF808)
  int32_t corr_cn;       // поправка ЦН (XDATA 0xF942), сырой байт, SID 0x23
  int32_t corr_coolant;  // коррекция ЦН по темп. ОЖ (0xF99C), сырой байт
                         // (128 = нейтраль ×1.00)
  int32_t corr_charge;   // коррекция ЦН по темп. заряда (0xF99D), сырой байт
  int32_t charge_temp_c; // температура заряда (0xF99E), °C (те же -40, что ДТВ)
  bool ext_valid;        // true, если доп. блок (SID 0x23) реально прочитан
} kline_data_t;

void kline_data_set(const kline_data_t *data);
void kline_data_get(kline_data_t *out);

#ifdef __cplusplus
}
#endif

#endif
