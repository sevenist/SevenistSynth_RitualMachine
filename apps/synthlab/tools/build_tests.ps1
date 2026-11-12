# Builds and runs SynthLab's host tests (tests/): every synth of the list alone and through the engine, a WAV of each in build/wav/, and
# the screen with the encoders (pictures in build/screen_*.png).
# Run it from anywhere:
#   .\apps\synthlab\tools\build_tests.ps1             every test, every synth
#   .\apps\synthlab\tools\build_tests.ps1 -Filter FM2 only the tests whose name, or whose synth's name, contains the text
# The compile and link steps are the library's (framework/tools/sim_build.ps1: one object per file, only what changed). The board is
# stubbed (tests/test_app.cpp): HWV2's control table and its 128 x 160 screen, as on the board.
#   .\apps\synthlab\tools\build_tests.ps1 Kick -Values Click=0,"AMP ENV/Decay=10"   that synth with other values (test_main.cpp: a name is
#                                             the first value with that name, SECTION/name the one in that section, #n the n-th row)
param([string]$Filter = "", [switch]$Clean, [string[]]$Values = @())
$ErrorActionPreference = "Stop"
$fw = "../../framework"
$u8g2 = "../../lib/u8g2"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
Push-Location (Join-Path $PSScriptRoot "..")
try {
    . "$fw/tools/sim_build.ps1"
    $objDir = "build/tobj"
    if ($Clean -and (Test-Path $objDir)) { Remove-Item -Recurse -Force $objDir }
    New-Item -ItemType Directory -Force "build/wav", "build/wav/null" | Out-Null
    $appSrc = @(Get-ChildItem src/*.cpp | Where-Object { $_.Name -notlike "main_*" }) + @(Get-ChildItem src/synths/*.cpp)   # (the tests have their own main)
    $cppSrc = @(Get-ChildItem tests/*.cpp) + $appSrc + @(Get-EngineSources)
    $cSrc = @(Get-UiSources) + @(Get-Item "$fw/boards/hwv2/hwv2_controls.c") + @(Get-ChildItem $u8g2/csrc/*.c)
    $flags = @("-O2", "-DPLATFORM_SIM", "-DPLATFORM_TEST", "-DDISPLAY_HEIGHT=160", "-Isrc", "-I.", "-I$fw", "-I$u8g2/csrc")
    if (-not (Invoke-SimBuild -CSources $cSrc -CppSources $cppSrc -Flags $flags -ObjDir $objDir -Out "build/tests.exe" -Libs @("-lm"))) { exit 2 }
    $exe = Get-ChildItem "build/tests.exe", "build/tests_new.exe" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($Filter) { & $exe.FullName $Filter @Values } else { & $exe.FullName }
    exit $LASTEXITCODE
} finally { Pop-Location }
