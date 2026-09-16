// ============================================================
// Тест K-Line — попытка достучаться до ЭБУ Январь 5.1.61 через
// донорскую K-Line плату на LM339.
//
// Физика (плата ESP32-2432S028 / CYD):
//   Разъём P5 (VIN-TX-RX-GND) НЕЛЬЗЯ использовать — электрически это
//   GPIO1/GPIO3, то есть тот же UART0, что и USB-Serial (CH340). Разъём
//   P3 даёт только один свободный пин (второй занят подсветкой
//   TFT_BL=GPIO21). Используем разъём CN1 (GND-22-27-3V3):
//     GPIO22 -> TX (в K-Line/донорскую плату)
//     GPIO27 -> RX (из K-Line/донорской платы)
//   Полярность подтверждена предыдущими прогонами теста (self-echo
//   проходит только на прямой логике, без инверсии).
//
// ПРОТОКОЛ — ISO 14230 (KWP2000), "быстрая инициализация":
//   Взято из задокументированного рабочего кода для ЭБУ Январь 7.2
//   (тот же протокол у семейства Январь/Bosch Motronic с K-Line).
//   В отличие от ISO9141 "медленной" (5-бод) инициализации, которую
//   пробовали раньше, здесь НЕ нужен ни адресный байт на 5 бод, ни
//   L-line — порт просто открывается сразу на 10400 бод, и шлётся
//   команда startCommunication. Формат кадра: [длина][адрес ЭБУ][адрес
//   тестера][сервис/данные...][контрольная сумма = сумма всех
//   предыдущих байт по модулю 256]:
//     startCommunication = 81 10 F1 81 03
//     readData (RLI_ASS)  = 82 10 F1 21 01 A5
//   K-Line — однопроводная шина: в ответ сначала приходит ЭХО наших же
//   переданных байт, а затем настоящий ответ ЭБУ. Успешный ответ на
//   startCommunication содержит байт 0xC1 (позитивный ответ на сервис
//   0x81); успешный ответ на readData содержит байт 0x61 (позитивный
//   ответ на сервис 0x21).
//   Точные смещения байт данных внутри readData-ответа зависят от
//   конкретной прошивки ЭБУ (у Январь 7.2 и Bosch 7.9.7 они уже
//   отличаются, см. источник) — на этом этапе тест только проверяет
//   сам факт ответа (0xC1 / 0x61) и печатает сырые байты в Serial для
//   дальнейшего разбора, а не пытается парсить конкретные параметры.
// ============================================================

#include <Arduino.h>
#include <lvgl.h>
#include "kline_test.h"
#include "screen_debug.h"

#define KLINE_TX_PIN 22
#define KLINE_RX_PIN 27
#define KLINE_BAUD 10400

static HardwareSerial KlineSerial(2); // UART2

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
// K-Line однопроводная — при исправной цепи это всегда должно быть true.
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

static void kline_run_test(void) {
  screen_debug_set_kline_result("K-Line: тест идёт...", false);
  lv_refr_now(NULL); // показать статус до блокирующего обмена

  pinMode(KLINE_RX_PIN, INPUT_PULLUP);

  KlineSerial.end();
  KlineSerial.begin(KLINE_BAUD, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN, false);
  delay(50);

  char buf[96];

  Serial.println("K-Line: старт теста (KWP2000, быстрая инициализация)");
  Serial.println("K-Line: шаг 0 — self-echo (sanity check)");
  if (!kline_self_echo_test()) {
    snprintf(buf, sizeof(buf), "K-Line: эха нет - проверь пайку/GND");
    screen_debug_set_kline_result(buf, false);
    Serial.println("K-Line: эха нет — раньше было, проверь контакт/GND.");
    return;
  }

  // Кадры и контрольные суммы взяты из задокументированного рабочего
  // кода для Январь 7.2 (тот же формат KWP2000, адрес ЭБУ 0x10, адрес
  // тестера 0xF1).
  static const uint8_t stopComm[] = {0x81, 0x10, 0xF1, 0x82, 0x04};
  static const uint8_t startComm[] = {0x81, 0x10, 0xF1, 0x81, 0x03};
  static const uint8_t readData[] = {0x82, 0x10, 0xF1, 0x21, 0x01, 0xA5};

  uint8_t resp[64];

  Serial.println("K-Line: шаг 1 — stopCommunication (на всякий случай, "
                  "ответ не проверяем)");
  size_t n0 = kline_send_and_collect(stopComm, sizeof(stopComm), 100, resp,
                                      sizeof(resp));
  kline_dump_hex("  ответ на stopCommunication", resp, n0);

  Serial.println("K-Line: шаг 2 — startCommunication");
  size_t n1 = kline_send_and_collect(startComm, sizeof(startComm), 150, resp,
                                      sizeof(resp));
  kline_dump_hex("  ответ на startCommunication", resp, n1);

  if (!kline_contains(resp, n1, 0xC1)) {
    snprintf(buf, sizeof(buf), "K-Line: нет 0xC1, эхо=%u байт", (unsigned)n1);
    screen_debug_set_kline_result(buf, false);
    Serial.println("K-Line: 0xC1 не пришёл — ЭБУ не подтвердил "
                    "startCommunication. Полные байты см. выше.");
    return;
  }

  Serial.println("K-Line: startCommunication OK (0xC1 получен). "
                  "Шаг 3 — readData (RLI_ASS)");
  size_t n2 = kline_send_and_collect(readData, sizeof(readData), 150, resp,
                                      sizeof(resp));
  kline_dump_hex("  ответ на readData", resp, n2);

  if (kline_contains(resp, n2, 0x61)) {
    snprintf(buf, sizeof(buf), "K-Line OK: 0xC1+0x61, данные %u байт",
             (unsigned)n2);
    screen_debug_set_kline_result(buf, true);
  } else {
    snprintf(buf, sizeof(buf), "K-Line: 0xC1 есть, но нет 0x61 (см. Serial)");
    screen_debug_set_kline_result(buf, true);
  }
}

void kline_test_register(void) {
  pinMode(KLINE_RX_PIN, INPUT_PULLUP);
  screen_debug_set_kline_test_cb(kline_run_test);
}
