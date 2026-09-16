// ============================================================
// ЭТАП 3: Экран через LVGL (вместо прямого рисования в TFT_eSPI)
// Плата: TENSTAR ROBOT ESP32-2432S028 (Cheap Yellow Display)
// TFT:   ILI9341-клон, 320x240, SPI (см. platformio.ini для пинов)
//
// Сам интерфейс (lib/ui_demo) не знает про железо — тот же код
// используется в PC-симуляторе (pc-sim/). Здесь — только "мост"
// между LVGL и TFT_eSPI: инициализация экрана и функция flush,
// которая копирует то, что нарисовал LVGL, на реальный дисплей.
//
// K-Line / ЭБУ Январь 5.1 на этом этапе НЕ используются.
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include "ui_demo.h"
#include "kline_test.h"

TFT_eSPI tft = TFT_eSPI();

// Тач XPT2046 на этой плате — на ОТДЕЛЬНОЙ от дисплея SPI-шине (не на тех
// же MISO/MOSI/SCLK, что TFT). Раньше пробовали через встроенный тач
// TFT_eSPI на пинах дисплея — сырые данные были всегда нулевые (см.
// docs/HARDWARE.md), хотя заводская демо-прошивка тач видела нормально.
// Значит дело не в железе, а в том, что читали не с тех ножек чипа.
#define TOUCH_CLK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS_PIN 33
#define TOUCH_IRQ 36

// TFT_eSPI на ESP32 по умолчанию занимает аппаратный VSPI — используем
// HSPI для тача, чтобы не делить один и тот же SPI-периферал на две
// разные шины (у тача и так физически свои пины).
SPIClass touchSPI = SPIClass(HSPI);
XPT2046_Touchscreen touchscreen(TOUCH_CS_PIN, TOUCH_IRQ);

static lv_display_t *lv_disp;

// Буфер отрисовки — не весь экран целиком, а часть (по строкам),
// так экономим RAM. 320 (ширина) * 20 строк * 2 байта (RGB565) = 12800 байт.
static const uint32_t LVGL_BUF_LINES = 20;
static lv_color_t draw_buf[320 * LVGL_BUF_LINES];

// LVGL зовёт эту функцию, когда нужно вывести на экран очередной
// прямоугольный кусок картинки (area) из подготовленного буфера px_map.
void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)px_map, w * h, true);
  tft.endWrite();

  lv_display_flush_ready(disp);
}

void setupTouch() {
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS_PIN);
  touchscreen.begin(touchSPI);
  touchscreen.setRotation(1); // должно совпадать с tft.setRotation(1)
}

// Сырые координаты XPT2046 (примерно 0..4095 по каждой оси) нужно
// смасштабировать в пиксели экрана (320x240). TOUCH_RAW_* — типовые
// границы для панелей этой серии плат (широко задокументированы для
// ESP32-2432S028), но точное значение зависит от конкретного экземпляра
// сенсора. Если координаты касания ощутимо "мимо" — сверьтесь с сырыми
// значениями в Serial Monitor (печатаются ниже) и подправьте эти границы.
static const int TOUCH_RAW_X_MIN = 200;
static const int TOUCH_RAW_X_MAX = 3700;
static const int TOUCH_RAW_Y_MIN = 240;
static const int TOUCH_RAW_Y_MAX = 3800;

// LVGL зовёт эту функцию, чтобы узнать, есть ли сейчас касание экрана.
void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();

    int x = constrain(map(p.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, tft.width()), 0, tft.width() - 1);
    int y = constrain(map(p.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, tft.height()), 0, tft.height() - 1);

    // Закомментировано на время reverse-engineering K-Line (см.
    // docs/KLINE.md) — не мешаем Serial Monitor лишним выводом.
    // Serial.print("touch raw: x=");
    // Serial.print(p.x);
    // Serial.print(" y=");
    // Serial.print(p.y);
    // Serial.print(" z=");
    // Serial.print(p.z);
    // Serial.print("  -> screen: x=");
    // Serial.print(x);
    // Serial.print(" y=");
    // Serial.println(y);

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void printChipInfo() {
  // Закомментировано на время reverse-engineering K-Line (см.
  // docs/KLINE.md) — не мешаем Serial Monitor лишним выводом.
  // Serial.println("------------------------------");
  // Serial.println("Chip info:");
  // Serial.print("  Model: ");
  // Serial.println(ESP.getChipModel());
  // Serial.print("  Revision: ");
  // Serial.println(ESP.getChipRevision());
  // Serial.print("  Cores: ");
  // Serial.println(ESP.getChipCores());
  // Serial.print("  CPU Freq (MHz): ");
  // Serial.println(ESP.getCpuFreqMHz());
  // Serial.print("  Flash size (bytes): ");
  // Serial.println(ESP.getFlashChipSize());
  // Serial.print("  Free heap (bytes): ");
  // Serial.println(ESP.getFreeHeap());
  // Serial.println("------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  // Закомментировано на время reverse-engineering K-Line (см.
  // docs/KLINE.md) — не мешаем Serial Monitor лишним выводом.
  // Serial.println("========================");
  // Serial.println("ESP32-2432S028 TEST");
  // Serial.println("========================");
  // Serial.println("ESP32 STARTED");

  // printChipInfo();

  // Подсветку включаем явно, отдельным пином (TFT_BL = GPIO21).
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1); // стандартная, задокументированная ориентация для ESP32-2432S028R (альбомная 320x240)

  // У клонов ILI9341 (см. комментарий про ILI9341_2_DRIVER в platformio.ini)
  // цвета часто инвертированы: чёрный фон рисуется белым. Включаем инверсию.
  tft.invertDisplay(true);

  // Serial.println("DISPLAY INITIALIZED");
  // Serial.print("  tft.width()  = ");
  // Serial.println(tft.width());
  // Serial.print("  tft.height() = ");
  // Serial.println(tft.height());

  setupTouch();
  // Serial.println("TOUCH INITIALIZED");

  // --- Инициализация LVGL ---
  lv_init();

  lv_disp = lv_display_create(tft.width(), tft.height());
  lv_display_set_flush_cb(lv_disp, lvgl_flush_cb);
  lv_display_set_buffers(lv_disp, draw_buf, NULL, sizeof(draw_buf),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, lvgl_touch_read_cb);

  ui_demo_create();
  kline_test_register();

  // Serial.println("DISPLAY TEST OK");
  // Serial.println("========================");
}

void loop() {
  static unsigned long lastBeat = 0;
  static unsigned long lastTick = millis();

  unsigned long now = millis();

  // LVGL нужно регулярно сообщать, сколько миллисекунд прошло,
  // и давать ему время на перерисовку/обработку таймеров.
  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();

  kline_test_poll(now);

  if (now - lastBeat >= 3000) {
    lastBeat = now;
    // Serial.println("...alive...");
  }
}
