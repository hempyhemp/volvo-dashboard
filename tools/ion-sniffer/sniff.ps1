# Запуск сниффера ИОН одной командой из любого места.
#   .\tools\ion-sniffer\sniff.ps1            - запустить ИОН и снимать обмен
#   .\tools\ion-sniffer\sniff.ps1 --attach   - прицепиться к уже открытому ИОН
# Все аргументы прокидываются в sniff_ion.py.
$ErrorActionPreference = "Stop"
$py = Join-Path $PSScriptRoot "sniff_ion.py"
python $py @args
