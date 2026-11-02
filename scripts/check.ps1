# Lua syntax check (luac -p) for every .lua under mods\. Env override: MS_LUAC = path to luac.exe. Exit 1 on any error.
$luac = if ($env:MS_LUAC) { $env:MS_LUAC } else { 'F:\MSDemoMods-release\tests\out\luac.exe' }
$bad = 0
Get-ChildItem -Recurse -Filter *.lua (Join-Path (Split-Path -Parent $PSScriptRoot) 'mods') | ForEach-Object {
    & $luac -p $_.FullName; if ($LASTEXITCODE -ne 0) { $bad++ } }
if ($bad) { Write-Host "$bad file(s) with syntax errors"; exit 1 } else { Write-Host "lua syntax OK"; exit 0 }
