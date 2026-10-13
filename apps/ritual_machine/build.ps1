# Build the Ritual Machine's desktop simulator. Run it from anywhere:  .\build.ps1          (-Clean to rebuild everything)
# (the repo root's build.ps1 calls this one). The app's sources are src/ here; the engine is ../../framework/engine.
#
# Every source file is compiled to its own object in build/obj and recompiled only when it, or a header it includes
# (tracked with -MMD), changed; compiles run in parallel (framework/tools/sim_build.ps1, shared by every app).
#
# Samples: every .wav / .mp3 you put in samples_src/ is converted to samples/<name>.smp (the format the engine streams; only new or changed files,
# by framework/tools/smp_convert.cpp), then copied to the simulator's card sdcard/system/samples (the folders of the card layout are made too):
#   .\build.ps1 -Sd E:          also sets up the real card E: (the card's drive letter): its folders + E:\system\samples
#   .\build.ps1 -NoSamples      skip the conversion
param([string]$Platform = "sim", [switch]$Clean, [string]$Display = "", [string]$Defs = "", [switch]$NoSamples, [string]$Sd = "")   # selects src/platform/<Platform>; -Display 128x128 builds for another screen size, -Defs "-DENGINE_SR=44100" passes engine flags (both with -Clean)
$ErrorActionPreference = "Stop"
$fw = "../../framework"                 # the library: its engine, the desktop-only helpers (host/), the shared tools
$lib = "../../lib"                      # third-party code for the desktop build (not versioned: DEVELOPING.md, "Requirements")
$u8g2 = "$lib/u8g2"
$objDir = "build/obj"
Push-Location $PSScriptRoot
try {
if ($Clean -and (Test-Path $objDir)) { Remove-Item -Recurse -Force $objDir }
New-Item -ItemType Directory -Force $objDir | Out-Null

# ---- samples: samples_src/*.wav|*.mp3 -> samples/*.smp (and optionally to the TF card) ----
if (-not $NoSamples -and (Test-Path samples_src)) {
    $conv = "build/smp_convert.exe"
    $inputs = @("$fw/tools/smp_convert.cpp", "$fw/host/sample_convert.h", "$fw/engine/sampler/smp_format.h") | ForEach-Object { Get-Item $_ }
    if (-not (Test-Path $conv) -or ($inputs | Where-Object { $_.LastWriteTime -gt (Get-Item $conv).LastWriteTime })) {
        $log = Join-Path $env:TEMP "smp_convert_cc.log"
        $p = Start-Process g++ -ArgumentList @("-O2", "-std=c++17", "-I$fw", "-I$lib/minimp3", "$fw/tools/smp_convert.cpp", "-o", $conv) -NoNewWindow -PassThru -Wait -RedirectStandardError $log -RedirectStandardOutput "$log.out"
        if ($p.ExitCode -ne 0) { Get-Content $log | ForEach-Object { Write-Host $_ }; Write-Host "Sample converter build FAILED" -ForegroundColor Red; exit 1 }
    }
    & $conv samples_src samples
    if ($LASTEXITCODE -ne 0) { Write-Host "Some samples could not be converted (see above)" -ForegroundColor Yellow }
}
# The card layout (hal/hal_storage.h, STORAGE_FOLDERS) and the samples in system\samples: on the simulator's card sdcard\ always, on a real card with -Sd.
function Sync-Card([string]$root) {
    foreach ($d in @("system", "system\samples", "system\config", "import", "presets")) { New-Item -ItemType Directory -Force (Join-Path $root $d) | Out-Null }
    $dest = Join-Path $root "system\samples"
    $n = 0
    foreach ($f in Get-ChildItem samples -Filter *.smp -ErrorAction SilentlyContinue) {
        $t = Join-Path $dest $f.Name
        # -LiteralPath: a sample name may hold [ ] (wildcards to PowerShell: the copy failed and stopped the build)
        if (-not (Test-Path -LiteralPath $t) -or (Get-Item -LiteralPath $t).Length -ne $f.Length -or (Get-Item -LiteralPath $t).LastWriteTime -lt $f.LastWriteTime) { Copy-Item -LiteralPath $f.FullName -Destination $t -Force; $n++ }
    }
    if ($n) { Write-Host ("samples: {0} file(s) copied to {1}" -f $n, $dest) }
}
if (-not $NoSamples) { Sync-Card "sdcard" }
if ($Sd) {
    $drive = ($Sd.TrimEnd([char]92, [char]58)) + ":" + [char]92
    if (-not (Test-Path $drive)) { Write-Host "No drive ${Sd}: samples not copied" -ForegroundColor Yellow }
    else { Sync-Card $drive }
}

# ---- UI sprites: assets/UI_Sprites/{8,16,24,64}/*.png -> src/core/ui_sprites_gen.c/.h (rewritten only when they change) ----
$py = "C:\.platformio\penv\Scripts\python.exe"           # the PlatformIO Python (never a bare python: see CONTINUE.md, traps)
if (Test-Path $py) {
    & $py tools/gen_ui_sprites.py
    if ($LASTEXITCODE -ne 0) { Write-Host "Some UI sprites could not be converted (see above)" -ForegroundColor Yellow }
} else { Write-Host "No PlatformIO Python: UI sprites not regenerated (the committed ui_sprites_gen.c is used)" -ForegroundColor Yellow }

# ---- the program ----
. "$fw/tools/sim_build.ps1"
$cSrc   = @(Get-ChildItem src/core/*.c) + @(Get-ChildItem src/app/*.c) + @(Get-ChildItem src/platform/$Platform/*.c) +
          @(Get-UiSources) + @(Get-DesktopBoardSources) +
          @(Get-ChildItem $u8g2/csrc/*.c) + @(Get-ChildItem $u8g2/sys/sdl/common/*.c)
$cppSrc = @(Get-EngineSources) + @(Get-ChildItem src/platform/engine/*.cpp) +
          @(Get-ChildItem src/platform/$Platform/*.cpp -ErrorAction SilentlyContinue)

$flags = @("-O2", "-DPLATFORM_$($Platform.ToUpper())", "-Isrc", "-I$fw", "-I$u8g2/csrc", "-IC:/msys64/ucrt64/include/SDL2", "-I$lib/minimp3")
if ($Display -match '^(\d+)x(\d+)$') { $flags += @("-DDISPLAY_WIDTH=$($Matches[1])", "-DDISPLAY_HEIGHT=$($Matches[2])") }
if ($Defs) { $flags += @($Defs -split '\s+' | Where-Object { $_ }) }

if (-not (Invoke-SimBuild -CSources $cSrc -CppSources $cppSrc -Flags $flags -ObjDir $objDir -Out "build/oled_sim.exe")) { exit 1 }
exit 0
} finally { Pop-Location }
