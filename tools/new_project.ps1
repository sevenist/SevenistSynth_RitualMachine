# Creates a new app from the starter template:
#   .\tools\new_project.ps1 my_synth          makes apps/my_synth, ready to build (.\build.ps1 -App my_synth, then .\run.ps1 -App my_synth)
# The name becomes the folder, the desktop program (build/<name>.exe) and the window title: lower-case letters, digits and _ only.
param([Parameter(Mandatory, Position = 0)][string]$Name, [string]$From = "template")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $root "apps/$From"
$dst = Join-Path $root "apps/$Name"
if ($Name -cnotmatch '^[a-z][a-z0-9_]{1,30}$') { Write-Host "'$Name': use 2 to 31 lower-case letters, digits or _, starting with a letter" -ForegroundColor Red; exit 1 }
if (-not (Test-Path "$src/platformio.ini")) { Write-Host "No app to start from at $src" -ForegroundColor Red; exit 1 }
if (Test-Path $dst) { Write-Host "$dst exists already" -ForegroundColor Red; exit 1 }

New-Item -ItemType Directory $dst | Out-Null
Get-ChildItem $src -Force | Where-Object { $_.Name -notin @("build", ".pio", ".vscode") } | ForEach-Object { Copy-Item $_.FullName -Destination $dst -Recurse }

# The places that carry the app's name. Each must be found: a template that changed shape should fail here, not build a half-renamed app.
function Set-Name([string]$file, [string]$old, [string]$new) {
    $path = Join-Path $dst $file
    $text = [IO.File]::ReadAllText($path)
    if (-not $text.Contains($old)) { Write-Host "${file}: '$old' not found, the new app is not renamed there" -ForegroundColor Red; exit 1 }
    [IO.File]::WriteAllText($path, $text.Replace($old, $new))      # (UTF-8 without a byte-order mark)
}
Set-Name "platformio.ini" "apps/$From" "apps/$Name"
Set-Name "build.ps1" "`$name = `"$From`"" "`$name = `"$Name`""
Set-Name "src/synth.h" "#define APP_NAME `"$From`"" "#define APP_NAME `"$Name`""
Set-Name "README.md" "apps/$From" "apps/$Name"
Set-Name "README.md" "-App $From" "-App $Name"

# VS Code's workspace lists every app as a folder of its own (the PlatformIO extension builds the folder that holds a platformio.ini).
$ws = Join-Path $root "oled_sim.code-workspace"
$folder = '{ "name": "template", "path": "apps/template" }'
$search = '"apps/template": true'
$text = if (Test-Path $ws) { [IO.File]::ReadAllText($ws) } else { "" }
if ($text.Contains($folder) -and $text.Contains($search)) {
    $text = $text.Replace($folder, "$folder,`n        { `"name`": `"$Name`", `"path`": `"apps/$Name`" }").Replace($search, "$search,`n            `"apps/$Name`": true")
    [IO.File]::WriteAllText($ws, $text)
} else { Write-Host "oled_sim.code-workspace not updated: add apps/$Name to its folders by hand" -ForegroundColor Yellow }

Write-Host "Created apps/$Name" -ForegroundColor Green
Write-Host "  .\build.ps1 -App $Name        build its desktop program"
Write-Host "  .\run.ps1 -App $Name          run it"
Write-Host "  C:\.platformio\penv\Scripts\pio.exe run -d apps\$Name        build its firmware (first prototype)"
Write-Host "  apps\$Name\README.md          what each file is and where to change things"
exit 0
