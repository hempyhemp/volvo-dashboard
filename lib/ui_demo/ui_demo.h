#ifndef UI_DEMO_H
#define UI_DEMO_H

#include <stdint.h>

// ui_demo.c компилируется как обычный C-файл. Когда его подключают из
// C++ (main.cpp прошивки ESP32), без extern "C" компилятор ожидал бы
// C++-декорированные (mangled) имена функций и линковка падала бы с
// "undefined reference", хотя функции реально существуют в .a файле.
#ifdef __cplusplus
extern "C" {
#endif

// Создаёт интерфейс: сверху переключатель вкладок DEBUG / ONLINE.
void ui_demo_create(void);

// Обновляет строку "Uptime: N s" на вкладке DEBUG.
void ui_demo_update_uptime(uint32_t seconds);

#ifdef __cplusplus
}
#endif

#endif
