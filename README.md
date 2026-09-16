# ESP32-2432S028 — автомобильная приборка (Январь 5.1.61, K-Line)

Проект PlatformIO для платы **TENSTAR ROBOT ESP32-2432S028** (жёлтая
плата с встроенным TFT-дисплеем 2.8", 320x240, ILI9341) — приборка,
читающая параметры ЭБУ **Январь 5.1.61** по K-Line.

Экран на плате рисует LVGL (через мост в `src/main.cpp` на базе
`TFT_eSPI`), сам интерфейс — в `lib/ui_demo/`. Две вкладки:
**DEBUG** (статус подключения K-Line + живые данные RPM/темп/дроссель/
напряжение) и **ONLINE** (черновик приборки).

K-Line **подключён и работает** (протокол, распиновка, статус каждого
параметра — см. [`docs/KLINE.md`](./docs/KLINE.md)). Общий обзор проекта
— [`PROJECT_CONTEXT.md`](./PROJECT_CONTEXT.md), распиновка платы —
[`docs/HARDWARE.md`](./docs/HARDWARE.md).

На этой плате COM-порт обычно **COM1** (чип CH340) — в примерах ниже
подставьте свой порт (`pio device list` покажет актуальный).

## PC-симулятор интерфейса (без платы)

Разрабатывать и смотреть UI можно прямо на компьютере, без прошивки —
см. [`pc-sim/README.md`](./pc-sim/README.md). Экран (`lib/ui_demo/`)
общий с прошивкой платы.

## Сборка и прошивка

Проще всего — через значки в статус-баре VS Code (расширение PlatformIO IDE):
✔️ Build → → Upload → 🔌 Monitor.

Ниже — те же действия командами, если нужно запускать из терминала.
`pio.exe` не в `PATH`, поэтому путь к нему указывается полностью.

### PowerShell

```powershell
# Путь к pio.exe одной переменной, чтобы не повторять его в каждой команде
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

# Сборка проекта (проверка, что всё компилируется без ошибок)
& $pio run

# Сборка "с нуля" (очистить .pio/build и пересобрать)
& $pio run --target clean
& $pio run

# Прошивка платы по USB
# (COM-порт определяется автоматически; если нужно — укажите вручную)
& $pio run --target upload
& $pio run --target upload --upload-port COM1

# Список доступных COM-портов, которые видит PlatformIO
& $pio device list

# Serial Monitor (скорость 115200 уже задана в platformio.ini)
& $pio device monitor

# Serial Monitor с явным указанием порта и скорости
& $pio device monitor --port COM5 --baud 115200

# Сборка + прошивка + сразу открыть Serial Monitor одной командой
& $pio run --target upload; & $pio device monitor

# Обновить конфигурацию IntelliSense для VS Code (если редактор
# подчёркивает Arduino.h / TFT_eSPI.h красным)
& $pio project init --ide vscode
```

Чтобы выйти из Serial Monitor — `Ctrl+C`.

### CMD (cmd.exe)

```bat
:: Путь к pio.exe одной переменной
set PIO="%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

:: Сборка проекта
%PIO% run

:: Сборка "с нуля"
%PIO% run --target clean
%PIO% run

:: Прошивка платы по USB
%PIO% run --target upload
%PIO% run --target upload --upload-port COM5

:: Список доступных COM-портов
%PIO% device list

:: Serial Monitor (115200)
%PIO% device monitor

:: Serial Monitor с явным портом и скоростью
%PIO% device monitor --port COM5 --baud 115200

:: Сборка + прошивка + Serial Monitor
%PIO% run --target upload && %PIO% device monitor

:: Обновить конфигурацию IntelliSense для VS Code
%PIO% project init --ide vscode
```
