#ifndef SCREEN_DEBUG_H
#define SCREEN_DEBUG_H

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Создаёт содержимое вкладки DEBUG внутри parent (контейнер вкладки).
void screen_debug_create(lv_obj_t *parent);

// Обновляет строку "Uptime: N s" на вкладке DEBUG.
void screen_debug_update_uptime(uint32_t seconds);

#ifdef __cplusplus
}
#endif

#endif
