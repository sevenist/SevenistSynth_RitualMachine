# Builds and runs TechnoGen's host tests (tests/): the app's portable code, the colour canvas and the engine's track modules, with the board
# stubbed (tests/stubs.cpp). Run it from anywhere:
#   .\apps\technogen\tools\build_tests.ps1               every test
#   .\apps\technogen\tools\build_tests.ps1 -Filter dice  only the tests whose name contains the text
# The compile and link steps are the library's (framework/tools/sim_build.ps1: one object per file, only what changed). The screen is HWV2's,
# 128 x 160, as on the board.
param([string]$Filter = "", [switch]$Clean)
$ErrorActionPreference = "Stop"
$fw = "../../framework"
$u8g2 = "../../lib/u8g2"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
Push-Location (Join-Path $PSScriptRoot "..")
try {
    . "$fw/tools/sim_build.ps1"
    $objDir = "build/tobj"
    if ($Clean -and (Test-Path $objDir)) { Remove-Item -Recurse -Force $objDir }
    # the app's portable files: everything in src/ but the two platform mains (they are guarded, but the tests have their own main)
    $appSrc = @(Get-ChildItem src/*.cpp | Where-Object { $_.Name -notlike "main_*" })
    $cppSrc = @(Get-ChildItem tests/*.cpp) + $appSrc + @(Get-EngineSources)
    $cSrc = @(Get-UiSources) + @(Get-Item "$fw/boards/hwv2/hwv2_controls.c") + @(Get-ChildItem $u8g2/csrc/*.c)
    $flags = @("-O1", "-DPLATFORM_SIM", "-DPLATFORM_TEST", "-DDISPLAY_HEIGHT=160", "-Isrc", "-I.", "-I$fw", "-I$u8g2/csrc")
    if (-not (Invoke-SimBuild -CSources $cSrc -CppSources $cppSrc -Flags $flags -ObjDir $objDir -Out "build/tests.exe" -Libs @("-lm"))) { exit 2 }
    $exe = Get-ChildItem "build/tests.exe", "build/tests_new.exe" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($Filter) { & $exe.FullName $Filter } else { & $exe.FullName }
    exit $LASTEXITCODE
} finally { Pop-Location }
