// ============================================================
// ЛОГГЕР НА microSD
//
// Задача: писать на карту все показания, НЕ задев отзывчивость экрана и не
// сорвав опрос ЭБУ. Это главное требование, поэтому про него подробно.
//
// ЧЕМ ОПАСНА ЗАПИСЬ НА КАРТУ. Обычный File.write() на SD через SPI — это
// блокирующая операция, и она непредсказуемо долгая: карта сама решает,
// когда ей стирать блок, и в такие моменты одна запись легко занимает
// десятки, а иногда сотни миллисекунд. Если делать это в главном цикле,
// мы получим ровно то, от чего только что уходили: провалы отрисовки и
// промахи опроса K-Line (у нас на обмен с ЭБУ всего 63 мс, а пауза P3
// 35 мс — одна «задумавшаяся» карта съела бы весь запас).
//
// КАК ОБХОДИМ. Три приёма:
//
//  1. РАЗНЫЕ ЯДРА. У ESP32 второе ядро простаивает: отрисовка и опрос живут
//     на ядре 1 (там крутится Arduino loop), а пишущая задача поднимается
//     на ЯДРЕ 0. Сколько бы карта ни думала, главный цикл в это время
//     работает, потому что физически это другой процессор.
//
//  2. ОЧЕРЕДЬ БЕЗ БЛОКИРОВКИ. Главный цикл не трогает карту вообще. Он
//     форматирует строку в стек и кладёт её в потоковый буфер FreeRTOS с
//     НУЛЕВЫМ таймаутом. Это несколько микросекунд и никакого ожидания:
//     если буфер переполнен, строка просто теряется и считается в счётчик.
//     Лучше потерять строку лога, чем кадр с ЭБУ.
//
//  3. КРУПНЫЕ РЕДКИЕ ЗАПИСИ. Пишущая задача не сливает каждую строку
//     отдельно, а копит их и отдаёт карте кусками по 4 КБ. У SD накладные
//     расходы на одну операцию почти не зависят от её размера, поэтому
//     одна запись на 4 КБ в разы дешевле, чем тридцать по 130 байт.
//     flush() (обновление таблицы FAT — самая дорогая операция) делается
//     ещё реже, раз в несколько секунд.
//
// СКОЛЬКО ЭТО ДАННЫХ. Кадр приходит примерно 10 раз в секунду, строка около
// 130 байт — порядка 1.3 КБ/с, то есть меньше 5 МБ в час. Для карты это
// ничто, буфера на 16 КБ хватает на 12 секунд полного затыка карты.
//
// ПРО ШИНУ (важно, см. docs/HARDWARE.md). У ESP32 всего два свободных SPI:
// VSPI занят дисплеем (пины 12/13/14/15), HSPI — тачем (25/32/39/33).
// Слот карты на этой плате разведён на свои пины 18/19/23 + CS 5, и они
// ничем не заняты, но СВОБОДНОГО КОНТРОЛЛЕРА SPI под них уже нет.
// Поэтому sd_logger_probe() — разовая проверка при старте: она временно
// берёт HSPI, выясняет, отвечает ли карта на этих пинах, и отдаёт шину
// тачу. Ответ на этот вопрос определяет, как подключать карту постоянно.
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>
#include "sd_logger.h"

// Пины слота microSD — стандартная разводка плат этой серии.
// В docs/HARDWARE.md была запись «карта висит на шине TFT со своим CS=5»,
// но она помечена как непроверенная, а на этой же плате ровно такое же
// предположение про тач оказалось неверным. Поэтому проверяем.
#define SD_SCK_PIN 18
#define SD_MISO_PIN 19
#define SD_MOSI_PIN 23
#define SD_CS_PIN 5

// Частота SPI для карты. 20 МГц — уверенно работает на карточных слотах
// этих плат; при ошибках чтения снижать до 10 МГц.
#define SD_SPI_HZ 20000000

// Сколько накопленных строк отдаём карте за одну запись.
#define SD_CHUNK_BYTES 4096
// Размер очереди между главным циклом и пишущей задачей.
#define SD_STREAM_BYTES 16384
// Как часто обновлять таблицу FAT. Реже — дешевле, но при внезапном
// пропадании питания теряется всё, что не сброшено.
#define SD_FLUSH_INTERVAL_MS 5000
// Ядро для пишущей задачи. Главный цикл Arduino живёт на ядре 1.
#define SD_TASK_CORE 0
#define SD_TASK_STACK 4096
#define SD_TASK_PRIO 1

static SPIClass g_sd_spi(HSPI);
static File g_file;
static StreamBufferHandle_t g_stream = NULL;
static TaskHandle_t g_task = NULL;
static volatile bool g_active = false;
static char g_filename[32] = "";

static volatile uint32_t g_lines = 0;
static volatile uint32_t g_dropped = 0;
static volatile uint32_t g_bytes = 0;
static volatile uint32_t g_write_ms = 0;

// --------------------------------------------------------------------------
// Разовая проверка слота при старте.
// Зовётся ДО инициализации тача, поэтому HSPI в этот момент свободен.
// После проверки шина отдаётся обратно — тач работает как обычно.
// --------------------------------------------------------------------------
bool sd_logger_probe(void) {
  Serial.println("[SD] проверка слота microSD на пинах SCK=18 MISO=19 MOSI=23 CS=5...");
  g_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  bool ok = SD.begin(SD_CS_PIN, g_sd_spi, SD_SPI_HZ);
  if (!ok) {
    Serial.println("[SD] карта НЕ ответила.");
    Serial.println("[SD] Это либо нет карты в слоте, либо слот разведён на");
    Serial.println("[SD] другие пины — тогда нужна распиновка платы.");
    SD.end();
    g_sd_spi.end();
    return false;
  }
  uint8_t type = SD.cardType();
  const char *tname = (type == CARD_MMC)    ? "MMC"
                      : (type == CARD_SD)   ? "SDSC"
                      : (type == CARD_SDHC) ? "SDHC"
                                            : "неизвестно";
  Serial.printf("[SD] карта ОТВЕТИЛА: тип %s, объём %llu МБ, свободно %llu МБ\n",
                tname, SD.cardSize() / (1024ULL * 1024ULL),
                (SD.totalBytes() - SD.usedBytes()) / (1024ULL * 1024ULL));
  return true;
}

// Освободить шину после проверки (чтобы отдать HSPI тачу).
static void sd_release(void) {
  SD.end();
  g_sd_spi.end();
}

void sd_logger_probe_release(void) { sd_release(); }

// --------------------------------------------------------------------------
// Пишущая задача. Живёт на втором ядре и только здесь трогает карту.
// --------------------------------------------------------------------------
static void sd_writer_task(void *arg) {
  // Буфер приходит из sd_logger_begin() уже выделенным в куче. Раньше он
  // был static — и тогда 4 КБ висели в памяти ВСЕГДА, даже когда карты нет
  // и задача не запускалась вовсе (проверено через nm по .bss). Теперь
  // без карты логгер не стоит ни байта сверх пары флагов.
  uint8_t *chunk = (uint8_t *)arg;
  size_t filled = 0;
  uint32_t last_flush = millis();

  for (;;) {
    // Ждём данные. Таймаут нужен, чтобы периодически сбрасывать FAT даже
    // когда поток редкий.
    size_t got = xStreamBufferReceive(g_stream, chunk + filled,
                                      SD_CHUNK_BYTES - filled,
                                      pdMS_TO_TICKS(500));
    filled += got;

    uint32_t now = millis();
    bool full = (filled >= SD_CHUNK_BYTES - 256);
    bool timeup = (now - last_flush >= SD_FLUSH_INTERVAL_MS);

    if (filled && (full || timeup)) {
      uint32_t t0 = millis();
      size_t w = g_file.write(chunk, filled);
      if (timeup) {
        g_file.flush();   // самая дорогая операция, поэтому редко
        last_flush = now;
      }
      g_write_ms += millis() - t0;
      g_bytes += w;
      filled = 0;
    }
  }
}

// --------------------------------------------------------------------------
// Поднять логгер целиком.
// --------------------------------------------------------------------------
bool sd_logger_begin(void) {
  g_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, g_sd_spi, SD_SPI_HZ)) {
    Serial.println("[SD] логгер НЕ поднят: карта не ответила");
    g_sd_spi.end();
    return false;
  }

  // Часов на плате нет, поэтому имя файла — просто следующий свободный
  // номер. Так логи разных поездок не затирают друг друга.
  for (int i = 1; i < 1000; i++) {
    snprintf(g_filename, sizeof(g_filename), "/drive%03d.csv", i);
    if (!SD.exists(g_filename)) break;
  }
  g_file = SD.open(g_filename, FILE_WRITE);
  if (!g_file) {
    Serial.printf("[SD] логгер НЕ поднят: не открылся %s\n", g_filename);
    SD.end();
    g_sd_spi.end();
    return false;
  }
  g_file.print("ms;rpm;ож_c;дрос_%;давл_кпа;баро_кпа;буст_бар;возд_c;"
               "gbc;воздух_кг_ч;расход_л_ч;смесь;мощн_лс;уоз_град;"
               "напряж_в;скор_кмч\n");

  g_stream = xStreamBufferCreate(SD_STREAM_BYTES, 1);
  if (!g_stream) {
    Serial.println("[SD] логгер НЕ поднят: не хватило памяти на очередь");
    g_file.close();
    SD.end();
    g_sd_spi.end();
    return false;
  }

  uint8_t *chunk = (uint8_t *)malloc(SD_CHUNK_BYTES);
  if (!chunk) {
    Serial.println("[SD] логгер НЕ поднят: не хватило памяти на буфер записи");
    vStreamBufferDelete(g_stream);
    g_stream = NULL;
    g_file.close();
    SD.end();
    g_sd_spi.end();
    return false;
  }

  BaseType_t r = xTaskCreatePinnedToCore(sd_writer_task, "sdlog", SD_TASK_STACK,
                                         chunk, SD_TASK_PRIO, &g_task,
                                         SD_TASK_CORE);
  if (r != pdPASS) {
    Serial.println("[SD] логгер НЕ поднят: не создалась задача");
    free(chunk);
    vStreamBufferDelete(g_stream);
    g_stream = NULL;
    g_file.close();
    SD.end();
    g_sd_spi.end();
    return false;
  }

  g_active = true;
  Serial.printf("[SD] логгер пишет в %s (запись на ядре %d, буфер %d КБ)\n",
                g_filename, SD_TASK_CORE, SD_STREAM_BYTES / 1024);
  return true;
}

// --------------------------------------------------------------------------
// Приём замера из главного цикла. Здесь НЕТ ни карты, ни ожидания.
// --------------------------------------------------------------------------
void sd_logger_write(const kline_data_t *kd, uint32_t now_ms) {
  if (!g_active || !kd) return;

  char line[160];
  int32_t boost_cbar = (kd->map_kpa_x100 - kd->baro_kpa_x100) / 100;
  int v100 = (int)(kd->voltage * 100 + 0.5f);

  // Разделитель — точка с запятой, дробные через запятую: так CSV
  // открывается в Excel с русской локалью без плясок с импортом.
  int n = snprintf(
      line, sizeof(line),
      "%lu;%ld;%ld;%ld;%ld,%02ld;%ld,%02ld;%s%ld,%02ld;%ld;"
      "%ld;%ld,%ld;%ld,%ld;%ld,%02ld;%ld;%ld,%ld;%d,%02d;%ld\n",
      (unsigned long)now_ms, (long)kd->rpm, (long)kd->coolant_c,
      (long)kd->throttle_pct, (long)(kd->map_kpa_x100 / 100),
      (long)(kd->map_kpa_x100 % 100), (long)(kd->baro_kpa_x100 / 100),
      (long)(kd->baro_kpa_x100 % 100), (boost_cbar < 0 ? "-" : ""),
      (long)(labs(boost_cbar) / 100), (long)(labs(boost_cbar) % 100),
      (long)kd->air_temp_c, (long)kd->gbc, (long)(kd->air_kgh_x10 / 10),
      (long)(kd->air_kgh_x10 % 10), (long)(kd->fuel_lph_x10 / 10),
      (long)(kd->fuel_lph_x10 % 10), (long)(kd->afr_x100 / 100),
      (long)(kd->afr_x100 % 100), (long)kd->power_hp,
      (long)(kd->ign_deg_x10 / 10), (long)(kd->ign_deg_x10 % 10), v100 / 100,
      v100 % 100, (long)kd->speed_kmh);
  if (n <= 0) return;
  if (n > (int)sizeof(line)) n = sizeof(line);

  // Нулевой таймаут — главный цикл не ждёт карту НИКОГДА.
  size_t sent = xStreamBufferSend(g_stream, line, (size_t)n, 0);
  if (sent == (size_t)n) {
    g_lines++;
  } else {
    g_dropped++;
  }
}

void sd_logger_stats(uint32_t *lines, uint32_t *dropped, uint32_t *bytes,
                     uint32_t *write_ms) {
  if (lines) *lines = g_lines;
  if (dropped) *dropped = g_dropped;
  if (bytes) *bytes = g_bytes;
  if (write_ms) *write_ms = g_write_ms;
}

bool sd_logger_active(void) { return g_active; }

const char *sd_logger_filename(void) { return g_active ? g_filename : ""; }
