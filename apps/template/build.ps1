# Build this app's desktop program (SDL). Run it from anywhere:  .\build.ps1     (-Clean to rebuild everything)
#   -Defs "-DENGINE_SR=44100"    engine flags (framework/engine/dsp/config.h), with -Clean
# The compile and link steps are the library's (framework/tools/sim_build.ps1: one object per file, parallel, only what changed).
param([switch]$Clean, [string]$Defs = "")
$ErrorActionPreference = "Stop"
$name = "template"                      # the program: build/<name>.exe (tools/new_project.ps1 sets it)
$fw = "../../framework"                 # the library
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
Push-Location $PSScriptRoot
try {
    if ($Clean -and (Test-Path build/obj)) { Remove-Item -Recurse -Force build/obj }
    . "$fw/tools/sim_build.ps1"
    $cppSrc = @(Get-EngineSources) + @(Get-ChildItem src/*.cpp)        # (the firmware's files compile to nothing here: they are guarded)
    $flags = @("-O2", "-DPLATFORM_SIM", "-Isrc", "-I$fw", "-IC:/msys64/ucrt64/include/SDL2")
    if ($Defs) { $flags += @($Defs -split '\s+' | Where-Object { $_ }) }
    if (-not (Invoke-SimBuild -CppSources $cppSrc -Flags $flags -Out "build/$name.exe")) { exit 1 }
    exit 0
} finally { Pop-Location }
