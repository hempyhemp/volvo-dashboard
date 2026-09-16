#ifndef KLINE_TEST_H
#define KLINE_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Регистрирует обработчик кнопки "K-Line: инициализация" на вкладке DEBUG
// (см. screen_debug_set_kline_test_cb). Вызывать один раз из setup(),
// после ui_demo_create().
void kline_test_register(void);

// Непрерывный опрос ЭБУ после успешного подключения (кнопкой). Ничего не
// делает, пока подключения не было. Вызывать из loop() на каждой
// итерации, передавая millis().
void kline_test_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
