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
#define KLINE_RESPONSE_TIMEOUT_MS 75
#define KLINE_POLL_INTERVAL_MS 300
#define KLINE_RECONNECT_INTERVAL_MS 2000

static HardwareSerial KlineSerial(2); // UART2
static bool g_connected = false;
static uint32_t g_last_poll_ms = 0;
static uint32_t g_last_connect_attempt_ms = 0;
static uint32_t g_current_baud = 0; // 0 = ещё не настроен

// Кадры и контрольные суммы — из задокументированного рабочего кода для
// Январь 7.2 (формат KWP2000, адрес ЭБУ 0x10, адрес тестера 0xF1).
static const uint8_t kStopComm[] = {0x81, 0x10, 0xF1, 0x82, 0x04};
static const uint8_t kStartComm[] = {0x81, 0x10, 0xF1, 0x81, 0x03};
static const uint8_t kReadData[] = {0x82, 0x10, 0xF1, 0x21, 0x01, 0xA5};
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
    Serial.println("K-Line RAW: пакет изменился относительно предыдущего");
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

// Разбирает ответ на readData по смещениям, подтверждённым на реальном
// ЭБУ (см. комментарий в шапке файла). Возвращает false, если в буфере
// нет ожидаемых байт (нет 0x61 или ответ короче нужного).
static bool kline_parse_read_data(const uint8_t *resp, size_t n,
                                   kline_data_t *out) {
  if (n <= 30 || resp[10] != 0x61) {
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
} kline_state_t;

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

static void kline_finish_connect_attempt(bool ok) {
  g_connected = ok;
  screen_debug_set_kline_result(
      ok ? "K-Line OK (авто-опрос включен)"
         : "K-Line: жду ЭБУ (авто-переподключение)",
      ok);
  g_state = KLINE_ST_IDLE;
  g_last_connect_attempt_ms = g_state_started_ms; // от начала попытки, не конца
  g_last_poll_ms = g_state_started_ms;
}

// Кнопка на DEBUG — ручная диагностика: self-echo + немедленный запуск
// подключения (в обход таймера автоподключения). Сам обмен всё равно
// идёт через общую неблокирующую стейт-машину ниже.
static void kline_manual_test(void) {
  screen_debug_set_kline_result("K-Line: тест идёт...", false);
  lv_refr_now(NULL);

  kline_set_baud(KLINE_BAUD); // ручная диагностика всегда начинается с 10400

  if (!kline_self_echo_test()) {
    screen_debug_set_kline_result("K-Line: эха нет - проверь пайку/GND",
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
    if (g_connected) {
      if (now_ms - g_last_poll_ms < KLINE_POLL_INTERVAL_MS) {
        return;
      }
      g_last_poll_ms = now_ms;
      kline_send_only(kReadData, sizeof(kReadData));
      g_buf_n = 0;
      g_state_started_ms = now_ms;
      g_state = KLINE_ST_POLL_WAIT_READ;
    } else {
      if (now_ms - g_last_connect_attempt_ms < KLINE_RECONNECT_INTERVAL_MS) {
        return;
      }
      // Свежее подключение всегда начинается на 10400, даже если до
      // обрыва связи мы успели переключиться на 38400.
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
    if (!kline_contains(g_buf, g_buf_n, 0xC1)) {
      kline_finish_connect_attempt(false);
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
    if (!ok) {
      kline_finish_connect_attempt(false);
      return;
    }
    kline_data_set(&kd);
    // Смена скорости на 38400 ПОПРОБОВАНА и ОТКЛЮЧЕНА (2026-09-17): не
    // было надёжного признака, что ЭБУ реально принял переход — по факту
    // ловили ложный "успех", сами переключались на 38400, а ЭБУ молча
    // оставался на 10400 → следующий опрос не долетал → обрыв связи →
    // переподключение на 10400 → успех → снова ложное переключение →
    // бесконечный цикл переподключений. Остаёмся на 10400, пока не
    // найдём задокументированный/проверенный признак подтверждения
    // смены скорости для ЭТОГО ЭБУ. См. docs/KLINE.md.
    kline_finish_connect_attempt(true);
    break;
  }

  case KLINE_ST_POLL_WAIT_READ: {
    kline_collect_available(g_buf, &g_buf_n, sizeof(g_buf));
    if (g_buf_n < 47 && (now_ms - g_state_started_ms) < KLINE_RESPONSE_TIMEOUT_MS) {
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
      screen_debug_set_kline_result("K-Line: потеряна связь, переподключаюсь",
                                     false);
      g_last_connect_attempt_ms = now_ms;
    }
    g_state = KLINE_ST_IDLE;
    break;
  }
  }
}

void kline_test_register(void) {
  kline_set_baud(KLINE_BAUD);
  screen_debug_set_kline_test_cb(kline_manual_test);
}
