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
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "ui_demo.h"

TFT_eSPI tft = TFT_eSPI();

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

void printChipInfo() {
  Serial.println("------------------------------");
  Serial.println("Chip info:");
  Serial.print("  Model: ");
  Serial.println(ESP.getChipModel());
  Serial.print("  Revision: ");
  Serial.println(ESP.getChipRevision());
  Serial.print("  Cores: ");
  Serial.println(ESP.getChipCores());
  Serial.print("  CPU Freq (MHz): ");
  Serial.println(ESP.getCpuFreqMHz());
  Serial.print("  Flash size (bytes): ");
  Serial.println(ESP.getFlashChipSize());
  Serial.print("  Free heap (bytes): ");
  Serial.println(ESP.getFreeHeap());
  Serial.println("------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("========================");
  Serial.println("ESP32-2432S028 TEST");
  Serial.println("========================");
  Serial.println("ESP32 STARTED");

  printChipInfo();

  // Подсветку включаем явно, отдельным пином (TFT_BL = GPIO21).
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1); // стандартная, задокументированная ориентация для ESP32-2432S028R (альбомная 320x240)

  // У клонов ILI9341 (см. комментарий про ILI9341_2_DRIVER в platformio.ini)
  // цвета часто инвертированы: чёрный фон рисуется белым. Включаем инверсию.
  tft.invertDisplay(true);

  Serial.println("DISPLAY INITIALIZED");
  Serial.print("  tft.width()  = ");
  Serial.println(tft.width());
  Serial.print("  tft.height() = ");
  Serial.println(tft.height());

  // --- Инициализация LVGL ---
  lv_init();

  lv_disp = lv_display_create(tft.width(), tft.height());
  lv_display_set_flush_cb(lv_disp, lvgl_flush_cb);
  lv_display_set_buffers(lv_disp, draw_buf, NULL, sizeof(draw_buf),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

  ui_demo_create();

  Serial.println("DISPLAY TEST OK");
  Serial.println("========================");
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

  ui_demo_update_uptime(now / 1000);

  if (now - lastBeat >= 3000) {
    lastBeat = now;
    Serial.println("...alive...");
  }
}
