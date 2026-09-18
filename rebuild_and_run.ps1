# Wrapper to run from the repo ROOT: .\rebuild_and_run.ps1
# Just calls the real script in pc-sim (it resolves its own paths).
# ASCII-only on purpose (see note in pc-sim\rebuild_and_run.ps1).
& (Join-Path $PSScriptRoot "pc-sim\rebuild_and_run.ps1")
exit $LASTEXITCODE
