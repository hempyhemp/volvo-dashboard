# ESP32-2432S028 Project

Hardware:

* Board: TENSTAR ROBOT ESP32-2432S028
* Color TFT: 2.8"
* Resolution: 320x240
* Board color: yellow
* MCU: ESP32
* Display controller: ILI9341-клон, требует ILI9341_2_DRIVER в TFT_eSPI (см. platformio.ini)
* Display interface: SPI
* Touch: XPT2046, резистивный, SPI — подключён и используется (LVGL
  pointer indev, `setupTouch()` в `src/main.cpp`; калибровка сохраняется
  в NVS через `Preferences`, см. docs/HARDWARE.md)

UI development:
* Экран полностью на LVGL (v9.5.0) — и в прошивке ESP32, и в PC-симуляторе
* Есть PC-симулятор (`pc-sim/`, LVGL + SDL2 + CMake/MSVC) — позволяет
  разрабатывать и смотреть UI на компьютере без прошивки платы
* Общий код интерфейса — `lib/ui_demo/` (стандартная папка PlatformIO для
  локальных библиотек; `pc-sim/CMakeLists.txt` ссылается на те же файлы
  по относительному пути `../lib/ui_demo/`). Одна копия кода, используется
  и платой, и симулятором
* Структура: `ui_demo.c` — переключатель вкладок (lv_tabview, тёмная тема);
  `screen_debug.c` — техническая проверка (кириллица, аптайм);
  `screen_online.c` — черновик приборки (моковые данные, дуги/цифры,
  фон-фото `lib/ui_demo/images/img_cat_bg.c`, сгенерировано из
  `assets/fon.jpg`)
* Кириллица под LVGL — через кастомный шрифт `lib/ui_demo/fonts/`,
  сгенерирован `npx lv_font_conv` из Segoe UI (встроенные шрифты LVGL
  кириллицу не содержат)

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
LVGL-интерфейс с тач-управлением на плате и в PC-симуляторе. Вкладка
ONLINE — черновик приборки с моковыми данными (K-Line ещё не подключён).

Do NOT implement K-Line yet.
