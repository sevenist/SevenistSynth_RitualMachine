# Runs an app's desktop program from the repo root, in the app's own folder (its card is the folder sdcard/ there):
#   .\run.ps1                        the Ritual Machine's simulator
#   .\run.ps1 -App template          another app of apps/
# Build first with .\build.ps1 (same -App). If the build found the program running it wrote <name>_new.exe: the newer of the two runs.
param([string]$App = "ritual_machine")
$dir = Join-Path $PSScriptRoot "apps/$App"
$name = if ($App -eq "ritual_machine") { "oled_sim" } else { $App }        # (the Ritual Machine's program keeps its first name)
$exe = Get-ChildItem "$dir/build/$name.exe", "$dir/build/${name}_new.exe" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $exe) { Write-Host "Nothing built for '$App' yet: run .\build.ps1 -App $App" -ForegroundColor Red; exit 1 }
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH      # SDL2.dll and the compiler's runtime DLLs
Push-Location $dir
try { & $exe.FullName @args } finally { Pop-Location }
exit $LASTEXITCODE
