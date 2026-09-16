#ifndef SCREEN_DEBUG_H
#define SCREEN_DEBUG_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Создаёт содержимое вкладки DEBUG внутри parent (контейнер вкладки).
void screen_debug_create(lv_obj_t *parent);

// Регистрирует обработчик нажатия кнопки "K-Line: инициализация".
// screen_debug.c ничего не знает про UART/GPIO — на ESP32 колбэк
// регистрирует kline_test.cpp (см. src/kline_test.cpp), в PC-симуляторе
// колбэк не регистрируется и кнопка сообщает, что тест недоступен.
void screen_debug_set_kline_test_cb(void (*cb)(void));

// Обновляет строку результата K-Line теста/автоподключения. success
// влияет только на цвет. Числовые плитки (RPM/темп/дроссель/напряжение)
// screen_debug.c обновляет сам, читая kline_data.h — этот сеттер нужен
// только для статусных сообщений (подключение/ошибка/self-echo).
void screen_debug_set_kline_result(const char *text, bool success);

#ifdef __cplusplus
}
#endif

#endif
