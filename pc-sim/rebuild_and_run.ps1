# Пересобрать симулятор и сразу перезапустить окно.
# Запускать из папки pc-sim: .\rebuild_and_run.ps1

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# Закрываем предыдущий запущенный sim.exe — иначе линковщик не сможет
# перезаписать файл (ошибка LNK1104: "не удается открыть файл sim.exe").
Stop-Process -Name "sim" -ErrorAction SilentlyContinue

& $cmake --build build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Сборка не удалась, sim.exe не перезапущен." -ForegroundColor Red
    exit 1
}

Start-Process ".\build\Release\sim.exe"
