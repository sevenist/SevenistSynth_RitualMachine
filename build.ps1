# Builds an app's desktop program from the repo root. The default is the Ritual Machine, so this is the command it always was:
#   .\build.ps1                      the Ritual Machine's simulator (apps/ritual_machine/build/oled_sim.exe)
#   .\build.ps1 -Clean               every other argument goes to the app's own build.ps1
#   .\build.ps1 -App template        another app of apps/
#   .\build.ps1 -Board hwv1          the board the panel window draws (default: panel); its own program, run with .\run.ps1 -Board hwv1
# Run what it built with .\run.ps1 (the program needs its app folder as the working directory: its card is the folder sdcard/ there).
param([string]$App = "ritual_machine")
$script = Join-Path $PSScriptRoot "apps/$App/build.ps1"
if (-not (Test-Path $script)) { Write-Host "No app '$App' (no $script)" -ForegroundColor Red; exit 1 }
& $script @args
exit $LASTEXITCODE
