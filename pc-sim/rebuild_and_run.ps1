# Rebuild the PC simulator and relaunch its window.
# Works from any folder: all paths are resolved from the script location,
# so you can run it as .\rebuild_and_run.ps1 from pc-sim, or as
# .\pc-sim\rebuild_and_run.ps1 from the repo root.
# NOTE: ASCII-only on purpose. Windows PowerShell 5.1 reads UTF-8-no-BOM
# files as the system ANSI codepage, which mangles Cyrillic and breaks
# parsing. Keep this file plain ASCII.

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$here = $PSScriptRoot
$build = Join-Path $here "build"

# Close a previously running sim.exe, otherwise the linker cannot
# overwrite the file (LNK1104: cannot open file sim.exe).
Stop-Process -Name "sim" -ErrorAction SilentlyContinue

# Configure the build dir from scratch if it is missing (fresh clone,
# or the build folder was deleted).
if (-not (Test-Path (Join-Path $build "CMakeCache.txt"))) {
    Write-Host "build not configured - running CMake configure..." -ForegroundColor Yellow
    & $cmake -S $here -B $build
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configure failed." -ForegroundColor Red
        exit 1
    }
}

& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed, sim.exe not relaunched." -ForegroundColor Red
    exit 1
}

Start-Process (Join-Path $build "Release\sim.exe")
