# Builds what a release holds, into build/release (not versioned). Run it from anywhere:
#   .\tools\make_release.ps1 -Version beta-1.1          (-SkipFirmware / -SkipSimulator: one of the two only)
#
#   ritual-machine-<version>-hwv1.bin
#       One image of the whole flash for the first prototype (HWV1), written at address 0 with esptool: the bootloader, the partition
#       table, the synth's firmware in the first application slot ([env:release] of platformio.ini) and the USB card reader's in the
#       second ([env:card_reader], 0x650000).
#           esptool --chip esp32s3 write-flash 0x0 ritual-machine-<version>-hwv1.bin
#   ritual-machine-simulator-<version>-win64.zip
#       The desktop simulator, one program per board it draws (build.ps1 -Board: ritual-machine-sim-hwv1.exe, the first prototype,
#       and ritual-machine-sim-panel.exe, the next panel), with the DLLs they need, a card folder holding the synthetic demo samples
#       of framework/tools/make_demo_samples.py (nothing from samples/: those files are the user's own), a readme and the licence
#       notices.
param([string]$Version = "beta-1.1", [switch]$SkipFirmware, [switch]$SkipSimulator)
$ErrorActionPreference = "Continue"
$pio = "C:\.platformio\penv\Scripts\pio.exe"            # never a bare pio / python (CONTINUE.md, traps)
$py  = "C:\.platformio\penv\Scripts\python.exe"
$msys = "C:\msys64\ucrt64"
$out = "build/release"
$fw = "../../framework"
Push-Location (Join-Path $PSScriptRoot "..")
try {
New-Item -ItemType Directory -Force $out | Out-Null

if (-not $SkipFirmware) {
    & $pio run -e release -e card_reader
    if ($LASTEXITCODE -ne 0) { Write-Host "Firmware build FAILED" -ForegroundColor Red; exit 1 }
    $img = "$out/ritual-machine-$Version-hwv1.bin"
    # firmware.factory.bin is PlatformIO's own image of the first slot (bootloader 0x0, partitions 0x8000, boot_app0 0xe000, the
    # application 0x10000); the card reader goes behind it, in the second slot.
    & $py -m esptool --chip esp32s3 merge-bin -o $img 0x0 .pio/build/release/firmware.factory.bin 0x650000 .pio/build/card_reader/firmware.bin
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $img)) { Write-Host "Image merge FAILED" -ForegroundColor Red; exit 1 }
    Write-Host ("{0}: {1:N0} bytes" -f $img, (Get-Item $img).Length) -ForegroundColor Green
}

if (-not $SkipSimulator) {
    $env:PATH = "$msys\bin;" + $env:PATH
    $name = "ritual-machine-simulator-$Version-win64"
    $stage = "$out/$name"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    foreach ($d in @("", "licenses", "sdcard/system/samples", "sdcard/system/config", "sdcard/import", "sdcard/presets")) { New-Item -ItemType Directory -Force "$stage/$d" | Out-Null }
    # one program per board the panel window draws (framework/tools/sim_build.ps1, Get-SimBoard): build/oled_sim<suffix>.exe
    foreach ($board in @(@{ Name = "hwv1"; Built = "oled_sim_hwv1" }, @{ Name = "panel"; Built = "oled_sim" })) {
        & .\build.ps1 -NoSamples -Board $board.Name
        if ($LASTEXITCODE -ne 0) { Write-Host "Simulator build FAILED ($($board.Name))" -ForegroundColor Red; exit 1 }
        $exe = Get-ChildItem "build/$($board.Built).exe", "build/$($board.Built)_new.exe" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
        Copy-Item $exe.FullName "$stage/ritual-machine-sim-$($board.Name).exe"
    }
    foreach ($dll in @("SDL2.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")) { Copy-Item "$msys\bin\$dll" $stage }

    # The demo samples, made in a scratch folder: samples/ is the user's.
    $scratch = "$out/_demo"
    if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
    New-Item -ItemType Directory -Force $scratch | Out-Null
    & $py "$fw/tools/make_demo_samples.py" $scratch | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host "Demo samples FAILED" -ForegroundColor Red; exit 1 }
    Copy-Item "$scratch/*.smp" "$stage/sdcard/system/samples"
    Remove-Item -Recurse -Force $scratch

    # (a file in each folder: a zip does not keep an empty one, and the simulator would ask to create it)
    Set-Content "$stage/sdcard/presets/readme.txt" "Presets (*.preset), in sub-folders if you like. Menu > PRESETS > Save as writes them here." -Encoding ascii
    Set-Content "$stage/sdcard/import/readme.txt" "Reserved for .wav / .mp3 files to import." -Encoding ascii
    Set-Content "$stage/sdcard/system/config/readme.txt" "The machine's settings and the sound as it was left (written by the simulator)." -Encoding ascii
    (Get-Content tools/release/README-simulator.txt -Raw).Replace("{VERSION}", $Version) | Set-Content "$stage/README.txt" -Encoding ascii -NoNewline
    Copy-Item ../../THIRD_PARTY_NOTICES.md $stage
    Copy-Item "$fw/engine/mi/LICENSE" "$stage/licenses/MutableInstruments-LICENSE.txt"
    Copy-Item ../../lib/u8g2/LICENSE "$stage/licenses/U8g2-LICENSE.txt"
    Copy-Item "$msys\share\licenses\SDL2\LICENSE.txt" "$stage/licenses/SDL2-LICENSE.txt"

    $zip = "$out/$name.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path $stage -DestinationPath $zip
    Write-Host ("{0}: {1:N0} bytes" -f $zip, (Get-Item $zip).Length) -ForegroundColor Green
}
} finally { Pop-Location }
