# ESP32-2432S028 Project

Hardware:

* Board: TENSTAR ROBOT ESP32-2432S028
* Color TFT: 2.8"
* Resolution: 320x240
* Board color: yellow
* MCU: ESP32
* Display controller: ILI9341-клон, требует ILI9341_2_DRIVER в TFT_eSPI (см. platformio.ini)
* Display interface: SPI
* Touch: присутствует (XPT2046, резистивный, SPI), но пока не используется

UI development:
* Переезжаем на LVGL для верстки интерфейса приборки
* Есть PC-симулятор (`pc-sim/`, LVGL + SDL2 + CMake/MSVC) — позволяет
  разрабатывать и смотреть UI на компьютере без прошивки платы
* Общий код интерфейса (`pc-sim/ui/ui_demo.c`) не завязан на железо —
  тот же подход будет использоваться и в прошивке ESP32 после переноса
  экрана на LVGL там (сейчас прошивка ещё рисует напрямую через
  TFT_eSPI + U8g2_for_TFT_eSPI, не через LVGL)

Project purpose:
Автомобильная приборная панель на ESP32.

Future stages:

1. ESP32 firmware
2. TFT display
3. UI приборной панели
4. K-Line interface
5. Январь 5.1 ECU communication
6. Reading ECU parameters
7. Displaying automotive data

Current stage:
Базовая проверка ESP32 + TFT (Serial + вывод текста на экран). Пройдена.

Do NOT implement K-Line yet.
