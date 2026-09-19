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
#include <esp_heap_caps.h>
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

// Буферы отрисовки. Было: ОДИН буфер на 20 строк (12800 б) и вывод через
// блокирующий pushColors — процессор сам пропихивал каждый пиксель в SPI и
// всё это время не мог рисовать следующий кусок.
//
// Стало (2026-09-19, оптимизация отзывчивости):
//   * два буфера по 40 строк (320*40*2 = 25600 б каждый, 51200 б всего);
//   * вывод через DMA.
// Смысл двойного буфера именно в DMA: пока контроллер сам выпихивает в
// экран буфер A, LVGL уже рисует в буфер B. Корректность держится на том,
// что pushPixelsDMA НАЧИНАЕТ с dmaWait() — то есть следующая посылка ждёт
// завершения предыдущей, а буфер A переиспользуется только через одну
// посылку, когда его DMA гарантированно закончился.
// Памяти на плате 320 КБ, занято около трети — запас есть.
// Буферы берём ИЗ КУЧИ, а не статическим массивом: у ESP32 сегмент
// статических данных (dram0_0_seg) заметно меньше общего объёма RAM, и два
// буфера по 25 КБ в него уже не влезли (линковщик ругался на переполнение
// на 42 КБ). В куче место есть. heap_caps_malloc с MALLOC_CAP_DMA гарантирует
// память, пригодную для DMA.
// Если запрошенный размер не выделился — спускаемся по списку, вплоть до
// прежних 20 строк, чтобы прошивка стартовала в любом случае.
static const uint32_t LVGL_TRY_LINES[] = {40, 32, 24, 20, 12};
static lv_color_t *draw_buf1 = NULL;
static lv_color_t *draw_buf2 = NULL;
static size_t draw_buf_bytes = 0;

