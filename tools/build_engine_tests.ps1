# Build and run the engine unit tests on the desktop (g++ C++17 for the engine and tests, gcc for the C core files).
# Run from oled_sim:
#   .\tools\build_engine_tests.ps1                       (default ENGINE_SR / ENGINE_BLOCK)
#   .\tools\build_engine_tests.ps1 -Sr 44100 -Block 64   (one custom configuration)
#   .\tools\build_engine_tests.ps1 -Matrix               (the six supported configurations, summary at the end)
#   .\tools\build_engine_tests.ps1 -Filter spectral      (only tests whose name contains the text)
#   $env:DX7_GAIN_OUT = "src/platform/engine/fm_patch_gain.h"   regenerates the FM loudness trim table during the run
param([int]$Sr = 48000, [int]$Block = 32, [switch]$Matrix, [string]$Filter = "")
$ErrorActionPreference = "Continue"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH

$cpp = @((Get-ChildItem tests/engine/*.cpp).FullName) +
       @((Get-ChildItem src/engine -Recurse -Filter *.cpp).FullName) +
       @((Get-ChildItem src/platform/engine/*.cpp).FullName)
# the C core files the integration tests use (the application's data model, compiled as C)
$coreObj = @()
New-Item -ItemType Directory -Force build/tobj | Out-Null
$newestHeader = (Get-ChildItem src/core/*.h | Sort-Object LastWriteTime | Select-Object -Last 1).LastWriteTime
foreach ($f in "rack", "synth_config", "synth_params", "dx7", "dx7_factory", "seq", "fxrack") {
    $o = "build/tobj/$f.o"
    if (-not (Test-Path $o) -or (Get-Item "src/core/$f.c").LastWriteTime -gt (Get-Item $o).LastWriteTime -or $newestHeader -gt (Get-Item $o).LastWriteTime) { & gcc -c -O2 -w -Isrc "src/core/$f.c" -o $o }
    $coreObj += $o
}

function Run-One($sr, $block) {
    $out = "build/engine_tests_${sr}_${block}.exe"
    if (Test-Path $out) { Remove-Item $out }
    $log = Join-Path $env:TEMP "engine_build.log"
    $args = @("-std=c++17", "-O2", "-pthread", "-Wall", "-Wextra", "-Wconversion", "-Wno-sign-conversion", "-Wno-unused-parameter", "-fno-exceptions", "-fno-rtti",
              "-DPLATFORM_SIM", "-DENGINE_SR=$sr", "-DENGINE_BLOCK=$block", "-Isrc", "-Itests/engine") + $cpp + $coreObj + @("-o", $out)
    $p = Start-Process g++ -ArgumentList $args -NoNewWindow -PassThru -Wait -RedirectStandardError $log -RedirectStandardOutput "$log.out"
    if ($p.ExitCode -ne 0 -or -not (Test-Path $out)) {
        Get-Content $log | Select-Object -First 30 | ForEach-Object { Write-Host $_ }
        Write-Host "Engine test build FAILED ($sr Hz, block $block)" -ForegroundColor Red
        return 2
    }
    $warn = Get-Content $log | Select-String "warning"
    if ($warn) { $warn | Select-Object -First 10 | ForEach-Object { Write-Host $_.Line -ForegroundColor Yellow } }
    if ($Filter) { & ".\$out" $Filter | Out-Host } else { & ".\$out" | Out-Host }
    return [int]$LASTEXITCODE
}

if ($Matrix) {
    $failed = @()
    foreach ($c in @(@(48000, 32), @(44100, 64), @(48000, 16), @(32000, 32), @(96000, 128), @(48000, 256))) {
        Write-Host "=== ENGINE_SR=$($c[0]) ENGINE_BLOCK=$($c[1])" -ForegroundColor Cyan
        $code = Run-One $c[0] $c[1]
        if ($code -ne 0) { $failed += "$($c[0])/$($c[1])" }
    }
    if ($failed.Count) { Write-Host "FAILED configurations: $($failed -join ', ')" -ForegroundColor Red; exit 1 }
    Write-Host "All configurations passed" -ForegroundColor Green
    exit 0
}
exit (Run-One $Sr $Block)
