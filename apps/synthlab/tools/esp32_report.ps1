# What a synth costs on the ESP32-S3, read from the compiler's output (no board, no PlatformIO): compiles one synth for the chip (-O2, the
# firmware's flags) and lists, for its Voice's render and its Fx's render, the instructions and every call they make. The calls are
# what to look for: __divsf3 (a float division: the S3 has no instruction for it, each one is a call of dozens of cycles), expf, powf,
# sinf, log10f ... (hundreds of cycles). None should be left in a sample loop; once a block, or on a change of value, is fine.
#   .\apps\synthlab\tools\esp32_report.ps1 kick          src/synths/kick.cpp
# Needs the ESP32 toolchain that PlatformIO installed (C:\.platformio\packages); it runs the compiler only, never pio.
param([Parameter(Mandatory, Position = 0)][string]$Synth)
$ErrorActionPreference = "Stop"
$pkgs = "C:/.platformio/packages"
$libs = "$pkgs/framework-arduinoespressif32-libs/esp32s3"
$cxx = "$pkgs/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++.exe"
if (-not (Test-Path $cxx)) { Write-Host "No ESP32-S3 compiler at $cxx (PlatformIO installs it with the first firmware build)" -ForegroundColor Red; exit 1 }
$root = Resolve-Path (Join-Path $PSScriptRoot "../../..")
$src = Join-Path $PSScriptRoot "../src/synths/$Synth.cpp"
if (-not (Test-Path $src)) { Write-Host "No synth at $src" -ForegroundColor Red; exit 1 }
$outDir = Join-Path $PSScriptRoot "../build/esp32"
New-Item -ItemType Directory -Force $outDir | Out-Null
$asm = Join-Path $outDir "$Synth.s"
Push-Location $root
try {
    & $cxx -S -O2 -std=gnu++17 -fno-exceptions -fno-rtti -fno-stack-protector -mlongcalls "@$libs/flags/defines" -iprefix "$libs/include/" "@$libs/flags/includes" `
        "-I$libs/qio_qspi/include" -Iapps/synthlab/src -Iframework $src -o $asm
    if ($LASTEXITCODE -ne 0) { exit 1 }
} finally { Pop-Location }

$lines = Get-Content $asm
# A function's body: from its label to its .size line. The two renders are the template wrappers lab.h makes (VoiceOps / FxOps).
function Show-Function([string]$what, [string]$pattern) {
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "^$pattern\w*:") { $start = $i; break } }
    if ($start -lt 0) { Write-Host "${what}: none"; return }
    $name = $lines[$start].TrimEnd(':')
    $body = @()
    for ($i = $start + 1; $i -lt $lines.Count -and $lines[$i] -notmatch "^\s*\.size\s+$([regex]::Escape($name))"; $i++) { $body += $lines[$i] }
    $count = @($body | Where-Object { $_ -match '^\s+[a-z]' -and $_ -notmatch '^\s+\.' }).Count
    Write-Host "${what}: $count instructions" -ForegroundColor Cyan
    $calls = $body | ForEach-Object { if ($_ -match '\b(call8|callx8)\s+(\S+)') { $Matches[2] } }
    if (-not $calls) { Write-Host "  no calls"; return }
    $calls | Group-Object | Sort-Object Count -Descending | ForEach-Object {
        $n = $_.Name
        $note = switch -regex ($n) {
            '__divsf3' { 'a float division' }
            '^(expf|exp2f|powf|logf|log2f|log10f|sinf|cosf|tanf|sqrtf|tanhf)$' { 'libm' }
            'update' { 'the synth''s own update (on a change of value?)' }
            default { '' }
        }
        Write-Host ("  {0,3} x {1}  {2}" -f $_.Count, $n, $note)
    }
}
Show-Function "Voice render" "_ZN3lab8VoiceOps.*6render"
Show-Function "Fx render" "_ZN3lab5FxOps.*6render"
Write-Host "Calls are counted where they are written, not how often they run: look in $asm for the loop they sit in."
exit 0