static void alloc_draw_buffers(void) {
  for (unsigned i = 0; i < sizeof(LVGL_TRY_LINES) / sizeof(LVGL_TRY_LINES[0]); i++) {
    size_t b = 320 * LVGL_TRY_LINES[i] * sizeof(lv_color_t);
    draw_buf1 = (lv_color_t *)heap_caps_malloc(b, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    draw_buf2 = (lv_color_t *)heap_caps_malloc(b, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (draw_buf1 && draw_buf2) {
      draw_buf_bytes = b;
      Serial.printf("[GFX] буферы отрисовки: 2 x %u строк (%u б), free heap %u\n",
                    (unsigned)LVGL_TRY_LINES[i], (unsigned)b,
                    (unsigned)ESP.getFreeHeap());
      return;
    }
    if (draw_buf1) { heap_caps_free(draw_buf1); draw_buf1 = NULL; }
    if (draw_buf2) { heap_caps_free(draw_buf2); draw_buf2 = NULL; }
  }
  Serial.println("[GFX] !!! не удалось выделить буферы отрисовки");
}

// LVGL зовёт эту функцию, когда нужно вывести на экран очередной
// прямоугольный кусок картинки (area) из подготовленного буфера px_map.
// Счётчики вывода — чтобы отделить «долго СЧИТАЕМ картинку» от «долго
// ПИХАЕМ её в экран». Если пикселей за кадр мало, а LVGL всё равно занят
// десятки миллисекунд, значит упираемся в расчёт (дуги), а не в шину.
volatile uint32_t g_flush_calls = 0;
volatile uint32_t g_flush_px = 0;
volatile uint32_t g_flush_us = 0;

void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  uint32_t fus = micros();
  g_flush_calls++;
  g_flush_px += w * h;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // Асинхронно: ставим посылку в DMA и СРАЗУ отпускаем LVGL рисовать
  // дальше во второй буфер. endWrite() тут звать НЕЛЬЗЯ — он дожидается
  // DMA и весь смысл перекрытия пропадает. Шину дисплея больше никто не
  // делит (тач сидит на отдельном SPI-периферале HSPI), поэтому держать
  // транзакцию открытой безопасно.
  tft.pushPixelsDMA((uint16_t *)px_map, w * h);
  g_flush_us += micros() - fus;
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
  // Увеличенный буфер передачи Serial. По умолчанию он маленький, и наши
  // отладочные строки ([TUNE]/[CAL]/[FRAME], около 450 байт раз в секунду)
  // переполняли его: printf вставал и ЖДАЛ, пока байты уползут в порт на
  // 115200 — это до 40 мс заморозки главного цикла каждую секунду, ровно
  // тот «рывок», который видно на экране. С буфером на 4 КБ строки просто
  // складываются и уходят фоном, цикл не стоит.
  Serial.setTxBufferSize(4096);
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

  // DMA для вывода в дисплей (см. lvgl_flush_cb). setSwapBytes(true) нужен
  // потому, что LVGL отдаёт RGB565 в порядке байтов процессора, а дисплею
  // нужен обратный — pushPixelsDMA сам переставит байты перед посылкой
  // (раньше это делал третий аргумент pushColors).
  tft.initDMA();
  tft.setSwapBytes(true);

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
  alloc_draw_buffers();
  lv_display_set_buffers(lv_disp, draw_buf1, draw_buf2, draw_buf_bytes,
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, lvgl_touch_read_cb);

  ui_demo_create();
  kline_test_register();

  // Serial.println("DISPLAY TEST OK");
  // Serial.println("========================");
}

// Замер отзывчивости. Раз в 5 секунд печатает, сколько времени съедают
// отрисовка и опрос ЭБУ и как часто прокручивается главный цикл. Нужен,
// чтобы «подтормаживает» стало числом, а не ощущением. Выключается одной
// строкой, если мешает.
#define LOOP_PROFILING 1

void loop() {
  static unsigned long lastTick = millis();
#if LOOP_PROFILING
  static uint32_t prof_last_ms = 0;
  static bool prof_started = false;
  static uint32_t prof_loops = 0;
  static uint32_t prof_lv_total = 0, prof_lv_max = 0;
  static uint32_t prof_kl_total = 0, prof_kl_max = 0;
#endif

  unsigned long now = millis();

  // LVGL нужно регулярно сообщать, сколько миллисекунд прошло,
  // и давать ему время на перерисовку/обработку таймеров.
  lv_tick_inc(now - lastTick);
  lastTick = now;

#if LOOP_PROFILING
  uint32_t t0 = micros();
  lv_timer_handler();
  uint32_t t1 = micros();
  kline_test_poll(now);
  uint32_t t2 = micros();

  uint32_t dlv = t1 - t0, dkl = t2 - t1;
  // Сколько пикселей ушло в дисплей именно в этом вызове LVGL. Если самый
  // долгий вызов рисует много — упираемся в отрисовку; если мало — тормоз
  // не в графике вообще.
  static uint32_t px_before = 0;
  uint32_t px_now = g_flush_px;
  uint32_t dpx = px_now - px_before;
  px_before = px_now;
  prof_loops++;
  prof_lv_total += dlv;
  prof_kl_total += dkl;
  static uint32_t prof_lv_max_px = 0;
  if (dlv > prof_lv_max) { prof_lv_max = dlv; prof_lv_max_px = dpx; }
  if (dkl > prof_kl_max) prof_kl_max = dkl;

  if (!prof_started) {
    prof_started = true;
    prof_last_ms = now;
  }
  if (now - prof_last_ms >= 5000) {
    uint32_t span = now - prof_last_ms;
    prof_last_ms = now;
    if (prof_loops) {
      Serial.printf("[PERF] циклов=%lu (%lu/с)  LVGL сред=%luмкс макс=%luмкс  "
                    "K-Line сред=%luмкс макс=%luмкс  "
                    "вывод: %lu кусков, %lu тыс.пикс, %lu мс  "
                    "в худшем вызове %lu пикс\n",
                    (unsigned long)prof_loops,
                    (unsigned long)(prof_loops * 1000UL / (span ? span : 1)),
                    (unsigned long)(prof_lv_total / prof_loops),
                    (unsigned long)prof_lv_max,
                    (unsigned long)(prof_kl_total / prof_loops),
                    (unsigned long)prof_kl_max,
                    (unsigned long)g_flush_calls,
                    (unsigned long)(g_flush_px / 1000),
                    (unsigned long)(g_flush_us / 1000),
                    (unsigned long)prof_lv_max_px);
    }
    prof_loops = 0;
    prof_lv_total = prof_lv_max = 0;
    prof_lv_max_px = 0;
    prof_kl_total = prof_kl_max = 0;
    g_flush_calls = 0;
    g_flush_px = 0;
    g_flush_us = 0;
  }
#else
  lv_timer_handler();
  kline_test_poll(now);
#endif
}
