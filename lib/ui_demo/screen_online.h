#ifndef SCREEN_ONLINE_H
#define SCREEN_ONLINE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Создаёт содержимое вкладки ONLINE (приборка) внутри parent.
// Данные пока полностью моковые (см. screen_online.c) — реальных
// параметров с ЭБУ по K-Line на этом этапе ещё нет.
void screen_online_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif
