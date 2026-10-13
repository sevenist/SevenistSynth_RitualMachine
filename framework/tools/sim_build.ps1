# The desktop build every app shares. Dot-source it from an app's build.ps1, then call Invoke-SimBuild with the app's source files:
#   . "$PSScriptRoot/../../framework/tools/sim_build.ps1"
#   Invoke-SimBuild -CSources $c -CppSources $cpp -Flags $flags -Out "build/my_app.exe"
#
# Every source file is compiled to its own object in -ObjDir and recompiled only when it, or a header it includes (tracked with -MMD),
# changed; compiles run in parallel. C files are built with gcc, C++ files with g++ -std=c++17 (the engine: no exceptions, no RTTI).
# The link takes every object of -ObjDir, so the folder belongs to one program.

# The engine's C++ sources (framework/engine), for an app's -CppSources.
function Get-EngineSources {
    return @(Get-ChildItem (Join-Path $PSScriptRoot "../engine") -Recurse -Filter *.cpp)
}

# The library's drawing kit (framework/ui) and its desktop board (framework/boards/desktop: the display and panel windows, the
# keyboard and mouse as controls, SDL audio, the card as a folder), for an app's -CSources. Both need u8g2 on the include path.
function Get-UiSources {
    return @(Get-ChildItem (Join-Path $PSScriptRoot "../ui") -Filter *.c)
}
function Get-DesktopBoardSources {
    return @(Get-ChildItem (Join-Path $PSScriptRoot "../boards/desktop") -Filter *.c)
}

# The board the desktop program simulates: its panel window is drawn from that board's control table (boards/desktop/sim_board.h).
# An app's build.ps1 takes -Board <name> and asks here what the board needs:
#   $board = Get-SimBoard $Board          $null (and a message) for a name that is not known
#   -Flags ... + $board.Flags     -CSources ... + $board.Sources     -ObjDir "build/obj$($board.Suffix)"     -Out "build/<name>$($board.Suffix).exe"
# The board is a compile-time choice, so each board has its own objects and its own program: "panel" (the default) builds <name>.exe,
# any other board <name>_<board>.exe. To add a board: an entry here and its branch in sim_board.h.
function Get-SimBoard([string]$Board = "panel") {
    $boards = Join-Path $PSScriptRoot "../boards"
    switch ($Board) {
        { $_ -in "", "panel" } { return @{ Name = "panel"; Flags = @(); Sources = @(); Suffix = "" } }
        "hwv1" { return @{ Name = "hwv1"; Flags = @("-DSIM_BOARD_HWV1"); Sources = @(Get-Item "$boards/hwv1/hwv1_controls.c"); Suffix = "_hwv1" } }
    }
    Write-Host "No board '$Board' for the desktop build (known: panel, hwv1)" -ForegroundColor Red
    return $null
}

# Does this object need building? (missing, source newer, or any header listed in its .d file newer)
function Test-NeedsBuild($src, $obj, $dep) {
    if (-not (Test-Path $obj)) { return $true }
    $t = (Get-Item $obj).LastWriteTime
    if ((Get-Item $src).LastWriteTime -gt $t) { return $true }
    if (-not (Test-Path $dep)) { return $true }
    $text = (Get-Content $dep -Raw) -replace '\\\r?\n', ' '
    $files = ($text -split '\s+') | Where-Object { $_ -and $_ -notmatch ':$' } | Select-Object -Skip 0
    foreach ($f in $files) { if ((Test-Path $f) -and (Get-Item $f).LastWriteTime -gt $t) { return $true } }
    return $false
}

# Returns $true when the program was built. -Flags go to both compilers (include paths, defines; -MMD is added here), -CFlags / -CppFlags
# to one of them, -Libs to the link (default: SDL2 and what it needs on Windows).
function Invoke-SimBuild {
    param(
        [object[]]$CSources = @(), [object[]]$CppSources = @(),
        [string[]]$Flags = @(), [string[]]$CFlags = @(),
        [string[]]$CppFlags = @("-std=c++17", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Wno-unused-parameter"),
        [string]$ObjDir = "build/obj", [Parameter(Mandatory)][string]$Out,
        [string[]]$Libs = @("-LC:/msys64/ucrt64/lib", "-lSDL2", "-lm", "-lole32", "-lwinmm")
    )
    New-Item -ItemType Directory -Force $ObjDir | Out-Null
    $common = $Flags + @("-MMD")
    $jobs = @()
    $expected = @{}     # objects that belong to a source file that still exists
    foreach ($set in @(@{files = $CSources; tool = "gcc"; flags = $common + $CFlags}, @{files = $CppSources; tool = "g++"; flags = $common + $CppFlags})) {
        foreach ($f in $set.files) {
            if ($f -isnot [IO.FileInfo]) { $f = Get-Item $f }     # (a FileInfo from a recursive listing turns into its bare name as a string)
            $rel = (Resolve-Path -Relative $f.FullName) -replace '^(\.{1,2}[\\/])+', '' -replace '[\\/:]', '_'     # (no leading ..\..\ for the library's files)
            $obj = Join-Path $ObjDir ($rel + ".o")
            $dep = Join-Path $ObjDir ($rel + ".d")
            $expected[(Split-Path $obj -Leaf)] = $true
            if (Test-NeedsBuild $f.FullName $obj $dep) {
                $jobs += [pscustomobject]@{src = $f.FullName; obj = $obj; dep = $dep; tool = $set.tool; flags = $set.flags; name = $f.Name}
            }
        }
    }

    # a source file that was deleted or renamed must not stay in the link through its old object
    Get-ChildItem $ObjDir -Filter *.o | Where-Object { -not $expected.ContainsKey($_.Name) } | ForEach-Object { Remove-Item $_.FullName, ($_.FullName -replace '\.o$', '.d') -ErrorAction SilentlyContinue }

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
                $ccArgs = $j.flags + @("-MF", $j.dep, "-c", $j.src, "-o", $j.obj)
                $p = Start-Process $j.tool -ArgumentList $ccArgs -NoNewWindow -PassThru -RedirectStandardError $log -RedirectStandardOutput "$log.out"
                $null = $p.Handle
                $running += [pscustomobject]@{proc = $p; job = $j; log = $log}
            }
            Start-Sleep -Milliseconds 40
            $still = @()
            foreach ($r in $running) {
                if ($r.proc.HasExited) {
                    $done++
                    $text = @(Get-Content $r.log, "$($r.log).out" -ErrorAction SilentlyContinue)
                    if ($text.Count) { Write-Host ("--- " + $r.job.name) -ForegroundColor DarkGray; $text | ForEach-Object { Write-Host $_ } }
                    if ($r.proc.ExitCode -ne 0) { $failed = $true; Remove-Item $r.job.obj -ErrorAction SilentlyContinue }
                    Write-Host -NoNewline ("`r[{0}/{1}] " -f $done, $jobs.Count)
                } else { $still += $r }
            }
            $running = $still
        }
        Write-Host ""
    }
    if ($failed) { Write-Host "Build FAILED" -ForegroundColor Red; return $false }

    # A running program locks its exe; build next to it instead of failing at the link step.
    if (Test-Path $Out) {
        try { [IO.File]::Open((Resolve-Path $Out), "Open", "ReadWrite", "None").Close() }
        catch {
            $locked = $Out
            $Out = $Out -replace '\.exe$', '_new.exe'
            Write-Host "$(Split-Path $locked -Leaf) is running (locked): building $Out instead" -ForegroundColor Yellow
        }
    }

    $objs = Get-ChildItem $ObjDir -Filter *.o | ForEach-Object { $_.FullName }
    $linkLog = Join-Path $env:TEMP "oled_link.log"
    $p = Start-Process g++ -ArgumentList (@($objs) + $Libs + @("-o", $Out)) -NoNewWindow -PassThru -Wait -RedirectStandardError $linkLog -RedirectStandardOutput "$linkLog.out"
    Get-Content $linkLog, "$linkLog.out" -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    if ($p.ExitCode -eq 0) { Write-Host ("Built {0} in {1:N1}s" -f $Out, $sw.Elapsed.TotalSeconds) -ForegroundColor Green; return $true }
    Write-Host "Link FAILED (exit $($p.ExitCode))" -ForegroundColor Red
    return $false
}
