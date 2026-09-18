#ifndef SCREEN_ENGINE_H
#define SCREEN_ENGINE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Вкладка МОТОР: параметры наполнения/коррекций ЭБУ Январь (TRS251),
// которых нет на ONLINE — абс. давление, цикловое наполнение (GBC),
// температура заряда, поправка ЦН и коррекции по ОЖ/заряду. Часть
// читается прямо из readData, часть — отдельной командой SID 0x23
// (см. src/kline_test.cpp). Всё берётся из общего слоя kline_data.h.
void screen_engine_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif
