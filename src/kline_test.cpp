// ============================================================
// K-Line — обмен с ЭБУ Январь 5.1.61 через донорскую K-Line плату на
// LM339.
//
// Физика (плата ESP32-2432S028 / CYD):
//   Разъём P5 (VIN-TX-RX-GND) НЕЛЬЗЯ использовать — электрически это
//   GPIO1/GPIO3, то есть тот же UART0, что и USB-Serial (CH340). Разъём
//   P3 даёт только один свободный пин (второй занят подсветкой
//   TFT_BL=GPIO21). Используем разъём CN1 (GND-22-27-3V3):
//     GPIO22 -> TX (в K-Line/донорскую плату)
//     GPIO27 -> RX (из K-Line/донорской платы)
//   Полярность подтверждена self-echo тестом — только прямая логика.
//
// ПРОТОКОЛ — ISO 14230 (KWP2000), "быстрая инициализация" (порт сразу
// на 10400 бод, без 5-бодного адресного байта и без L-line). Формат
// кадра: [длина][адрес ЭБУ][адрес тестера][сервис/данные...][контрольная
// сумма = сумма всех предыдущих байт по модулю 256]. K-Line
// однопроводная — в ответе сначала идёт ЭХО наших же переданных байт, а
// затем настоящий ответ ЭБУ.
//
// ПОДТВЕРЖДЕНО НА РЕАЛЬНОМ ЭБУ (2026-09-16):
//   startCommunication (81 10 F1 81 03) -> 0xC1 в ответе.
//   readData/RLI_ASS (82 10 F1 21 01 A5) -> 0x61 в ответе, 47 байт
//   суммарно (6 байт эха запроса + 41 байт настоящего ответа).
//   Смещения байт параметров ВЗЯТЫ ИЗ ОПУБЛИКОВАННОГО РАБОЧЕГО КОДА для
//   Январь 7.2 (другая ревизия, но тот же протокол/семейство) и
//   ПРОВЕРЕНЫ на этом конкретном 5.1.61: с реальными данными получили
//   температуру ОЖ ~20°C и напряжение ~11.95В — оба значения совпали с
//   реальными условиями на столе (комнатная температура, БП ~12В), это
//   и есть подтверждение, что раскладка байт та же. Индексы ниже — от
//   начала ПОЛНОГО принятого буфера (эхо нашего запроса + ответ ЭБУ), с
//   такими же номерами, что в буфере целиком, включая эхо!:
//     [10] = 0x61 (позитивный ответ)
//     [20] = температура ОЖ + 40 (т.е. реальная темп-ра = байт-40)
//     [22] = положение дросселя, %
//     [23] = обороты двигателя / 40 (т.е. rpm = байт*40)
//     [29] = скорость автомобиля, км/ч
//     [30] = напряжение борт. сети: U = 5.2 + байт*0.05
//   Остальные параметры (давление, топливо и т.д.) пока не проверены на
//   этом ЭБУ и не парсятся.
//
// RAW-АНАЛИЗ ДЛЯ ПОИСКА НОВЫХ ПАРАМЕТРОВ (напр. MAP из TRS251):
//   kline_analyze_raw() хранит предыдущий ответ readData и на каждом
//   следующем сравнивает побайтово. Если что-то изменилось — печатает в
//   Serial полный RAW-дамп (с индексами) и отдельно список изменившихся
//   байт (индекс, было -> стало). Если ничего не изменилось — молчит
//   (иначе Serial захлебнётся при опросе раз в 300мс). Метод: меняем на
//   ЭБУ физически ОДИН параметр (например, давление на ДАД) и смотрим,
//   какой байт(ы) дрогнули — так же, как автор поста про Январь 7.2/
//   Bosch 7.9.7 подбирал байты для Bosch вручную, только тут вместо
//   осциллографа сравнение самих HEX-пакетов. Это временный
//   инструмент для reverse engineering — не часть готовой раскладки
//   параметров, найденные байты потом переносятся в kline_parse_read_data
//   и kline_data_t как обычные поля.
//
// АВТОПОДКЛЮЧЕНИЕ (без кнопки):
//   Раньше подключение запускалось только по кнопке. Идея проверять
//   готовность ЭБУ по уровню на RX (например, "idle=HIGH значит ЭБУ
//   запитан") отброшена — на этой линии до включения питания ЭБУ уровень
//   не определён достоверно без осциллографа, гадать рискованно. Вместо
//   этого — классический подход (как в реальных рабочих реализациях,
//   напр. STM32-скетч с периодическим таймером переподключения):
//   пока не подключены — пробуем startCommunication+readData каждые
//   KLINE_RECONNECT_INTERVAL_MS; как только ответ получен — переходим в
//   режим обычного опроса каждые KLINE_POLL_INTERVAL_MS; если опрос
//   перестал получать ответ — снова пробуем переподключаться. Self-echo
//   тест (проверка своей же обвязки TX/RX) больше не нужен на каждой
//   попытке — он уже подтвердил, что железо исправно, эта проверка
//   осталась только в ручном варианте (кнопка на вкладке DEBUG).
//
// НЕБЛОКИРУЮЩИЙ ОПРОС (2026-09-17):
//   Раньше каждый шаг (stopComm/startComm/readData) ждал ответ через
//   delay(100/150/150 мс) — то есть loop() реально останавливался на
//   это время, LVGL не мог ни перерисовать кадр, ни прочитать тач
//   (отсюда подтормаживания). Переписано на стейт-машину: отправили
//   команду и сразу вышли из kline_test_poll(); на каждом следующем
//   вызове (а loop() крутится много раз в мс) просто проверяем, не
//   пришли ли уже все байты и не истёк ли таймаут — сами тайминги
//   (100/150/150 мс) НЕ менялись, только способ ожидания. Один "тик"
//   стейт-машины — это O(1) проверка millis()/available(), из-за этого
//   loop() больше нигде не блокируется дольше, чем на реальное время
//   передачи нескольких байт по UART (единицы мс).
// ============================================================

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include <stdio.h>
#include "kline_test.h"
#include "screen_debug.h"
#include "kline_data.h"

#define KLINE_TX_PIN 22
#define KLINE_RX_PIN 27
// 10400 — обязательная скорость для самой "быстрой инициализации"
// (startCommunication/readData) — это протокольный факт, не место для
// экспериментов (пробовали 19200 наугад — просто не подключалось).
// Таймаут ответа НЕ разгоняет обмен сам по себе — это просто потолок
// ожидания, реальную задержку определяет только бод; стейт-машина и так
// выходит из ожидания сразу по приходу всех байт, не пересиживая таймаут.
//
// Пробовали ПОСЛЕ подключения переключаться на 38400 через
// задокументированную (в стороннем STM32-коде) команду kSwitch38400 —
// ОТКЛЮЧЕНО (2026-09-17), см. комментарий у kSwitch38400 ниже: нет
// надёжного признака, что ЭБУ реально принял смену скорости, из-за
// этого ловили бесконечный цикл ложных переподключений.
// 19200 в момент "быстрой инициализации" не сработал (ЭБУ вообще не
// отвечает на этой скорости в startCommunication/readData) — откатили
// на 10400. ИОН/CombiLoader явно умеют разговаривать быстрее — но,
// видимо, через отдельную команду смены скорости ПОСЛЕ подключения на
// 10400, а не через изначальный fast-init на другой скорости. См.
// docs/KLINE.md — ищем правильную команду/признак для ЭТОГО ЭБУ.
// ИТОГ ЭКСПЕРИМЕНТОВ С 19200 (2026-09-17): даже с таймаутом 500мс на
// startCommunication приходит РОВНО эхо своей же команды (5 байт) и
// больше ничего — ЭБУ на этой скорости не отвечает вообще, дело не в
// таймингах. У ИОН для этого ЭБУ 19200 тоже настраивается вручную —
// скорее всего, там другой протокол/команды инициализации, а не те же
// байты на другой скорости. Нужен сниффер трафика ИОН (см. project-
// заметки), чтобы увидеть настоящий протокол. Пока остаёмся на 10400.
#define KLINE_BAUD 10400
// 75мс оказалось МАЛО для нормального 10400: 47 байт readData передаются
// ~45мс сами по себе, плюс задержка ответа ЭБУ — впритык, из-за чего
// часть ответов обрезалась (см. баг в kline_parse_read_data выше и
// комментарий там). Возвращено на проверенные 150мс. Скоростные
// эксперименты (19200/38400/57600) приостановлены по решению
// пользователя — сначала до конца расшифровать readData на 10400, потом
// отдельно разбираться с высокоскоростным режимом ИОН.
// Потолок ожидания ПОЛНОГО ответа (не задержка!). Стейт-машина выходит
// сразу по приходу всех байт, так что на нормальную скорость это не
// влияет — важно только для детекта потери пакета.
// По KWP2000/ISO14230: P2max (макс. время ответа ЭБУ) = 50мс.
//  - на 10400: 47 байт readData идут ~45мс + P2 → держим 150 с запасом
//    при подключении (это один раз, надёжность важнее скорости);
//  - на 38400: те же 47 байт ~12мс + P2(≤50) → ~75мс достаточно (ровно
//    столько ставят в ИОН: ReadTimeout=75), см. kline_poll_timeout_ms.
#define KLINE_RESPONSE_TIMEOUT_MS 150
#define KLINE_POLL_TIMEOUT_FAST_MS 75
// Период опроса readData — как часто обновляется приборка. 100мс (~10Гц),
// было 300. По KWP2000 между концом ответа ЭБУ и следующим запросом нужен
// P3min (~55мс по стандарту; в ИОН ReqTimeout=75). На 38400 ответ ~40мс,
// при периоде 100мс зазор до следующего запроса ~60мс — выше P3min.
// Ниже (75–90мс) технически можно (ИОН примерно так и гоняет), но зазор
// становится впритык к P3min: если в Serial пойдут обрезки (n<47) или
// переподключения — вернуть 100 или выше.
#define KLINE_POLL_INTERVAL_MS 100
// Интервал между попытками подключения. 800мс — достаточно "тишины на
// шине" для fast-init (нужно ~300мс), но заметно быстрее прежних 2с:
// ускоряет и первый коннект, и восстановление после залипания на 38400.
#define KLINE_RECONNECT_INTERVAL_MS 800

// АВТОПЕРЕХОД НА ПОВЫШЕННУЮ СКОРОСТЬ (2026-09-18) — рецепт подтверждён на
// реальном ЭБУ, см. docs/KLINE.md ("38400 РАБОТАЕТ"). После успешного
// подключения на 10400 автоматически (один раз) переводим ЭБУ на 38400
// (StopDiagnosticSession -> смена скорости), дальше опрашиваем на этой
// скорости. При неудаче — честно остаёмся/переподключаемся на 10400.
// Поставить 0 — отключить автопереход, работать только на 10400.
#define KLINE_AUTO_FAST 1
#define KLINE_FAST_BAUD 38400
// После скольких подряд неудачных обычных попыток коннекта на 10400
// пробовать "разбудить" ЭБУ, залипший на 38400 (см. ветку восстановления
// в kline_test_poll). Обычные попытки при этом остаются ЧИСТЫМИ на 10400.
// 1 = чередуем: неудачная чистая попытка -> восстановление -> чистая ...
// Так залипший на 38400 ЭБУ восстанавливается за ~1.5-2с, а обычный
// коннект (ЭБУ уже на 10400) всё равно проходит с первой чистой попытки.
#define KLINE_FAST_RECOVER_AFTER 1

static HardwareSerial KlineSerial(2); // UART2
static bool g_connected = false;
static bool g_fast_active = false; // true = сейчас на повышенной скорости
static uint8_t g_connect_fail_count = 0; // подряд неудачных попыток коннекта
static uint32_t g_last_poll_ms = 0;
static uint32_t g_last_connect_attempt_ms = 0;
static uint32_t g_current_baud = 0; // 0 = ещё не настроен

// Кэш параметров, читаемых отдельной командой SID 0x23 (ротацией, см.
// kExtReads и блок про KLINE_AIRTEMP_INTERVAL_MS ниже). Меняются медленно,
// поэтому кэшируем и подмешиваем в каждый kline_data_set.
static int32_t g_air_temp_c = 0;
static bool g_air_temp_valid = false;
static int32_t g_corr_cn = 0;
static int32_t g_corr_coolant = 0;
static int32_t g_corr_charge = 0;
static int32_t g_charge_temp_c = 0;
static bool g_ext_valid = false; // прочитан ли блок корр+темп заряда
static uint32_t g_last_airtemp_ms = 0;

// Потолок ожидания ответа в установившемся опросе — зависит от текущей
// скорости: на 38400 ответ приходит намного быстрее, поэтому меньший
// потолок = быстрее замечаем обрыв, не рискуя надёжностью на 10400.
static uint32_t kline_poll_timeout_ms(void) {
  return (g_current_baud >= 38400) ? KLINE_POLL_TIMEOUT_FAST_MS
                                   : KLINE_RESPONSE_TIMEOUT_MS;
}

// Кадры и контрольные суммы — из задокументированного рабочего кода для
// Январь 7.2 (формат KWP2000, адрес ЭБУ 0x10, адрес тестера 0xF1).
static const uint8_t kStopComm[] = {0x81, 0x10, 0xF1, 0x82, 0x04};
static const uint8_t kStartComm[] = {0x81, 0x10, 0xF1, 0x81, 0x03};
static const uint8_t kReadData[] = {0x82, 0x10, 0xF1, 0x21, 0x01, 0xA5};

// ReadMemoryByAddress (SID 0x23) — читает произвольный адрес XDATA, чего
// нет в штатном readData. Разбор дампа J5TRS251 (2026-09-18): диспетчер
// SID 0x23 -> обработчик 0xABAA, формат запроса:
//   23 00 <адрес_hi> <адрес_lo> <длина>  (первый байт обязан быть 0x00,
//   длина 1..0x77). Ответ содержит 0x63 (=0x23+0x40) и затем <длина> байт.
// Список доп. адресов, которые опрашиваем по очереди (по одному за слот,
// раз в KLINE_AIRTEMP_INTERVAL_MS — все эти величины меняются медленно):
typedef struct {
  uint8_t hi, lo, len;
  uint8_t kind; // 0 = ДТВ (воздух); 1 = блок 0xF99C..F99E (корр+темп заряда)
} kline_ext_read_t;
static const kline_ext_read_t kExtReads[] = {
    {0xF8, 0x85, 1, 0}, // температура воздуха (ДТВ)
    {0xF9, 0x9C, 3, 1}, // корр.ОЖ (F99C), корр.заряд (F99D), темп.заряда (F99E)
    {0xF9, 0x42, 1, 2}, // поправка ЦН (F942)
};
static int g_ext_index = 0;

// Собирает кадр ReadMemoryByAddress для адреса hi:lo длиной len. out >= 9.
static size_t kline_build_readmem(uint8_t hi, uint8_t lo, uint8_t len,
                                  uint8_t *out) {
  out[0] = 0x85; // 0x80 | 5 байт данных (23 00 hi lo len)
  out[1] = 0x10;
  out[2] = 0xF1;
  out[3] = 0x23;
  out[4] = 0x00;
  out[5] = hi;
  out[6] = lo;
  out[7] = len;
  uint16_t sum = 0;
  for (int i = 0; i < 8; i++) sum += out[i];
  out[8] = (uint8_t)(sum & 0xFF);
  return 9;
}
// Смена скорости на 38400 — из того же STM32-кода (ab38400), формат
// такой же KWP2000-кадр (сервис 0x10, параметр 0x81 = код "38400").
// ОТКЛЮЧЕНО (2026-09-17): пробовали считать переход принятым, если ЭБУ
// ответил хоть чем-то сверх эха запроса — детект оказался ложным
// срабатыванием (реф-код тоже только проверял контрольную сумму, без
// проверки конкретного кода подтверждения), реального перехода ЭБУ на
// 38400 не происходило, из-за чего следующий опрос не долетал и связь
// рвалась по кругу (переподключение → ложный "успех" → снова обрыв).
// Оставлено закомментированным на будущее — понадобится точный
// документированный признак подтверждения именно для этого ЭБУ.
// static const uint8_t kSwitch38400[] = {0x83, 0x10, 0xF1, 0x10, 0x81, 0x26, 0x3B};

// Настраивает UART2 на нужную скорость, только если она реально
// отличается от текущей (иначе begin() не нужен).
static void kline_set_baud(uint32_t baud) {
  if (g_current_baud == baud) {
    return;
  }
  pinMode(KLINE_RX_PIN, INPUT_PULLUP);
  KlineSerial.begin(baud, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN, false);
  g_current_baud = baud;
}

// Обёртка над screen_debug_set_kline_result() — дописывает текущую
// скорость и таймаут ответа к любому статусному сообщению, чтобы на
// экране DEBUG всегда было видно, на чём сейчас реально работаем (по
// просьбе пользователя во время экспериментов со скоростью).
static void kline_set_status(const char *msg, bool success) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s [%lu/%dмс]", msg,
           (unsigned long)g_current_baud, KLINE_RESPONSE_TIMEOUT_MS);
  screen_debug_set_kline_result(buf, success);
}

static bool kline_read_byte(uint8_t *out, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (KlineSerial.available()) {
      *out = (uint8_t)KlineSerial.read();
      return true;
    }
  }
  return false;
}

// Sanity-check физики линии (без ЭБУ): видит ли RX то, что шлёт TX.
// Используется только вручную (кнопка на DEBUG) — на эту проверку
// больше не завязана автоматическая логика подключения, см. шапку файла.
static bool kline_self_echo_test(void) {
  while (KlineSerial.available()) {
    KlineSerial.read();
  }
  KlineSerial.write((uint8_t)0xAA);
  KlineSerial.flush();
  uint8_t got = 0;
  bool ok = kline_read_byte(&got, 60);
  // Закомментировано на время reverse-engineering K-Line (см.
  // docs/KLINE.md) — не мешаем Serial Monitor лишним выводом.
  // Serial.printf("  self-echo: %s (получено 0x%02X)\n",
  //               ok ? "ЕСТЬ эхо" : "нет эха", got);
  return ok;
}

// Отправляет команду немедленно, НЕ дожидаясь ответа (неблокирующе).
// Сброс входного буфера перед отправкой — чтобы не подхватить хвост
// предыдущего обмена.
static void kline_send_only(const uint8_t *cmd, size_t len) {
  while (KlineSerial.available()) {
    KlineSerial.read();
  }
  KlineSerial.write(cmd, len);
  KlineSerial.flush(); // ждёт только физическую отправку (единицы мс)
}

// Забирает то, что уже накопилось в приёмном буфере UART, в out (не
// блокируется, просто читает то, что есть прямо сейчас).
static void kline_collect_available(uint8_t *out, size_t *n, size_t max_out) {
  while (KlineSerial.available() && *n < max_out) {
    out[(*n)++] = (uint8_t)KlineSerial.read();
  }
}

// Закомментировано на время reverse-engineering K-Line (см. docs/KLINE.md)
// — не мешаем Serial Monitor лишним выводом, оставлен только RAW-анализ
// (kline_analyze_raw ниже).
// static void kline_dump_hex(const char *label, const uint8_t *buf, size_t n) {
//   Serial.print(label);
//   Serial.print(" (");
//   Serial.print(n);
//   Serial.print(" байт): ");
//   for (size_t i = 0; i < n; i++) {
//     Serial.printf("%02X ", buf[i]);
//   }
//   Serial.println();
// }

static bool kline_contains(const uint8_t *buf, size_t n, uint8_t value) {
  for (size_t i = 0; i < n; i++) {
    if (buf[i] == value) return true;
  }
  return false;
}

// Байты с уже известным смыслом — скрываем их из CHANGED, чтобы лог не
// засорялся их обычным дрожанием/сменой при движке офф. Индексы — в
// той же нумерации, что и весь остальной файл (от начала ПОЛНОГО
// буфера, эхо запроса включено):
//   0-9   — эхо нашего же запроса (81 10 F1 21 01 A5), константа
//   10-11 — SID (0x61) + LID (0x01) эхо, тоже константа при успехе
//   20    — coolant, 22 — throttle, 23 — rpm, 29 — speed, 30 — voltage
//     (уже расшифрованы и используются в kline_parse_read_data)
//   38-39 — часовой расход топлива (л/ч), 40-41 — путевой расход
//     топлива (л/100км) — известны из поста про Январь 7.2 (buffer[38..41]
//     в исходном коде), просто пока не добавлены в kline_data_t
//   46    — контрольная сумма, чисто производная от остальных байт
static bool kline_is_known_byte(size_t idx) {
  static const size_t known[] = {0,  1,  2,  3,  4,  5,  6,  7,  8, 9,
                                  10, 11, 20, 22, 23, 29, 30, 38, 39, 40,
                                  41, 46};
  for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
    if (known[i] == idx) {
      return true;
    }
  }
  return false;
}

// См. комментарий "RAW-АНАЛИЗ" в шапке файла. Хранит предыдущий readData
// и печатает в Serial только то, что изменилось с прошлого раза, помимо
// уже известных байт (kline_is_known_byte) — молчит, если поменялись
// только они, или если пакет вообще идентичен предыдущему.
static uint8_t g_prev_resp[64];
static size_t g_prev_resp_len = 0;
static bool g_have_prev_resp = false;

static void kline_analyze_raw(const uint8_t *resp, size_t n) {
  if (n == 0) {
    return;
  }

  bool changed = (n != g_prev_resp_len);
  if (!changed) {
    for (size_t i = 0; i < n; i++) {
      if (resp[i] != g_prev_resp[i] && !kline_is_known_byte(i)) {
        changed = true;
        break;
      }
    }
  }

  if (g_have_prev_resp && changed) {
    Serial.printf("K-Line RAW: пакет изменился относительно предыдущего (n=%u)\n",
                  (unsigned)n);
    Serial.print("RAW:");
    for (size_t i = 0; i < n; i++) {
      Serial.printf(" [%u]%02X", (unsigned)i, resp[i]);
    }
    Serial.println();

    Serial.println("CHANGED (известные байты скрыты):");
    size_t common = (n < g_prev_resp_len) ? n : g_prev_resp_len;
    for (size_t i = 0; i < common; i++) {
      if (resp[i] != g_prev_resp[i] && !kline_is_known_byte(i)) {
        Serial.printf("  [%u] %02X -> %02X\n", (unsigned)i, g_prev_resp[i],
                      resp[i]);
      }
    }
    for (size_t i = common; i < n; i++) {
      if (!kline_is_known_byte(i)) {
        Serial.printf("  [%u] (новый байт) -> %02X\n", (unsigned)i, resp[i]);
      }
    }
    Serial.println();
  }

  memcpy(g_prev_resp, resp, n);
  g_prev_resp_len = n;
  g_have_prev_resp = true;
}

// ПРОБА StartDiagnosticSession (сервис KWP2000 0x10) — идея пользователя
// (2026-09-17): не гадать скорость напрямую, а проверить, поддерживает
// ли ЭБУ смену РЕЖИМА СЕССИИ через штатный сервис 0x10, оставаясь на
// 10400. По стандарту KWP2000 положительный ответ на сервис 0x10 всегда
// начинается с 0x50, отрицательный — с 0x7F; так что даже без знания
// точного формата ответа для ЭТОГО ЭБУ можно надёжно отличить "принял"
// от "отказал". Пробуем по одной под-функции за раз, с паузой между
// попытками (НЕ хаотичным циклом) — 0x81 (её мы уже используем в
// startCommunication, тут просто для контроля), 0x82, 0x85, 0x86
// (в некоторых KWP2000-реализациях 0x86 — "Development/Engineering
// Session", иногда используется для переключения скорости через
// последующий LinkControl, но для ЭТОГО ЭБУ/TRS251 это НЕ подтверждено
// нигде — сама проба и есть проверка). Результат только печатается в
// Serial, никаких переключений скорости эта проба не делает.
static const uint8_t kProbeSubfn[] = {0x81, 0x82, 0x85, 0x86};
static int g_probe_index = -1; // -1 = проба не идёт
static bool g_probe_requested = false;

static void kline_build_session_probe(uint8_t subfn, uint8_t *frame6) {
  frame6[0] = 0x82;
  frame6[1] = 0x10;
  frame6[2] = 0xF1;
  frame6[3] = 0x10;
  frame6[4] = subfn;
  uint16_t sum = frame6[0] + frame6[1] + frame6[2] + frame6[3] + frame6[4];
  frame6[5] = (uint8_t)(sum & 0xFF);
}

// ============================================================
// ЭКСПЕРИМЕНТ СО СКОРОСТЬЮ, РАУНД 2 (2026-09-17) — по уточнённому плану
// пользователя после проверки документации/форумов по TRS251:
//   - Прошлый вывод "KWP diagnostic session route закрыт" был СЛИШКОМ
//     сильным — proба сервиса 0x10 БЕЗ параметра скорости отклонялась
//     целиком (generalReject), но это не проверяет вариант, когда сама
//     под-функция несёт код скорости.
//   - Найдено упоминание: некоторые KWP2000-реализации кодируют переход
//     на повышенную скорость через StartDiagnosticSession с ДОПОЛНИТЕЛЬНЫМ
//     байтом baudrateIdentifier: 0x30=19200, 0x50=38400, 0x64=57600,
//     под-функция сессии в этом варианте — 0x89 (не пробовали раньше,
//     мы гоняли только 0x81/0x82/0x85/0x86 БЕЗ доп. параметра).
//   - Также существует отдельный сервис 0x26 SetDataRates — по стандарту
//     ISO 14230 принимает 3 параметра (slowRate/mediumRate/fastRate),
//     конкретная кодировка — на усмотрение производителя ЭБУ. Точных
//     значений для TRS251 нет — пробуем как первую безопасную попытку,
//     легко поменять через defines ниже.
//   - На форуме TRS есть прямое упоминание связки TRS251+ИОН именно на
//     38400 — так что 38400 полностью не списываем со счетов.
//
// ВАЖНО: этот блок НИЧЕГО не переключает автоматически — только шлёт
// запрос, слушает ответ и печатает TX/RX/расшифровку в Serial. UART
// остаётся на 10400 всё время. Если ЭБУ ответит позитивно — переключение
// скорости остаётся отдельным, осознанным следующим шагом, не частью
// этого теста.
// ============================================================
#define KLINE_SPEED_EXPERIMENT 1 // выключить эксперимент — поставить 0

#if KLINE_SPEED_EXPERIMENT

// Данные (без заголовка/длины/чексуммы) для каждой пробы — генерируются
// в полный кадр через kline_build_frame().
static const uint8_t kSpdData19200[] = {0x10, 0x89, 0x30};
static const uint8_t kSpdData38400[] = {0x10, 0x89, 0x50};
static const uint8_t kSpdData57600[] = {0x10, 0x89, 0x64};
// SetDataRates(slow, medium, fast) — ПЕРВАЯ безопасная попытка, кодировка
// НЕ подтверждена нигде для TRS251, просто переиспользуем те же коды
// скорости, что и выше. Легко поменять эти 3 байта, если понадобится.
static const uint8_t kSpdDataSetRates[] = {0x26, 0x30, 0x50, 0x64};

// ПРОБА "OltPin" через InputOutputControlByLocalIdentifier (сервис 0x30)
// — гипотеза по итогам разбора дизассемблированных исходников
// родственных прошивок Январь (репозиторий dm_bir_olt на GitHub,
// 2026-09-18). В j5TRS2431.asm (ближайший родственник TRS251) диспетчер
// сервиса 0x30 при LID=0x0F вызывает функцию, которая управляет
// отдельным выводом P3.2 микроконтроллера — в родственной прошивке
// Январь 7.2 этот же вывод прямо подписан в комментариях как "OltPin",
// физически ОТДЕЛЬНЫЙ от K-line RxD/TxD (P3.0/P3.1). Гипотеза: ИОН перед
// переходом в свой "OLT"-режим (возможно, включая смену скорости) сперва
// дёргает этот вывод именно через сервис 0x30, а не через диагностические
// сессии/SetDataRates, которые мы уже пробовали и получили отказ.
//   R3=0x00 -> выставить OltPin в 1
//   R3=0x01 -> прочитать состояние OltPin
// ВАЖНО: точный порядок байт LID/параметра в запросе для ИМЕННО TRS251 не
// подтверждён живым протоколом — это проверяемая гипотеза, не факт.
static const uint8_t kSpdDataOltSet[] = {0x30, 0x0F, 0x00};
static const uint8_t kSpdDataOltRead[] = {0x30, 0x0F, 0x01};

typedef struct {
  const uint8_t *data;
  size_t data_len;
  const char *label;
} kline_speed_probe_t;

static const kline_speed_probe_t kSpeedProbes[] = {
    {kSpdData19200, sizeof(kSpdData19200), "StartDiagSession 0x89 +19200(0x30)"},
    {kSpdData38400, sizeof(kSpdData38400), "StartDiagSession 0x89 +38400(0x50)"},
    {kSpdData57600, sizeof(kSpdData57600), "StartDiagSession 0x89 +57600(0x64)"},
    {kSpdDataSetRates, sizeof(kSpdDataSetRates), "SetDataRates 0x26 (30/50/64)"},
    {kSpdDataOltSet, sizeof(kSpdDataOltSet), "IOControl OltPin SET (0x30 LID=0x0F op=00)"},
    {kSpdDataOltRead, sizeof(kSpdDataOltRead), "IOControl OltPin READ (0x30 LID=0x0F op=01)"},
};

static int g_spd_index = -1; // -1 = тест не идёт
static bool g_spd_requested = false;
static uint8_t g_spd_tx[8];
static size_t g_spd_tx_len = 0;

// Собирает полный KWP2000-кадр [длина][адрес ЭБУ][адрес тестера][data...]
// [checksum] из произвольных data-байт (включая сам SID). out должен
// вмещать data_len+4 байт. Возвращает итоговую длину кадра.
static size_t kline_build_frame(uint8_t *out, const uint8_t *data,
                                 size_t data_len) {
  out[0] = (uint8_t)(0x80 | data_len);
  out[1] = 0x10;
  out[2] = 0xF1;
  memcpy(out + 3, data, data_len);
  uint16_t sum = 0;
  for (size_t i = 0; i < 3 + data_len; i++) {
    sum += out[i];
  }
  out[3 + data_len] = (uint8_t)(sum & 0xFF);
  return 3 + data_len + 1;
}

// Расшифровка кода отрицательного ответа KWP2000 (NRC) в текст — чтобы
// не гадать, что означает 7F xx YY.
static const char *kline_nrc_text(uint8_t nrc) {
  switch (nrc) {
    case 0x10: return "generalReject";
    case 0x11: return "serviceNotSupported";
    case 0x12: return "subFunctionNotSupported-invalidFormat";
    case 0x21: return "busy-repeatRequest";
    case 0x22: return "conditionsNotCorrect-requestSequenceError";
    case 0x31: return "requestOutOfRange";
    case 0x33: return "securityAccessDenied";
    case 0x35: return "invalidKey";
    case 0x78: return "requestCorrectlyReceived-responsePending";
    default:   return "неизвестный NRC";
  }
}

// Печатает TX/RX/расшифровку одной пробы в Serial. Не переключает
// скорость сама — только докладывает результат.
static void kline_speed_probe_report(const uint8_t *tx, size_t tx_len,
                                      const uint8_t *rx, size_t rx_len) {
  const kline_speed_probe_t *p = &kSpeedProbes[g_spd_index];

  Serial.printf("[KL] SPEED TEST '%s'\n", p->label);
  Serial.print("  TX:");
  for (size_t i = 0; i < tx_len; i++) Serial.printf(" %02X", tx[i]);
  Serial.println();
  Serial.print("  RX:");
  for (size_t i = 0; i < rx_len; i++) Serial.printf(" %02X", rx[i]);
  Serial.println();

  // Ответ ЭБУ идёт СРАЗУ ПОСЛЕ эха нашего же запроса (tx_len байт).
  const uint8_t *resp = (rx_len > tx_len) ? rx + tx_len : NULL;
  size_t resp_len = (rx_len > tx_len) ? rx_len - tx_len : 0;

  if (resp_len == 0) {
    Serial.println("  RESULT: нет ответа (только эхо своего запроса)");
  } else if (resp_len >= 7 && resp[3] == 0x7F) {
    // Полный кадр ответа: [0x83=len][F1=src][10=dst][7F][echoed SID][NRC]
    // [checksum] — 0x7F стоит ПОСЛЕ адресных байт, не сразу после len
    // (это и было багом в первой версии — сверяли не тот индекс).
    Serial.printf("  RESULT: НЕГАТИВНЫЙ — service=0x%02X NRC=0x%02X (%s)\n",
                  resp[4], resp[5], kline_nrc_text(resp[5]));
  } else {
    bool has50 = kline_contains(resp, resp_len, 0x50);
    bool has66 = kline_contains(resp, resp_len, 0x66);
    bool has70 = kline_contains(resp, resp_len, 0x70);
    bool has7f = kline_contains(resp, resp_len, 0x7F);
    if (has50) {
      Serial.println("  RESULT: ПОЗИТИВНЫЙ (0x50, StartDiagnosticSession) — "
                      "SPEED CHANGE ACCEPTED (скорость НЕ переключена, это "
                      "только проверка)");
    } else if (has66) {
      Serial.println("  RESULT: ПОЗИТИВНЫЙ (0x66, SetDataRates) — "
                      "SPEED CHANGE ACCEPTED (скорость НЕ переключена, это "
                      "только проверка)");
    } else if (has70) {
      Serial.println("  RESULT: ПОЗИТИВНЫЙ (0x70, IOControlByLocalIdentifier) "
                      "— ЭБУ ПРИНЯЛ команду OltPin!");
    } else if (has7f) {
      Serial.println("  RESULT: негативный (0x7F, формат не как ожидался — "
                      "см. RX выше)");
    } else {
      Serial.println("  RESULT: нечёткий ответ, см. RX выше");
    }
  }
  Serial.println("  CURRENT BAUD: 10400 (не менялась)");
}

#endif // KLINE_SPEED_EXPERIMENT

// Разбирает ответ на readData по смещениям, подтверждённым на реальном
// ЭБУ (см. комментарий в шапке файла). Возвращает false, если в буфере
// нет ожидаемых байт (нет 0x61) или ответ короче полного кадра.
//
// ВАЖНО (баг найден и исправлен 2026-09-17, по замечанию пользователя):
// раньше здесь была проверка "n <= 30" — то есть при частично принятом
// ответе (например, таймаут истёк раньше, чем пришли все 47 байт) код
// всё равно пытался читать resp[34]/[35] (boost_raw) и resp[28]
// (ign_deg), которые лежат ЗА пределами реально принятых данных — это
// мусор из статического буфера (хвост предыдущего, более длинного
// ответа), а не значения от ЭБУ. Именно поэтому BOOST/IGN "прыгали" в
// логах — это было не дрожание параметра, а чтение непринятых байт.
// Полный кадр readData всегда 47 байт (6 эха + 41 ответа) — просто
// требуем ровно это, а не пытаемся спасти частичные данные.
static bool kline_parse_read_data(const uint8_t *resp, size_t n,
                                   kline_data_t *out) {
  if (n < 47 || resp[10] != 0x61) {
    return false;
  }
  out->connected = true;
  out->coolant_c = (int32_t)resp[20] - 40;
  out->throttle_pct = resp[22];
  out->rpm = (int32_t)resp[23] * 40;
  out->speed_kmh = resp[29];
  out->voltage = 5.2f + resp[30] * 0.05f;
  // Ниже — НЕ подтверждено на этом ЭБУ, кандидаты из второго
  // стороннего кода (см. docs/KLINE.md, таблица кандидатов). Байты те
  // же, что и в остальном разборе (полный буфер, эхо включено).
  out->ign_deg = ((int32_t)resp[28] * 10 / 2) / 10;
  out->boost_raw = (int32_t)resp[34] | ((int32_t)resp[35] << 8);
  // Цикловое наполнение (GBC) — XDATA 0xF808, лежит в readData [36]-[37]
  // (16-бит, младший байт первым, как у MAP).
  out->gbc = (int32_t)resp[36] | ((int32_t)resp[37] << 8);
  // Параметры, читаемые отдельной командой (SID 0x23) — подмешиваем
  // последние кэшированные значения, чтобы readData их не сбрасывал.
  out->air_temp_c = g_air_temp_c;
  out->air_temp_valid = g_air_temp_valid;
  out->corr_cn = g_corr_cn;
  out->corr_coolant = g_corr_coolant;
  out->corr_charge = g_corr_charge;
  out->charge_temp_c = g_charge_temp_c;
  out->ext_valid = g_ext_valid;
  return true;
}

// Стейт-машина неблокирующего опроса. Тайминги (100/150/150 мс) — те же,
// что были в блокирующей версии, просто ждём их не через delay(), а
// проверяя millis() на каждом вызове kline_test_poll().
typedef enum {
  KLINE_ST_IDLE,             // не подключены/не опрашиваем — ждём таймер
  KLINE_ST_CONNECT_GAP,      // stopComm отправлен, выдерживаем паузу 100мс
  KLINE_ST_CONNECT_WAIT_START, // startComm отправлен, ждём 0xC1
  KLINE_ST_CONNECT_GAP2,     // 0xC1 получен, пауза перед readData
  KLINE_ST_CONNECT_WAIT_READ,  // readData (в рамках подключения) отправлен
  KLINE_ST_POLL_WAIT_READ,     // readData (обычный опрос) отправлен
  KLINE_ST_PROBE_GAP,          // пауза перед очередной пробой StartDiagnosticSession
  KLINE_ST_PROBE_WAIT,         // проба отправлена, ждём 0x50/0x7F/таймаут
  KLINE_ST_SPD_GAP,            // пауза перед очередной пробой смены скорости
  KLINE_ST_SPD_WAIT,           // проба смены скорости отправлена, ждём ответ
  KLINE_ST_EXT_WAIT,           // ReadMemoryByAddress (SID 0x23) отправлен
} kline_state_t;

// Доп. параметры (SID 0x23) читаются по очереди, не каждый цикл — они
// меняются медленно. Кэшируем и подмешиваем в каждый kline_data_set
// (иначе readData затирал бы их). Переменные объявлены выше (рядом с
// g_current_baud) — нужны в kline_parse_read_data, определённом раньше.
#define KLINE_AIRTEMP_INTERVAL_MS 1000

// Минимальная пауза между ответом ЭБУ и следующим запросом тестера
// (похоже на P3min из KWP2000). В блокирующей версии эта пауза возникала
// сама собой — kline_send_and_collect всегда "досиживал" полный
// delay(150), даже если ответ пришёл раньше, так что до readData
// проходило суммарно ~150 мс от отправки startComm. Здесь, раз мы
// выходим из ожидания сразу по получении нужных байт (быстрее 150 мс),
// ЭБУ не успевал "отдохнуть" и игнорировал readData — добавили
// отдельную явную паузу.
#define KLINE_INTER_MSG_GAP_MS 100

static kline_state_t g_state = KLINE_ST_IDLE;
static uint32_t g_state_started_ms = 0;
static uint8_t g_buf[64];
static size_t g_buf_n = 0;

// СКАН СКОРОСТЕЙ (2026-09-17) — по уточнённому плану пользователя после
// проверки документации по Январь-5/TRS251: 19200 скорее всего вообще
// не K-Line этого ЭБУ (в прошивках Январь встречается схема "USART0 =
// K-Line, 10400/38400/57600" отдельно от "USART1 = 19200 для цифрового
// подключения Innovate LC1" — то есть 19200 мы, похоже, пытались
// использовать не по адресу). Задокументированные для TRS251/Январь-5
// K-Line скорости — 38400 и 57600 (TRS251/251ALL в некоторых источниках
// упоминается устойчивым вплоть до ~45454 бод).
//
// Вместо смены скорости НА ЛЕТУ внутри уже установленной сессии (это мы
// уже пробовали через 0x26/0x10 — не сработало) — пробуем то, что
// предложил пользователь: ПОЛНАЯ переинициализация (stopComm+
// startComm+readData с нуля) на каждой скорости по очереди. Таймаут
// ответа (KLINE_RESPONSE_TIMEOUT_MS) на время скана НЕ трогаем — сначала
// нужно установить сам факт соединения на повышенной скорости, тюнинг
// таймингов имеет смысл только после этого.
static const uint32_t kScanBauds[] = {10400, 38400, 57600};
static int g_scan_index = -1; // -1 = скан не идёт
static bool g_scan_requested = false;

// K-LINE WAKE-UP ИМПУЛЬС (2026-09-18) — найден при разборе x86-кода
// СТАРОЙ версии инструмента "J5OLT" (D:\ЧипТюненх\3000\J5OLT\unpacked
// запускать этот.ExE — непакованный, в отличие от нового olt.dll из
// ИОН, поэтому статический разбор сработал). Перед стартом сессии на
// нужной скорости этот инструмент явно дёргает
// EscapeCommFunction(SETBREAK) -> Sleep(~25мс) ->
// EscapeCommFunction(CLRBREAK) — то есть тянет K-line к земле на 25мс
// и отпускает. Это классический wake-up pattern ISO14230 (W1), который
// наш код СЕЙЧАС НЕ делает — мы просто открываем UART на целевой
// скорости и сразу шлём stopComm/startComm байтами. Гипотеза: ЭБУ может
// игнорировать UART-байты на непривычной скорости (38400/57600) без
// этого физического пробуждения линии, из-за чего предыдущий скан
// скоростей (переинициализация без импульса) давал чистый FAIL.
//
// Добавлено ТОЛЬКО в ручной скан скоростей (кнопка на DEBUG), НЕ в
// фоновое автоподключение на 10400 — тот путь и так надёжно работает,
// трогать его незачем, а блокирующий delay() здесь (кнопка нажимается
// вручную, не часть цикла опроса) не мешает LVGL сильнее, чем уже
// существующий self-echo тест.
#define KLINE_WAKEUP_LOW_MS 25
#define KLINE_WAKEUP_HIGH_MS 25

static void kline_send_wakeup_pulse(void) {
  KlineSerial.flush();
  KlineSerial.end();
  pinMode(KLINE_TX_PIN, OUTPUT);
  digitalWrite(KLINE_TX_PIN, LOW);
  delay(KLINE_WAKEUP_LOW_MS);
  digitalWrite(KLINE_TX_PIN, HIGH);
  delay(KLINE_WAKEUP_HIGH_MS);
  g_current_baud = 0; // следующий kline_set_baud() обязан заново вызвать begin()
}

// IOControl OltPin SET (0x30, LID=0x0F, op=00) — см. комментарий у
// kline_send_wakeup_pulse. ПОДТВЕРЖДЕНО НА РЕАЛЬНОМ ЭБУ (2026-09-18):
// ЭБУ отвечает позитивно (0x70), в отличие от StartDiagnosticSession/
// SetDataRates (0x7F generalReject/serviceNotSupported). Блокирующая
// отправка (см. kline_send_wakeup_pulse — тот же довод про кнопку, не
// часть цикла опроса): шлём на ТЕКУЩЕЙ (10400) скорости, ответ не
// разбираем строго — сама попытка это часть эксперимента со сменой
// скорости, а не отдельная проверка.
static const uint8_t kOltPinSet[] = {0x83, 0x10, 0xF1, 0x30, 0x0F, 0x00, 0xC3};

static void kline_send_oltpin_set_blocking(void) {
  // Без этой паузы ЭБУ не успевает "отдохнуть" после предыдущего обмена
  // (тот же P3min-довод, что и у KLINE_INTER_MSG_GAP_MS в остальном
  // коде) — без неё ЭБУ вообще не отвечает (проверено 2026-09-18: RX
  // содержал только эхо своего же запроса, без ответа).
  delay(KLINE_INTER_MSG_GAP_MS);
  kline_send_only(kOltPinSet, sizeof(kOltPinSet));
  uint32_t start = millis();
  uint8_t buf[16];
  size_t n = 0;
  while (millis() - start < KLINE_RESPONSE_TIMEOUT_MS) {
    kline_collect_available(buf, &n, sizeof(buf));
  }
  Serial.print("[KL] OltPin SET перед сменой скорости: RX:");
  for (size_t i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
  Serial.println();
}

static void kline_scan_report(bool ok) {
  Serial.printf("[KL] scan @%lu: %s\n", (unsigned long)kScanBauds[g_scan_index],
                ok ? "SUCCESS" : "FAIL");

  g_scan_index++;
  if (g_scan_index >= (int)(sizeof(kScanBauds) / sizeof(kScanBauds[0]))) {
    Serial.println("[KL] scan: завершён, возвращаемся на 10400");
    g_scan_index = -1;
    kline_set_status("K-Line: скан скоростей завершён (см. Serial)",
                                   true);
    kline_set_baud(KLINE_BAUD);
    g_connected = false; // честно переподключаемся заново на 10400
    g_last_connect_attempt_ms = millis() - KLINE_RECONNECT_INTERVAL_MS - 1;
    g_state = KLINE_ST_IDLE;
    return;
  }

  // ЭКСПЕРИМЕНТ (2026-09-18): для скоростей ВЫШЕ 10400 сначала (пока
  // ещё на 10400) шлём подтверждённо принимаемую ЭБУ команду OltPin
  // SET, затем физический wake-up импульс — по гипотезе из разбора
  // J5OLT это и есть недостающий шаг перед переходом на 38400/57600.
  // На 10400 (первый элемент kScanBauds) ничего не меняем — этот путь
  // и так надёжно работает.
  if (kScanBauds[g_scan_index] != KLINE_BAUD) {
    // ЭБУ слушает OltPin-команды только на 10400 — если предыдущая
    // попытка скана уже переключила UART на другую скорость, нужно
    // сначала вернуться на 10400, иначе OltPin SET уходит "в пустоту"
    // (баг найден 2026-09-18: перед 57600 в логе был только эхо-ответ).
    kline_set_baud(KLINE_BAUD);
    kline_send_oltpin_set_blocking();
    kline_send_wakeup_pulse();
  }

  kline_set_baud(kScanBauds[g_scan_index]);
  Serial.printf("[KL] scan @%lu: старт\n", (unsigned long)kScanBauds[g_scan_index]);
  kline_send_only(kStopComm, sizeof(kStopComm));
  g_state_started_ms = millis();
  g_state = KLINE_ST_CONNECT_GAP;
}

static void kline_finish_connect_attempt(bool ok) {
  g_connected = ok;
  if (ok) {
    g_connect_fail_count = 0;
  } else if (g_connect_fail_count < 255) {
    g_connect_fail_count++;
  }
  kline_set_status(
      ok ? "K-Line OK (авто-опрос включен)"
         : "K-Line: жду ЭБУ (авто-переподключение)",
      ok);
  g_state = KLINE_ST_IDLE;
  g_last_connect_attempt_ms = g_state_started_ms; // от начала попытки, не конца
  g_last_poll_ms = g_state_started_ms;
}

// ============================================================
// РЕАЛЬНАЯ СМЕНА СКОРОСТИ (2026-09-18) — ПОДТВЕРЖДЕНО дизассемблированием
// j5TRS2431 (ближайший родственник TRS251, репозиторий cmgt/dm_bir_olt).
// Диспетчер сервисов (code_9EF4) при SID=0x10 идёт в обработчик
// code_A01B, который:
//   1. Требует установленной сессии связи (StartCommunication уже прошёл).
//   2. Требует param1 == 0x81.
//   3. param2 == 0x26 -> пишет S0RELL=0xF3 (38400);
//      param2 == 0x39 -> пишет S0RELL=0xF7 (57600).
//   4. Шлёт ПОЗИТИВНЫЙ ответ (0x50 = SID 0x10 + 0x40).
// Скорость на этом ядре (C509) задаётся генератором S0REL, а НЕ таймером
// TH1 (TH1/TL1 тут — системный тик 1мс; это была прежняя ошибка анализа).
// Новую скорость ЭБУ применяет через ~20мс ПОСЛЕ окончания передачи
// ответа (флаг взводится в конце TX в серийном прерывании RI0_TI0).
//
// Это ровно тот kSwitch38400, что раньше отключили как "ненадёжный" —
// теперь из кода ЭБУ видно, что кадр ПРАВИЛЬНЫЙ; прежняя ошибка была в
// отсутствии детекта именно 0x50 и, вероятно, в тайминге переключения
// нашего UART. 38400 — та же скорость, что прописана в настройках ИОН
// (InjOnl.ini, Speed=38400).
//
// Кадры (checksum = сумма байт % 256):
//   38400: 83 10 F1 10 81 26 3B
//   57600: 83 10 F1 10 81 39 4E
// ============================================================
static const uint8_t kSwitch38400[] = {0x83, 0x10, 0xF1, 0x10, 0x81, 0x26, 0x3B};
// StopDiagnosticSession (SID 0x20) — ОБЯЗАТЕЛЬНЫЙ шаг ПЕРЕД сменой
// скорости. Разбор реального дампа J5TRS251_VOLVO_ORIG.bin (2026-09-18)
// показал: обработчик смены скорости (SID 0x10, 0xA01E) требует бит
// 0x7E = СБРОШЕН, иначе шлёт шаблон 9DD4 = `83 F1 10 7F 10 10 23` (ровно
// тот generalReject, что мы получали!). А бит 0x7E взводят readData
// (0xA145) и IOControl/OltPin (0xA1E3) — то есть наш обычный опрос его
// уже поставил. Сбросить 0x7E умеет ТОЛЬКО StopDiagnosticSession (SID
// 0x20, 0xA068), при этом связь (бит 0x7D) остаётся. КРИТИЧНО: между
// StopDiagnosticSession и сменой скорости НЕЛЬЗЯ слать readData — он
// снова взведёт 0x7E.
static const uint8_t kStopDiag[] = {0x81, 0x10, 0xF1, 0x20, 0xA2};
static bool g_realswitch_requested = false;

// Блокирующий тест смены скорости (кнопка, когда уже подключены — не
// часть цикла опроса, delay() допустим, как и в self-echo/скане).
// Возвращает true, если на новой скорости пришёл валидный readData.
static bool kline_do_real_switch(uint32_t target_baud, const uint8_t *cmd) {
  Serial.printf("[KL] РЕАЛ. смена скорости на %lu...\n",
                (unsigned long)target_baud);

  kline_set_baud(KLINE_BAUD);

  // 0) StopDiagnosticSession (SID 0x20) — сбрасывает бит 0x7E, который
  //    взвёл наш readData/OltPin. Без этого смена скорости отвечает
  //    "ошибка сессии" 7F 10 10. После этого НЕ шлём readData до смены!
  uint8_t buf[48];
  size_t n = 0;
  uint32_t start;
  delay(KLINE_INTER_MSG_GAP_MS);
  kline_send_only(kStopDiag, sizeof(kStopDiag));
  n = 0;
  start = millis();
  while (millis() - start < 150) {
    kline_collect_available(buf, &n, sizeof(buf));
  }
  Serial.print("  StopDiag(0x20) RX:");
  for (size_t i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
  Serial.println();

  // 1) На 10400 шлём команду смены скорости, ждём позитив 0x50.
  delay(KLINE_INTER_MSG_GAP_MS);
  kline_send_only(cmd, 7);

  n = 0;
  start = millis();
  while (millis() - start < 200) {
    kline_collect_available(buf, &n, sizeof(buf));
  }
  Serial.print("  switch TX:");
  for (int i = 0; i < 7; i++) Serial.printf(" %02X", cmd[i]);
  Serial.print("  RX:");
  for (size_t i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
  Serial.println();

  // Позитив 0x50 ищем ПОСЛЕ 7 байт эха нашего запроса; заодно ловим 0x7F.
  bool accepted = false, negative = false;
  for (size_t i = 7; i < n; i++) {
    if (buf[i] == 0x50) accepted = true;
    if (buf[i] == 0x7F) negative = true;
  }
  if (!accepted) {
    Serial.printf("  -> НЕ принято (%s), остаёмся на 10400\n",
                  negative ? "0x7F негатив" : "нет 0x50");
    return false;
  }
  Serial.println("  -> ЭБУ принял (0x50). Ждём применения новой скорости ~40мс");

  // 2) ЭБУ применит новую скорость через ~20мс после ответа — ждём с запасом.
  delay(40);

  // 3) Переключаем свой UART и сразу проверяем связь readData на новой
  //    скорости (ЭБУ откатится на 10400 через ~5с без валидного обмена).
  kline_set_baud(target_baud);
  delay(KLINE_INTER_MSG_GAP_MS);
  kline_send_only(kReadData, sizeof(kReadData));
  n = 0;
  start = millis();
  while (millis() - start < 200) {
    kline_collect_available(buf, &n, sizeof(buf));
  }
  Serial.printf("  readData@%lu: n=%u buf:", (unsigned long)target_baud,
                (unsigned)n);
  for (size_t i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
  Serial.println();

  bool ok = (n >= 47 && buf[10] == 0x61);
  Serial.printf("  -> %s\n", ok ? "SUCCESS!!! связь на новой скорости"
                                 : "нет валидного ответа (0x61)");
  return ok;
}

static void kline_run_real_switch_test(void) {
  if (kline_do_real_switch(38400, kSwitch38400)) {
    // Успех — остаёмся на 38400, обычный опрос продолжится на этой
    // скорости (kline_set_baud уже переключил g_current_baud). Держим
    // связь регулярным опросом, иначе ЭБУ через ~5с вернётся на 10400.
    Serial.println("[KL] 38400 РАБОТАЕТ — остаёмся, авто-опрос на 38400");
    g_fast_active = true;
    g_connected = true;
    g_last_poll_ms = millis();
    kline_set_status("K-Line: 38400 SUCCESS!", true);
    g_state = KLINE_ST_IDLE;
    return;
  }
  // Не вышло — честно вернуться на 10400 и переподключиться.
  kline_set_baud(KLINE_BAUD);
  g_connected = false;
  g_last_connect_attempt_ms = millis() - KLINE_RECONNECT_INTERVAL_MS - 1;
  kline_set_status("K-Line: смена на 38400 не удалась (см. Serial)", false);
  g_state = KLINE_ST_IDLE;
}

#if KLINE_AUTO_FAST
// Автопереход на повышенную скорость сразу после успешного подключения на
// 10400. Вызывается ОДИН раз за подключение из стейт-машины. Блокирующий
// (~0.9с однократно) — на LVGL влияет только в момент коннекта, дальше
// опрос неблокирующий как обычно.
static void kline_auto_speed_up(void) {
  if (kline_do_real_switch(KLINE_FAST_BAUD, kSwitch38400)) {
    g_fast_active = true;
    g_connected = true;
    g_last_poll_ms = millis();
    kline_set_status("K-Line OK @38400 (авто-опрос)", true);
    return;
  }
  // Не удалось — вернуться на 10400 и честно переподключиться (ЭБУ, если
  // и успел переключиться, сам откатится на 10400 через ~5с).
  g_fast_active = false;
  kline_set_baud(KLINE_BAUD);
  g_connected = false;
  g_last_connect_attempt_ms = millis() - KLINE_RECONNECT_INTERVAL_MS - 1;
  kline_set_status("K-Line: быстрый режим не вышел, переподключаюсь", false);
}
#endif

// Кнопка на DEBUG. Если уже подключены — запускает СКАН СКОРОСТЕЙ
// (10400/38400/57600, см. kScanBauds выше) вместо переподключения:
// подключение и так рабочее, трогать его незачем, а вот проверить,
// принимает ли ЭБУ команду смены скорости — самое время. (Полный скан
// переинициализацией на 10400/38400/57600 уже отработал своё — дал
// чистый FAIL на 38400/57600, показав, что САМА инициализация жёстко
// привязана к 10400, код оставлен в файле, но на кнопку больше не
// повешен. Проба StartDiagnosticSession 0x10 БЕЗ параметра скорости
// тоже уже отработала — generalReject. См. docs/KLINE.md.) Если не
// подключены — обычная ручная диагностика: self-echo + немедленный
// запуск подключения (в обход таймера автоподключения).
static void kline_manual_test(void) {
  if (g_connected) {
    // Не дёргаем стейт-машину напрямую — если сейчас как раз в полёте
    // обычный опрос (KLINE_ST_POLL_WAIT_READ), резкая подмена состояния
    // перемешает байты старого ответа с новым (уже наступали на это).
    // Вместо этого просто ставим флаг — старт теста произойдёт safely
    // из KLINE_ST_IDLE, когда предыдущий обмен точно завершён.
    kline_set_status("K-Line: реал. смена на 38400 (см. Serial)...", true);
    g_realswitch_requested = true;
    return;
  }

  kline_set_status("K-Line: тест идёт...", false);
  lv_refr_now(NULL);

  kline_set_baud(KLINE_BAUD); // ручная диагностика всегда начинается с 10400

  if (!kline_self_echo_test()) {
    kline_set_status("K-Line: эха нет - проверь пайку/GND",
                                   false);
    return;
  }

  g_connected = false;
  g_state = KLINE_ST_IDLE;
  // Форсируем немедленную попытку на следующем тике kline_test_poll()
  // вместо ожидания KLINE_RECONNECT_INTERVAL_MS — безопасно относительно
  // переполнения millis(), тот же приём, что и везде в Arduino-коде.
  g_last_connect_attempt_ms = millis() - KLINE_RECONNECT_INTERVAL_MS - 1;
}

// Вызывать из loop() на каждой итерации, передавая millis(). Ничего не
// блокирует — каждый вызов это лёгкая проверка millis()/available().
void kline_test_poll(uint32_t now_ms) {
  switch (g_state) {
  case KLINE_ST_IDLE:
    if (g_connected && g_realswitch_requested) {
      g_realswitch_requested = false;
      if (g_fast_active) {
        // Уже на повышенной скорости (авто-режим) — повторная смена
        // сломала бы связь (ЭБУ уже не на 10400). Просто сообщаем.
        kline_set_status("K-Line: уже на 38400 (авто-режим)", true);
      } else {
        kline_run_real_switch_test();
      }
      return;
    }
#if KLINE_SPEED_EXPERIMENT
    if (g_connected && g_spd_requested) {
      g_spd_requested = false;
      g_spd_index = 0;
      g_state_started_ms = now_ms;
      g_state = KLINE_ST_SPD_GAP;
    } else if (g_connected && g_scan_requested) {
#else
    if (g_connected && g_scan_requested) {
#endif
      g_scan_requested = false;
      g_scan_index = 0;
      kline_set_baud(kScanBauds[g_scan_index]);
      Serial.printf("[KL] scan @%lu: старт\n",
                    (unsigned long)kScanBauds[g_scan_index]);
      kline_send_only(kStopComm, sizeof(kStopComm));
      g_state_started_ms = now_ms;
      g_state = KLINE_ST_CONNECT_GAP;
    } else if (g_connected && g_probe_requested) {
      g_probe_requested = false;
      g_probe_index = 0;
      g_state_started_ms = now_ms;
      g_state = KLINE_ST_PROBE_GAP;
    } else if (g_connected) {
      if (now_ms - g_last_poll_ms < KLINE_POLL_INTERVAL_MS) {
        return;
      }
      g_last_poll_ms = now_ms;
      // Раз в KLINE_AIRTEMP_INTERVAL_MS вместо обычного readData читаем
      // ОДИН доп. параметр из kExtReads (SID 0x23), по кругу — они
      // меняются медленно, пропуск одного readData раз в секунду незаметен.
      if (now_ms - g_last_airtemp_ms >= KLINE_AIRTEMP_INTERVAL_MS) {
        g_last_airtemp_ms = now_ms;
        const kline_ext_read_t *er = &kExtReads[g_ext_index];
        uint8_t frame[9];
        kline_build_readmem(er->hi, er->lo, er->len, frame);
        kline_send_only(frame, sizeof(frame));
        g_buf_n = 0;
        g_state_started_ms = now_ms;
        g_state = KLINE_ST_EXT_WAIT;
      } else {
        kline_send_only(kReadData, sizeof(kReadData));
        g_buf_n = 0;
        g_state_started_ms = now_ms;
        g_state = KLINE_ST_POLL_WAIT_READ;
      }
    } else {
      if (now_ms - g_last_connect_attempt_ms < KLINE_RECONNECT_INTERVAL_MS) {
        return;
      }
      // Свежее подключение всегда начинается на 10400, даже если до
      // обрыва связи мы успели переключиться на 38400.
      g_fast_active = false;
      g_air_temp_valid = false; // не показываем устаревшую температуру
      g_ext_valid = false;
      g_ext_index = 0;
      g_last_airtemp_ms = 0;    // прочитать доп. параметры сразу после коннекта
#if KLINE_AUTO_FAST
      // ВОССТАНОВЛЕНИЕ ПОСЛЕ "ЭБУ завис на 38400" (2026-09-18) — РЕДКОЕ,
      // не перед каждой попыткой! Если бы мы слали 38400-мусор перед
      // каждым коннектом, он ломал бы fast-init даже у ЭБУ, который уже
      // на 10400 (нарушается требование тишины на шине перед инициа-
      // лизацией) — из-за этого связь пропала совсем. Поэтому: обычные
      // попытки идут ЧИСТО на 10400; только если подряд не удалось
      // KLINE_FAST_RECOVER_AFTER раз — один раз шлём StopDiagnosticSession
      // на 38400 (сбрасывает делитель скорости ЭБУ на дефолт 10400) и
      // выходим БЕЗ немедленного коннекта: следующая штатная попытка
      // через KLINE_RECONNECT_INTERVAL_MS сделает чистый fast-init уже
      // после достаточного простоя шины.
      if (g_connect_fail_count >= KLINE_FAST_RECOVER_AFTER) {
        g_connect_fail_count = 0;
        kline_set_baud(KLINE_FAST_BAUD);
        kline_send_only(kStopDiag, sizeof(kStopDiag));
        delay(80); // ответ ЭБУ + применение отката (~20мс после ответа)
        kline_set_baud(KLINE_BAUD);
        g_last_connect_attempt_ms = now_ms; // ждём чистую попытку через 2с
        return;
      }
#endif
      kline_set_baud(KLINE_BAUD);
      kline_send_only(kStopComm, sizeof(kStopComm)); // ответ не важен
      g_state_started_ms = now_ms;
      g_state = KLINE_ST_CONNECT_GAP;
    }
    break;

  case KLINE_ST_CONNECT_GAP:
    if (now_ms - g_state_started_ms < 100) {
      return;
    }
    kline_send_only(kStartComm, sizeof(kStartComm));
    g_buf_n = 0;
    g_state_started_ms = now_ms;
    g_state = KLINE_ST_CONNECT_WAIT_START;
    break;

  case KLINE_ST_CONNECT_WAIT_START:
    kline_collect_available(g_buf, &g_buf_n, sizeof(g_buf));
    // Ждём эхо startComm (5 байт) + положительный ответ (обычно 7 байт).
    if (g_buf_n < 12 && (now_ms - g_state_started_ms) < KLINE_RESPONSE_TIMEOUT_MS) {
      return;
    }
    if (g_scan_index >= 0) {
      Serial.printf("[KL] scan @%lu startComm: n=%u buf:",
                    (unsigned long)kScanBauds[g_scan_index],
                    (unsigned)g_buf_n);
      for (size_t i = 0; i < g_buf_n; i++) {
        Serial.printf(" %02X", g_buf[i]);
      }
      Serial.println();
    }
    if (!kline_contains(g_buf, g_buf_n, 0xC1)) {
      if (g_scan_index >= 0) {
        kline_scan_report(false);
      } else {
        kline_finish_connect_attempt(false);
      }
      return;
    }
    g_state_started_ms = now_ms;
    g_state = KLINE_ST_CONNECT_GAP2;
    break;

  case KLINE_ST_CONNECT_GAP2:
    if (now_ms - g_state_started_ms < KLINE_INTER_MSG_GAP_MS) {
      return;
    }
    kline_send_only(kReadData, sizeof(kReadData));
    g_buf_n = 0;
    g_state_started_ms = now_ms;
    g_state = KLINE_ST_CONNECT_WAIT_READ;
    break;

  case KLINE_ST_CONNECT_WAIT_READ: {
    kline_collect_available(g_buf, &g_buf_n, sizeof(g_buf));
    if (g_buf_n < 47 && (now_ms - g_state_started_ms) < KLINE_RESPONSE_TIMEOUT_MS) {
      return;
    }
    kline_analyze_raw(g_buf, g_buf_n);
    kline_data_t kd = {0};
    bool ok = kline_parse_read_data(g_buf, g_buf_n, &kd);

    if (g_scan_index >= 0) {
      Serial.printf("[KL] scan @%lu readData: n=%u buf:",
                    (unsigned long)kScanBauds[g_scan_index],
                    (unsigned)g_buf_n);
      for (size_t i = 0; i < g_buf_n; i++) {
        Serial.printf(" %02X", g_buf[i]);
      }
      Serial.println();
      if (ok) {
        kline_data_set(&kd); // почему бы и не обновить приборку заодно
      }
      kline_scan_report(ok);
      return;
    }

    if (!ok) {
      kline_finish_connect_attempt(false);
      return;
    }
    kline_data_set(&kd);
    kline_finish_connect_attempt(true);
#if KLINE_AUTO_FAST
    // Сразу после успешного подключения на 10400 — автопереход на 38400
    // (рецепт подтверждён, см. docs/KLINE.md). Один раз за подключение;
    // при неудаче kline_auto_speed_up сам вернёт на 10400/переподключит.
    if (!g_fast_active) {
      kline_auto_speed_up();
    }
#endif
    break;
  }

  case KLINE_ST_POLL_WAIT_READ: {
    kline_collect_available(g_buf, &g_buf_n, sizeof(g_buf));
    if (g_buf_n < 47 && (now_ms - g_state_started_ms) < kline_poll_timeout_ms()) {
      return;
    }
    kline_analyze_raw(g_buf, g_buf_n);
    kline_data_t kd = {0};
    if (kline_parse_read_data(g_buf, g_buf_n, &kd)) {
      kline_data_set(&kd);
    } else {
      g_connected = false;
      kline_data_t empty = {0};
      kline_data_set(&empty);
      kline_set_status("K-Line: потеряна связь, переподключаюсь",
                                     false);
      g_last_connect_attempt_ms = now_ms;
    }
    g_state = KLINE_ST_IDLE;
    break;
  }

  case KLINE_ST_EXT_WAIT: {
    kline_collect_available(g_buf, &g_buf_n, sizeof(g_buf));
    const kline_ext_read_t *er = &kExtReads[g_ext_index];
    // Ответ: 9 байт эха запроса + ответ ЭБУ (0x63 + len байт + чексумма).
    size_t need = 9 + 3 + er->len; // эхо + [63 ..] с запасом
    if (g_buf_n < need && (now_ms - g_state_started_ms) < kline_poll_timeout_ms()) {
      return;
    }
    // Ищем 0x63 (=0x23+0x40, позитивный ReadMemoryByAddress) ПОСЛЕ эха.
    int d = -1;
    for (size_t i = 9; i + er->len < g_buf_n; i++) {
      if (g_buf[i] == 0x63) { d = (int)i + 1; break; }
    }
    if (d >= 0) {
      if (er->kind == 0) {
        // ДТВ: °C со смещением +40 (как ДТОЖ) — проверить по факту.
        g_air_temp_c = (int32_t)g_buf[d] - 40;
        g_air_temp_valid = true;
        Serial.printf("[KL] ДТВ(F885): raw=%u -> %ld C\n", g_buf[d],
                      (long)g_air_temp_c);
      } else if (er->kind == 1) {
        // Блок F99C..F99E: корр.ОЖ, корр.заряд, темп.заряда(+40).
        g_corr_coolant = g_buf[d];
        g_corr_charge = g_buf[d + 1];
        g_charge_temp_c = (int32_t)g_buf[d + 2] - 40;
        g_ext_valid = true;
        Serial.printf("[KL] коррЦН ОЖ=%u заряд=%u Tзаряда=%ld C (raw)\n",
                      g_buf[d], g_buf[d + 1], (long)g_charge_temp_c);
      } else if (er->kind == 2) {
        // Поправка ЦН (F942) — сырой байт, масштаб уточнить по факту.
        g_corr_cn = g_buf[d];
        Serial.printf("[KL] поправка ЦН(F942): raw=%u\n", g_buf[d]);
      }
    } else {
      Serial.printf("[KL] SID23 kind=%u: нет 0x63, RX:", er->kind);
      for (size_t i = 0; i < g_buf_n; i++) Serial.printf(" %02X", g_buf[i]);
      Serial.println();
    }
    // следующий доп. параметр в следующий слот
    g_ext_index = (g_ext_index + 1) % (int)(sizeof(kExtReads) / sizeof(kExtReads[0]));
    g_state = KLINE_ST_IDLE;
    break;
  }

  case KLINE_ST_PROBE_GAP: {
    if (now_ms - g_state_started_ms < KLINE_INTER_MSG_GAP_MS) {
      return;
    }
    uint8_t frame[6];
    kline_build_session_probe(kProbeSubfn[g_probe_index], frame);
    kline_send_only(frame, sizeof(frame));
    g_buf_n = 0;
    g_state_started_ms = now_ms;
    g_state = KLINE_ST_PROBE_WAIT;
    break;
  }

  case KLINE_ST_PROBE_WAIT: {
    kline_collect_available(g_buf, &g_buf_n, sizeof(g_buf));
    // В отличие от readData/startComm тут заранее НЕ известна точная
    // длина ответа — поэтому ждём весь таймаут целиком, а не выходим
    // по первому же байту сверх эха (раньше так и было — обрезало
    // ответ на середине, как только приходил хоть один байт).
    if ((now_ms - g_state_started_ms) < KLINE_RESPONSE_TIMEOUT_MS) {
      return;
    }

    uint8_t subfn = kProbeSubfn[g_probe_index];
    Serial.printf("[KL] probe 0x10 0x%02X: n=%u buf:", subfn,
                  (unsigned)g_buf_n);
    for (size_t i = 0; i < g_buf_n; i++) {
      Serial.printf(" %02X", g_buf[i]);
    }
    Serial.println();

    bool positive = kline_contains(g_buf, g_buf_n, 0x50);
    bool negative = kline_contains(g_buf, g_buf_n, 0x7F);
    Serial.printf("[KL] probe 0x10 0x%02X verdict: %s\n", subfn,
                  positive ? "ПОЗИТИВНЫЙ (0x50)"
                  : negative ? "негативный (0x7F)"
                             : "нет чёткого ответа (см. RAW выше)");

    g_probe_index++;
    if (g_probe_index >= (int)(sizeof(kProbeSubfn) / sizeof(kProbeSubfn[0]))) {
      g_probe_index = -1;
      kline_set_status(
          "K-Line: проба сессий завершена (см. Serial)", true);
      g_state = KLINE_ST_IDLE;
    } else {
      g_state_started_ms = now_ms;
      g_state = KLINE_ST_PROBE_GAP;
    }
    break;
  }

#if KLINE_SPEED_EXPERIMENT
  case KLINE_ST_SPD_GAP: {
    if (now_ms - g_state_started_ms < KLINE_INTER_MSG_GAP_MS) {
      return;
    }
    const kline_speed_probe_t *p = &kSpeedProbes[g_spd_index];
    uint8_t frame[8];
    size_t frame_len = kline_build_frame(frame, p->data, p->data_len);
    kline_send_only(frame, frame_len);
    memcpy(g_spd_tx, frame, frame_len);
    g_spd_tx_len = frame_len;
    g_buf_n = 0;
    g_state_started_ms = now_ms;
    g_state = KLINE_ST_SPD_WAIT;
    break;
  }

  case KLINE_ST_SPD_WAIT: {
    kline_collect_available(g_buf, &g_buf_n, sizeof(g_buf));
    if ((now_ms - g_state_started_ms) < KLINE_RESPONSE_TIMEOUT_MS) {
      return;
    }

    kline_speed_probe_report(g_spd_tx, g_spd_tx_len, g_buf, g_buf_n);

    g_spd_index++;
    if (g_spd_index >= (int)(sizeof(kSpeedProbes) / sizeof(kSpeedProbes[0]))) {
      g_spd_index = -1;
      // Пробы (включая OltPin SET/READ, подтверждённые 2026-09-18)
      // завершены — сразу же, без второго нажатия кнопки, запускаем
      // скан скоростей с wake-up импульсом (kline_scan_report), это и
      // есть проверка гипотезы целиком.
      kline_set_status(
          "K-Line: пробы OK, скан скоростей с wake-up...", true);
      g_scan_requested = true;
      g_state = KLINE_ST_IDLE;
    } else {
      g_state_started_ms = now_ms;
      g_state = KLINE_ST_SPD_GAP;
    }
    break;
  }
#endif // KLINE_SPEED_EXPERIMENT
  }
}

void kline_test_register(void) {
  kline_set_baud(KLINE_BAUD);
  screen_debug_set_kline_test_cb(kline_manual_test);
}
