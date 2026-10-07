# Build and run the UI tests (tests/ui: the application core on the platform stubs of tools/ui_stubs.c; no SDL, no audio). Run from oled_sim:
#   .\tools\build_ui_tests.ps1               every test
#   .\tools\build_ui_tests.ps1 -Filter fm    only the tests whose name contains the text
# From bash: gcc -O1 -Wall -Wno-unused-function -Isrc -Ilib/u8g2/csrc tests/ui/*.c tools/ui_stubs.c src/core/*.c lib/u8g2/csrc/*.c -lm -o build/ui_tests.exe
param([string]$Filter = "")
$ErrorActionPreference = "Continue"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
New-Item -ItemType Directory -Force build | Out-Null

$src = @((Get-ChildItem tests/ui/*.c).FullName) + @("tools/ui_stubs.c") + @((Get-ChildItem src/core/*.c).FullName) + @((Get-ChildItem lib/u8g2/csrc/*.c).FullName)
$out = "build/ui_tests.exe"
if (Test-Path $out) { Remove-Item $out }
$log = Join-Path $env:TEMP "ui_tests_build.log"
$args = @("-std=gnu11", "-O1", "-Wall", "-Wno-unused-function", "-Isrc", "-Ilib/u8g2/csrc") + $src + @("-lm", "-o", $out)
$p = Start-Process gcc -ArgumentList $args -NoNewWindow -PassThru -Wait -RedirectStandardError $log -RedirectStandardOutput "$log.out"
if ($p.ExitCode -ne 0 -or -not (Test-Path $out)) {
    Get-Content $log | Select-Object -First 40 | ForEach-Object { Write-Host $_ }
    Write-Host "UI test build FAILED" -ForegroundColor Red
    exit 2
}
$warn = Get-Content $log | Select-String "warning" | Where-Object { $_.Line -notmatch "u8g2" }
if ($warn) { $warn | Select-Object -First 10 | ForEach-Object { Write-Host $_.Line -ForegroundColor Yellow } }
if ($Filter) { & ".\$out" $Filter } else { & ".\$out" }
exit $LASTEXITCODE
