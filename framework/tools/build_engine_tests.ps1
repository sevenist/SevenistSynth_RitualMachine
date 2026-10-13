# Build and run the library's own tests on the desktop (framework/tests: the engine alone, no app code). Run it from anywhere:
#   .\framework\tools\build_engine_tests.ps1                       (default ENGINE_SR / ENGINE_BLOCK)
#   .\framework\tools\build_engine_tests.ps1 -Sr 44100 -Block 64   (one custom configuration)
#   .\framework\tools\build_engine_tests.ps1 -Matrix               (the six supported configurations, summary at the end)
#   .\framework\tools\build_engine_tests.ps1 -Filter spectral      (only tests whose name contains the text)
# An app's tests that need the engine with its own code (a rack, a sequencer pattern) live with the app: apps/<name>/tools.
param([int]$Sr = 48000, [int]$Block = 32, [switch]$Matrix, [string]$Filter = "", [string]$Defs = "")
$ErrorActionPreference = "Continue"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
$fw = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outDir = Join-Path $fw "build"
New-Item -ItemType Directory -Force $outDir | Out-Null

$cpp = @((Get-ChildItem "$fw/tests/*.cpp").FullName) + @((Get-ChildItem "$fw/engine" -Recurse -Filter *.cpp).FullName)

function Run-One($sr, $block) {
    $out = Join-Path $outDir "engine_tests_${sr}_${block}.exe"
    if (Test-Path $out) { Remove-Item $out }
    $log = Join-Path $env:TEMP "fw_engine_build.log"
    $ccArgs = @("-std=c++17", "-O2", "-pthread", "-Wall", "-Wextra", "-Wconversion", "-Wno-sign-conversion", "-Wno-unused-parameter", "-fno-exceptions", "-fno-rtti",
                "-DPLATFORM_SIM", "-DENGINE_SR=$sr", "-DENGINE_BLOCK=$block", "-I$fw", "-I$fw/tests") + @($Defs -split " " | Where-Object { $_ }) + $cpp + @("-o", $out)
    $p = Start-Process g++ -ArgumentList $ccArgs -NoNewWindow -PassThru -Wait -RedirectStandardError $log -RedirectStandardOutput "$log.out"
    if ($p.ExitCode -ne 0 -or -not (Test-Path $out)) {
        Get-Content $log | Select-Object -First 30 | ForEach-Object { Write-Host $_ }
        Write-Host "Library test build FAILED ($sr Hz, block $block)" -ForegroundColor Red
        return 2
    }
    $warn = Get-Content $log | Select-String "warning"
    if ($warn) { $warn | Select-Object -First 10 | ForEach-Object { Write-Host $_.Line -ForegroundColor Yellow } }
    if ($Filter) { & $out $Filter | Out-Host } else { & $out | Out-Host }
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
