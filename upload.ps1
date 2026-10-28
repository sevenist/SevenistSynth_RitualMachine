# Builds an app's firmware and flashes it on the board, from the repo root (PlatformIO's own Upload only works with the app's
# folder open, the one that holds platformio.ini):
#   .\upload.ps1                         the Ritual Machine on the board found on USB
#   .\upload.ps1 -Port COM8              on that port
#   .\upload.ps1 -App template           another app of apps/
#   .\upload.ps1 -Env card_reader        another environment of the app's platformio.ini (the card reader's firmware)
#   .\upload.ps1 -BuildOnly              build, flash nothing
# It always uses PlatformIO's own pio.exe (a bare `pio` on PATH can be Python 3.13's, which breaks PlatformIO's environment:
# DEVELOPING.md, "ESP32 firmware").
param([string]$App = "ritual_machine", [string]$Port = "", [string]$Env = "", [switch]$BuildOnly)
$pio = "C:\.platformio\penv\Scripts\pio.exe"
$dir = Join-Path $PSScriptRoot "apps/$App"
if (-not (Test-Path $pio)) { Write-Host "PlatformIO not found at $pio" -ForegroundColor Red; exit 1 }
if (-not (Test-Path "$dir/platformio.ini")) { Write-Host "No firmware for '$App': $dir/platformio.ini is missing" -ForegroundColor Red; exit 1 }
$pioArgs = @("run", "-d", $dir)
if ($Env) { $pioArgs += @("-e", $Env) }
if (-not $BuildOnly) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($Port -and $ports -notcontains $Port) {
        Write-Host "No board on $Port (ports now: $($ports -join ', ')). Is it switched on and plugged in? Is a serial monitor holding it?" -ForegroundColor Red
        exit 1
    }
    if (-not $ports) { Write-Host "No serial port at all: the board is off or unplugged" -ForegroundColor Red; exit 1 }
    $pioArgs += @("-t", "upload")
    if ($Port) { $pioArgs += @("--upload-port", $Port) }
}
& $pio @pioArgs
exit $LASTEXITCODE
