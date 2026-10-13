# Runs an app's desktop program from the repo root, in the app's own folder (its card is the folder sdcard/ there):
#   .\run.ps1                        the Ritual Machine's simulator
#   .\run.ps1 -App template          another app of apps/
#   .\run.ps1 -Board hwv1            the program built for another board (.\build.ps1 -Board hwv1; default: panel)
# Build first with .\build.ps1 (same -App, same -Board). If the build found the program running it wrote <name>_new.exe: the newer of
# the two runs.
param([string]$App = "ritual_machine", [string]$Board = "panel")
$dir = Join-Path $PSScriptRoot "apps/$App"
$base = if ($App -eq "ritual_machine") { "oled_sim" } else { $App }        # (the Ritual Machine's program keeps its first name)
$name = if ($Board -and $Board -ne "panel") { "${base}_$Board" } else { $base }     # (each board is a program of its own: framework/tools/sim_build.ps1)
$exe = Get-ChildItem "$dir/build/$name.exe", "$dir/build/${name}_new.exe" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $exe) { Write-Host "Nothing built for '$App' on the board '$Board' yet: run .\build.ps1 -App $App -Board $Board" -ForegroundColor Red; exit 1 }
# a build of another board that is newer: this program may not have the last change
$newer = Get-ChildItem "$dir/build/$base.exe", "$dir/build/${base}_*.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.BaseName -ne $name -and $_.BaseName -notmatch '_new$' -and $_.LastWriteTime -gt $exe.LastWriteTime.AddMinutes(1) } | Select-Object -First 1
if ($newer) { Write-Host "Note: $($exe.Name) is older than $($newer.Name): build this board again if the code changed (.\build.ps1 -App $App -Board $Board)" -ForegroundColor Yellow }
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH      # SDL2.dll and the compiler's runtime DLLs
Push-Location $dir
try { & $exe.FullName @args } finally { Pop-Location }
exit $LASTEXITCODE
