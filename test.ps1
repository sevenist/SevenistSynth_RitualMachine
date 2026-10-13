# Runs every host test of the repo: the library's own tests (the engine alone), then the Ritual Machine's engine tests (the engine
# driven through the app's rack, pads and sequencers) and its UI tests.
#   .\test.ps1               the default configuration (48 kHz, blocks of 32)
#   .\test.ps1 -Matrix       the engine tests in the six supported sample-rate / block-size configurations
#   .\test.ps1 -Filter seq   only the tests whose name contains the text
#   .\test.ps1 -Defs "-DENGINE_REVERB_HALF=1"      engine flags for the two engine runs (framework/engine/dsp/config.h)
param([switch]$Matrix, [string]$Filter = "", [string]$Defs = "")
$pass = @{}
if ($Matrix) { $pass.Matrix = $true }
if ($Filter) { $pass.Filter = $Filter }
if ($Defs) { $pass.Defs = $Defs }
$failed = @()

Write-Host "##### library tests (framework/tests)" -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "framework/tools/build_engine_tests.ps1") @pass
if ($LASTEXITCODE -ne 0) { $failed += "library" }

Write-Host "##### Ritual Machine, engine tests (apps/ritual_machine/tests/engine)" -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "apps/ritual_machine/tools/build_engine_tests.ps1") @pass
if ($LASTEXITCODE -ne 0) { $failed += "ritual_machine engine" }

Write-Host "##### Ritual Machine, UI tests (apps/ritual_machine/tests/ui)" -ForegroundColor Cyan
if ($Filter) { & (Join-Path $PSScriptRoot "apps/ritual_machine/tools/build_ui_tests.ps1") -Filter $Filter }
else { & (Join-Path $PSScriptRoot "apps/ritual_machine/tools/build_ui_tests.ps1") }
if ($LASTEXITCODE -ne 0) { $failed += "ritual_machine UI" }

if ($failed.Count) { Write-Host "FAILED: $($failed -join ', ')" -ForegroundColor Red; exit 1 }
Write-Host "All test runs passed" -ForegroundColor Green
exit 0
