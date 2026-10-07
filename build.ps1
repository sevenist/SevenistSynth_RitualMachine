# Build the OLED + synth simulator. Run from oled_sim:  .\build.ps1          (-Clean to rebuild everything)
#
# Every source file is compiled to its own object in build/obj and recompiled only when it, or a header it includes
# (tracked with -MMD), changed; compiles run in parallel. C files are built with gcc, the C++ engine with g++ -std=c++17.
#
# Samples: every .wav / .mp3 you put in samples_src/ is converted to samples/<name>.smp (the format the engine streams; only new or changed files,
# by tools/smp_convert.cpp), then copied to the simulator's card sdcard/system/samples (the folders of the card layout are made too):
#   .\build.ps1 -Sd E:          also sets up the real card E: (the card's drive letter): its folders + E:\system\samples
#   .\build.ps1 -NoSamples      skip the conversion
param([string]$Platform = "sim", [switch]$Clean, [string]$Display = "", [string]$Defs = "", [switch]$NoSamples, [string]$Sd = "")   # selects src/platform/<Platform>; -Display 128x128 builds for another screen size, -Defs "-DENGINE_SR=44100" passes engine flags (both with -Clean)
$ErrorActionPreference = "Stop"
$u8g2 = "lib/u8g2"
$objDir = "build/obj"
if ($Clean -and (Test-Path $objDir)) { Remove-Item -Recurse -Force $objDir }
New-Item -ItemType Directory -Force $objDir | Out-Null

