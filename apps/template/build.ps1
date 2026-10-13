# Build this app's desktop program (SDL). Run it from anywhere:  .\build.ps1     (-Clean to rebuild everything)
#   -Defs "-DENGINE_SR=44100"    engine flags (framework/engine/dsp/config.h), with -Clean
# The compile and link steps are the library's (framework/tools/sim_build.ps1: one object per file, parallel, only what changed).
# The first build also compiles u8g2 (the display library, lib/u8g2: DEVELOPING.md, "Requirements"): about 10 s.
param([switch]$Clean, [string]$Defs = "")
$ErrorActionPreference = "Stop"
$name = "template"                      # the program: build/<name>.exe (tools/new_project.ps1 sets it)
$fw = "../../framework"                 # the library
$u8g2 = "../../lib/u8g2"                # the display library (third-party, not versioned)
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
Push-Location $PSScriptRoot
try {
    if ($Clean -and (Test-Path build/obj)) { Remove-Item -Recurse -Force build/obj }
    . "$fw/tools/sim_build.ps1"
    $cppSrc = @(Get-EngineSources) + @(Get-ChildItem src/*.cpp)        # (the firmware's files compile to nothing here: they are guarded)
    # the library's drawing kit and its desktop board (the display and panel windows, the PC keyboard as the keys, SDL audio), and u8g2
    $cSrc = @(Get-UiSources) + @(Get-DesktopBoardSources) + @(Get-ChildItem $u8g2/csrc/*.c) + @(Get-ChildItem $u8g2/sys/sdl/common/*.c)
    $flags = @("-O2", "-DPLATFORM_SIM", "-Isrc", "-I$fw", "-I$u8g2/csrc", "-IC:/msys64/ucrt64/include/SDL2")
    if ($Defs) { $flags += @($Defs -split '\s+' | Where-Object { $_ }) }
    if (-not (Invoke-SimBuild -CSources $cSrc -CppSources $cppSrc -Flags $flags -Out "build/$name.exe")) { exit 1 }
    exit 0
} finally { Pop-Location }
