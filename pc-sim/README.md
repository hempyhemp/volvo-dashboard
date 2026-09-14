# PC-симулятор интерфейса (LVGL + SDL2)

Позволяет разрабатывать и смотреть UI приборки прямо на компьютере,
без прошивки платы. Экран 320x240, тот же размер, что и у реального
ILI9341 на ESP32-2432S028.

Код интерфейса в `../lib/ui_demo/ui_demo.c` не знает про железо — это
буквально тот же файл, что использует прошивка ESP32 (там его рисует
LVGL через TFT_eSPI вместо SDL2-окна).

**Все команды ниже выполняются из папки `pc-sim`**, а не из корня
проекта — сначала перейдите в неё:

```powershell
cd D:\MyApps\volvo-dashboard\pc-sim
```

## Из чего собрано

* **LVGL** (`lib/lvgl`, версия v9.5.0) — библиотека интерфейса, склонирована с GitHub.
* **SDL2** — окно на ПК, подтягивается автоматически через `vcpkg` (манифест `vcpkg.json`).
* **vcpkg** — свежая версия, лежит в `tools/vcpkg` (bootstrap выполнен один раз; встроенный в Visual Studio vcpkg оказался слишком старым для актуальных портов).
* Компилятор — MSVC из Visual Studio 2022 (Community), CMake — тоже из комплекта VS.

## Пересборка + перезапуск одной командой

После изменения `src/main.c` или `../lib/ui_demo/ui_demo.c`:

```powershell
.\rebuild_and_run.ps1
```

Скрипт сам закрывает предыдущее окно `sim.exe` (если оно ещё открыто —
иначе линковщик не сможет перезаписать файл, `LNK1104`), пересобирает
и запускает заново. Если PowerShell откажется выполнять `.ps1` из-за
политики безопасности:

```powershell
powershell -ExecutionPolicy Bypass -File .\rebuild_and_run.ps1
```

### Вручную, по шагам (если нужно)

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# Нужно только один раз, либо после изменений в CMakeLists.txt
& $cmake -B build

# Пересборка
& $cmake --build build --config Release

# Запуск (закройте предыдущее окно перед пересборкой)
.\build\Release\sim.exe
```

Откроется окно 320x240. Закрыть — просто закрыть окно (крестик).

## Если нужно настроить с нуля (например, на другом компьютере)

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$vcpkg = "$PWD\tools\vcpkg\scripts\buildsystems\vcpkg.cmake"

& $cmake -G "Visual Studio 17 2022" -A x64 -B build -DCMAKE_TOOLCHAIN_FILE=$vcpkg -DVCPKG_TARGET_TRIPLET=x64-windows
& $cmake --build build --config Release
```

Первая настройка соберёт SDL2 из исходников через vcpkg — это может
занять несколько минут. Повторные пересборки — секунды.