# ---- samples: samples_src/*.wav|*.mp3 -> samples/*.smp (and optionally to the TF card) ----
if (-not $NoSamples -and (Test-Path samples_src)) {
    $conv = "build/smp_convert.exe"
    $inputs = @("tools/smp_convert.cpp", "src/platform/sim/sample_convert.h", "src/engine/sampler/smp_format.h") | ForEach-Object { Get-Item $_ }
    if (-not (Test-Path $conv) -or ($inputs | Where-Object { $_.LastWriteTime -gt (Get-Item $conv).LastWriteTime })) {
        $log = Join-Path $env:TEMP "smp_convert_cc.log"
        $p = Start-Process g++ -ArgumentList @("-O2", "-std=c++17", "-Isrc", "-Ilib/minimp3", "tools/smp_convert.cpp", "-o", $conv) -NoNewWindow -PassThru -Wait -RedirectStandardError $log -RedirectStandardOutput "$log.out"
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
        if (-not (Test-Path $t) -or (Get-Item $t).Length -ne $f.Length -or (Get-Item $t).LastWriteTime -lt $f.LastWriteTime) { Copy-Item $f.FullName $t -Force; $n++ }
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

$cSrc   = @(Get-ChildItem src/core/*.c) + @(Get-ChildItem src/platform/$Platform/*.c) +
          @(Get-ChildItem $u8g2/csrc/*.c) + @(Get-ChildItem $u8g2/sys/sdl/common/*.c)
$cppSrc = @(Get-ChildItem src/engine -Recurse -Filter *.cpp) + @(Get-ChildItem src/platform/engine/*.cpp) +
          @(Get-ChildItem src/platform/$Platform/*.cpp -ErrorAction SilentlyContinue)

$common = @("-O2", "-DPLATFORM_$($Platform.ToUpper())", "-Isrc", "-I$u8g2/csrc", "-IC:/msys64/ucrt64/include/SDL2", "-Ilib/minimp3", "-MMD")
if ($Display -match '^(\d+)x(\d+)$') { $common += @("-DDISPLAY_WIDTH=$($Matches[1])", "-DDISPLAY_HEIGHT=$($Matches[2])") }
if ($Defs) { $common += @($Defs -split '\s+' | Where-Object { $_ }) }
$cFlags   = $common
$cppFlags = $common + @("-std=c++17", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Wno-unused-parameter")

# Does this object need building? (missing, source newer, or any header listed in its .d file newer)
function Needs-Build($src, $obj, $dep) {
    if (-not (Test-Path $obj)) { return $true }
    $t = (Get-Item $obj).LastWriteTime
    if ((Get-Item $src).LastWriteTime -gt $t) { return $true }
    if (-not (Test-Path $dep)) { return $true }
    $text = (Get-Content $dep -Raw) -replace '\\\r?\n', ' '
    $files = ($text -split '\s+') | Where-Object { $_ -and $_ -notmatch ':$' } | Select-Object -Skip 0
    foreach ($f in $files) { if ((Test-Path $f) -and (Get-Item $f).LastWriteTime -gt $t) { return $true } }
    return $false
}

$jobs = @()
$expected = @{}     # objects that belong to a source file that still exists
foreach ($set in @(@{files = $cSrc; tool = "gcc"; flags = $cFlags}, @{files = $cppSrc; tool = "g++"; flags = $cppFlags})) {
    foreach ($f in $set.files) {
        $rel = (Resolve-Path -Relative $f.FullName) -replace '^\.\\', '' -replace '[\\/:]', '_'
        $obj = Join-Path $objDir ($rel + ".o")
        $dep = Join-Path $objDir ($rel + ".d")
        $expected[(Split-Path $obj -Leaf)] = $true
        if (Needs-Build $f.FullName $obj $dep) {
            $jobs += [pscustomobject]@{src = $f.FullName; obj = $obj; dep = $dep; tool = $set.tool; flags = $set.flags; name = $f.Name}
        }
    }
}

# a source file that was deleted or renamed must not stay in the link through its old object
Get-ChildItem $objDir -Filter *.o | Where-Object { -not $expected.ContainsKey($_.Name) } | ForEach-Object { Remove-Item $_.FullName, ($_.FullName -replace '\.o$', '.d') -ErrorAction SilentlyContinue }

$sw = [Diagnostics.Stopwatch]::StartNew()
$failed = $false
if ($jobs.Count -gt 0) {
    $parallel = [Math]::Max(1, [int]$env:NUMBER_OF_PROCESSORS)
    Write-Host ("Compiling {0} file(s), {1} at a time..." -f $jobs.Count, $parallel)
    $running = @()
    $queue = New-Object System.Collections.Queue
    $jobs | ForEach-Object { $queue.Enqueue($_) }
    $done = 0
    while ($queue.Count -gt 0 -or $running.Count -gt 0) {
        while ($queue.Count -gt 0 -and $running.Count -lt $parallel) {
            $j = $queue.Dequeue()
            $log = Join-Path $env:TEMP ("oled_cc_" + [IO.Path]::GetFileName($j.obj) + ".log")
            $args = $j.flags + @("-MF", $j.dep, "-c", $j.src, "-o", $j.obj)
            $p = Start-Process $j.tool -ArgumentList $args -NoNewWindow -PassThru -RedirectStandardError $log -RedirectStandardOutput "$log.out"
            $null = $p.Handle
            $running += [pscustomobject]@{proc = $p; job = $j; log = $log}
        }
        Start-Sleep -Milliseconds 40
        $still = @()
        foreach ($r in $running) {
            if ($r.proc.HasExited) {
                $done++
                $out = @(Get-Content $r.log, "$($r.log).out" -ErrorAction SilentlyContinue)
                if ($out.Count) { Write-Host ("--- " + $r.job.name) -ForegroundColor DarkGray; $out | ForEach-Object { Write-Host $_ } }
                if ($r.proc.ExitCode -ne 0) { $failed = $true; Remove-Item $r.job.obj -ErrorAction SilentlyContinue }
                Write-Host -NoNewline ("`r[{0}/{1}] " -f $done, $jobs.Count)
            } else { $still += $r }
        }
        $running = $still
    }
    Write-Host ""
}
if ($failed) { Write-Host "Build FAILED" -ForegroundColor Red; exit 1 }

# A running simulator locks its exe; build next to it instead of failing at the link step.
$out = "build/oled_sim.exe"
if (Test-Path $out) {
    try { [IO.File]::Open((Resolve-Path $out), "Open", "ReadWrite", "None").Close() }
    catch { $out = "build/oled_sim_new.exe"; Write-Host "oled_sim.exe is running (locked): building $out instead" -ForegroundColor Yellow }
}

$objs = Get-ChildItem $objDir -Filter *.o | ForEach-Object { $_.FullName }
$linkLog = Join-Path $env:TEMP "oled_link.log"
$p = Start-Process g++ -ArgumentList (@($objs) + @("-LC:/msys64/ucrt64/lib", "-lSDL2", "-lm", "-lole32", "-lwinmm", "-o", $out)) `
        -NoNewWindow -PassThru -Wait -RedirectStandardError $linkLog -RedirectStandardOutput "$linkLog.out"
Get-Content $linkLog, "$linkLog.out" -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
if ($p.ExitCode -eq 0) { Write-Host ("Built {0} in {1:N1}s" -f $out, $sw.Elapsed.TotalSeconds) -ForegroundColor Green }
else { Write-Host "Link FAILED (exit $($p.ExitCode))" -ForegroundColor Red; exit $p.ExitCode }
