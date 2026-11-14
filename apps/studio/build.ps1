# Build SynthLab Studio, a desktop-only program (SDL): .\build.ps1     (-Clean to rebuild everything)
#   .\build\studio.exe                  the window
#   .\build\studio.exe --selftest       the models and the widgets
#   .\build\studio.exe --shot a.png --tab layout --synth 2      one frame into a PNG (no window)
# It uses SynthLab's synths and sound (apps/synthlab/src: lab.h, synth.cpp, lab_voice.cpp, synths/) and the library: the engine, the
# drawing kit with its widgets (framework/ui), the modular catalog (framework/modular), the desktop's tool window and audio
# (framework/boards/desktop). No board, no firmware.
param([switch]$Clean, [string]$Defs = "")
$ErrorActionPreference = "Stop"
$name = "studio"
$fw = "../../framework"
$lab = "../synthlab"
$u8g2 = "../../lib/u8g2"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
Push-Location $PSScriptRoot
try {
    . "$fw/tools/sim_build.ps1"
    $objDir = "build/obj"
    if ($Clean -and (Test-Path $objDir)) { Remove-Item -Recurse -Force $objDir }
    $labSrc = @(Get-Item "$lab/src/synth.cpp", "$lab/src/lab_voice.cpp") + @(Get-ChildItem "$lab/src/synths/*.cpp")
    $cppSrc = @(Get-EngineSources) + $labSrc + @(Get-ChildItem src/*.cpp) + @(Get-Item "$lab/tests/png.cpp") + @(Get-ChildItem "$fw/modular/*.cpp")
    # the desktop board's files the studio uses: the tool window, the audio output (the display window is linked for canvas_send, never opened)
    $desk = "$fw/boards/desktop"
    $cSrc = @(Get-UiSources) + @(Get-Item "$desk/desk_window.c", "$desk/audio_out_sim.c", "$desk/display_sim.c") + @(Get-ChildItem $u8g2/csrc/*.c) + @(Get-ChildItem $u8g2/sys/sdl/common/*.c)
    $flags = @("-O2", "-DPLATFORM_SIM", "-Isrc", "-I$lab/src", "-I$fw", "-I$u8g2/csrc", "-IC:/msys64/ucrt64/include/SDL2",
               "-DDISPLAY_WIDTH=128", "-DDISPLAY_HEIGHT=160", "-DCANVAS_MAX_W=2040", "-DCANVAS_MAX_H=1280",
               "-DLAB_STUDIO", "-DLAB_MAX_PARAMS=256")     # (SynthLab's list gets the Modular synth; a patch's list can be long)
    if ($Defs) { $flags += @($Defs -split '\s+' | Where-Object { $_ }) }
    if (-not (Invoke-SimBuild -CSources $cSrc -CppSources $cppSrc -Flags $flags -ObjDir $objDir -Out "build/$name.exe")) { exit 1 }
    exit 0
} finally { Pop-Location }
