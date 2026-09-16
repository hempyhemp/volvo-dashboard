#ifndef KLINE_TEST_H
#define KLINE_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

// Регистрирует обработчик кнопки "K-Line: инициализация" на вкладке DEBUG
// (см. screen_debug_set_kline_test_cb). Вызывать один раз из setup(),
// после ui_demo_create().
void kline_test_register(void);

#ifdef __cplusplus
}
#endif

#endif
