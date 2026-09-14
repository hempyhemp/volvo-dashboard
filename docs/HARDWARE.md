# Hardware — TENSTAR ROBOT ESP32-2432S028

Плата известна в сообществе как "Cheap Yellow Display" (CYD). Пины ниже
подтверждены множеством независимых источников (witnessmenow/ESP32-Cheap-
Yellow-Display, rzeldent/esp32-smartdisplay, стандартный `User_Setup` для
TFT_eSPI под эту плату) — расхождений между источниками по TFT-пинам нет.

## Аппаратные ревизии платы

| Ревизия | Тач-контроллер | Комментарий |
|---|---|---|
| **ESP32-2432S028R** (используется в этом проекте) | XPT2046, резистивный, SPI | Стандартная жёлтая плата TENSTAR ROBOT, самая массовая |
| Редкий клон с ёмкостным тачем | GT911, I2C | Другой шлейф тача, другие пины — НЕ наш случай |

## Сводная таблица

| Device         | Model      | Interface | GPIO                                  | Status    |
| -------------- | ---------- | --------- | -------------------------------------- | --------- |
| MCU            | ESP32      | —         | —                                       | confirmed |
| TFT            | ILI9341    | SPI       | MISO=12, MOSI=13, SCLK=14, CS=15, DC=2, RST=-1 | confirmed |
| TFT resolution | 320x240    | —         | —                                       | confirmed |
| Touch          | XPT2046    | SPI (общая шина с TFT) | CS=33, IRQ=36           | not used  |
| Backlight      | TFT backlight | GPIO   | 21                                      | confirmed |
| K-Line         | future     | —         | —                                       | not connected |
| ECU            | Январь 5.1 | K-Line    | future                                  | future    |

## Примечания

* `TFT_RST = -1` — отдельный пин сброса не используется, дисплей сбрасывается
  вместе с общим сбросом платы (EN).
* Подсветка (`GPIO21`) управляется отдельно от инициализации TFT_eSPI —
  в `main.cpp` она включается явно через `pinMode`/`digitalWrite`.
* SPI-шина (MISO/MOSI/SCLK) общая для TFT и тач-контроллера, у каждого
  свой `CS`. SD-карта на этой плате тоже висит на этой же физической
  SPI-шине со своим CS = GPIO5 (в проекте пока не используется).
* Тач (XPT2046) и SD-карта на этом этапе не подключаются в коде —
  только TFT.
