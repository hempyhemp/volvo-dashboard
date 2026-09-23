#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include "kline_data.h"

#ifdef __cplusplus
extern "C" {
#endif

// Разовая проверка слота microSD при старте. Пробует поднять карту на
// стандартных для этой платы пинах и печатает результат в Serial, после чего
// ОСВОБОЖДАЕТ шину. Нужна, чтобы выяснить реальную разводку слота, не рискуя
// тачем. Возвращает true, если карта ответила.
bool sd_logger_probe(void);

// Отдать шину обратно после sd_logger_probe().
void sd_logger_probe_release(void);

// Поднять логгер: инициализировать карту, открыть файл, запустить пишущую
// задачу на ВТОРОМ ядре. false = карты нет или файл не открылся; в этом
// случае sd_logger_write() становится пустышкой и ничего не стоит.
bool sd_logger_begin(void);

// Положить очередной замер в очередь. Зовётся из главного цикла после
// каждого удачного кадра. НИКОГДА не блокирует: форматирует строку в стек и
// отдаёт её в буфер с нулевым таймаутом. Если буфер переполнен — строка
// теряется, и это видно в счётчике потерь.
void sd_logger_write(const kline_data_t *kd, uint32_t now_ms);

// Счётчики для строки [SD] в логе: записано строк, потеряно строк,
// записано байт, сколько миллисекунд суммарно заняли записи на карту.
void sd_logger_stats(uint32_t *lines, uint32_t *dropped, uint32_t *bytes,
                     uint32_t *write_ms);

// true, если логгер реально работает (карта поднялась и файл открыт).
bool sd_logger_active(void);

// Имя открытого файла (для показа в интерфейсе/логе), "" если не пишем.
const char *sd_logger_filename(void);

#ifdef __cplusplus
}
#endif

#endif // SD_LOGGER_H
