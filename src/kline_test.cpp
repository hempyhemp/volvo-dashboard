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
// ============================================================

#include <Arduino.h>
#include <lvgl.h>
#include "kline_test.h"
#include "screen_debug.h"
#include "kline_data.h"

#define KLINE_TX_PIN 22
#define KLINE_RX_PIN 27
#define KLINE_BAUD 10400
#define KLINE_POLL_INTERVAL_MS 300
#define KLINE_RECONNECT_INTERVAL_MS 2000

static HardwareSerial KlineSerial(2); // UART2
static bool g_connected = false;
static uint32_t g_last_poll_ms = 0;
static uint32_t g_last_connect_attempt_ms = 0;
static bool g_serial_started = false;

// Кадры и контрольные суммы — из задокументированного рабочего кода для
// Январь 7.2 (формат KWP2000, адрес ЭБУ 0x10, адрес тестера 0xF1).
static const uint8_t kStopComm[] = {0x81, 0x10, 0xF1, 0x82, 0x04};
static const uint8_t kStartComm[] = {0x81, 0x10, 0xF1, 0x81, 0x03};
static const uint8_t kReadData[] = {0x82, 0x10, 0xF1, 0x21, 0x01, 0xA5};

static void kline_ensure_serial_started(void) {
  if (g_serial_started) {
    return;
  }
  pinMode(KLINE_RX_PIN, INPUT_PULLUP);
  KlineSerial.begin(KLINE_BAUD, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN, false);
  g_serial_started = true;
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
  Serial.printf("  self-echo: %s (получено 0x%02X)\n",
                ok ? "ЕСТЬ эхо" : "нет эха", got);
  return ok;
}

// Отправляет команду, ждёт wait_ms и забирает всё, что пришло за это
// время (включая эхо собственной передачи), в out (макс. max_out байт).
static size_t kline_send_and_collect(const uint8_t *cmd, size_t len,
                                      uint32_t wait_ms, uint8_t *out,
                                      size_t max_out) {
  while (KlineSerial.available()) {
    KlineSerial.read(); // сброс мусора перед отправкой
  }
  KlineSerial.write(cmd, len);
  KlineSerial.flush();
  delay(wait_ms);

  size_t n = 0;
  while (KlineSerial.available() && n < max_out) {
    out[n++] = (uint8_t)KlineSerial.read();
  }
  return n;
}

static void kline_dump_hex(const char *label, const uint8_t *buf, size_t n) {
  Serial.print(label);
  Serial.print(" (");
  Serial.print(n);
  Serial.print(" байт): ");
  for (size_t i = 0; i < n; i++) {
    Serial.printf("%02X ", buf[i]);
  }
  Serial.println();
}

static bool kline_contains(const uint8_t *buf, size_t n, uint8_t value) {
  for (size_t i = 0; i < n; i++) {
    if (buf[i] == value) return true;
  }
  return false;
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
  return true;
}

// Одна попытка подключения: startCommunication + первый readData. Не
// блокирует дольше ~300 мс. Возвращает true при успехе.
static bool kline_try_connect(void) {
  uint8_t resp[64];

  kline_send_and_collect(kStopComm, sizeof(kStopComm), 100, resp,
                          sizeof(resp)); // на всякий случай, ответ не важен

  size_t n1 = kline_send_and_collect(kStartComm, sizeof(kStartComm), 150,
                                      resp, sizeof(resp));
  if (!kline_contains(resp, n1, 0xC1)) {
    return false;
  }

  size_t n2 = kline_send_and_collect(kReadData, sizeof(kReadData), 150, resp,
                                      sizeof(resp));
  kline_dump_hex("K-Line: подключились, ответ на readData", resp, n2);

  kline_data_t kd = {0};
  if (!kline_parse_read_data(resp, n2, &kd)) {
    return false;
  }

  kline_data_set(&kd);
  return true;
}

// Кнопка на DEBUG — ручная диагностика: self-echo + немедленная попытка
// подключения (в обход таймера автоподключения).
static void kline_manual_test(void) {
  screen_debug_set_kline_result("K-Line: тест идёт...", false);
  lv_refr_now(NULL);

  kline_ensure_serial_started();

  Serial.println("K-Line: ручной тест — self-echo");
  if (!kline_self_echo_test()) {
    screen_debug_set_kline_result("K-Line: эха нет - проверь пайку/GND",
                                   false);
    return;
  }

  g_last_connect_attempt_ms = millis();
  if (kline_try_connect()) {
    g_connected = true;
    g_last_poll_ms = millis();
    screen_debug_set_kline_result("K-Line OK (авто-опрос включен)", true);
  } else {
    g_connected = false;
    screen_debug_set_kline_result("K-Line: эхо есть, ЭБУ не ответил", false);
  }
}

// Вызывать из loop() на каждой итерации, передавая millis(). Без
// подключения — пробует подключиться раз в KLINE_RECONNECT_INTERVAL_MS;
// после подключения — опрашивает раз в KLINE_POLL_INTERVAL_MS. Каждая
// попытка/опрос блокирует до ~300-400 мс — известный компромисс для
// текущего этапа, LVGL на это время подвиснет.
void kline_test_poll(uint32_t now_ms) {
  kline_ensure_serial_started();

  if (!g_connected) {
    if (now_ms - g_last_connect_attempt_ms < KLINE_RECONNECT_INTERVAL_MS) {
      return;
    }
    g_last_connect_attempt_ms = now_ms;

    if (kline_try_connect()) {
      g_connected = true;
      g_last_poll_ms = now_ms;
      screen_debug_set_kline_result("K-Line OK (авто-опрос включен)", true);
    } else {
      screen_debug_set_kline_result("K-Line: жду ЭБУ (авто-переподключение)",
                                     false);
    }
    return;
  }

  if (now_ms - g_last_poll_ms < KLINE_POLL_INTERVAL_MS) {
    return;
  }
  g_last_poll_ms = now_ms;

  uint8_t resp[64];
  size_t n = kline_send_and_collect(kReadData, sizeof(kReadData), 150, resp,
                                     sizeof(resp));

  kline_data_t kd = {0};
  if (!kline_parse_read_data(resp, n, &kd)) {
    g_connected = false;
    kline_data_t empty = {0};
    kline_data_set(&empty);
    screen_debug_set_kline_result("K-Line: потеряна связь, переподключаюсь",
                                   false);
    return;
  }

  kline_data_set(&kd);
}

void kline_test_register(void) {
  kline_ensure_serial_started();
  screen_debug_set_kline_test_cb(kline_manual_test);
}
