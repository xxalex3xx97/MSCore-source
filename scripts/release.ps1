# Build MSCore release into build\release\<Version> (local only: no gh, no git, no upload).
# Outputs: MSCore-<ver>.zip, ue4ss-<ueVer>.zip, MSCoreManager.exe, manifest.txt
# Reads game files from the installed copy (read-only). Usage: powershell -File scripts\release.ps1 -Version 0.7.0
# Env overrides: MS_GAME_DIR (game root), MS_UE4SS_ROOT (RE-UE4SS source tree, same as build.ps1).
# MS_UE4SS_ROOT: build.ps1 compiles MSCore.dll into that tree. If the dev probe build shares the default tree
#   (F:\MSDemoMods\build\RE-UE4SS), set MS_UE4SS_ROOT to an isolated mirror (e.g. F:\mscore-build\RE-UE4SS) first.
# Dry run: there is no -DryRun switch. The script never publishes, tags, uploads or copies zips anywhere.
#   It writes only: the RE-UE4SS build tree (build.ps1), build\MSCoreManager.exe (build-manager.ps1),
#   build\release\<Version>\ (zips, manifest.txt, exe), and a temp dir that is removed at the end. Safe to run.
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$Ue4ssVersion = '3.0.1-beta',
    [string]$MinNative = '0.5.0'   # oldest MSCore native that may be updated by this manifest (kept at 0.5.0)
)
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$game  = if ($env:MS_GAME_DIR) { $env:MS_GAME_DIR } else { 'F:\SteamLibrary\steamapps\common\Main Sequence Demo' }
$re    = if ($env:MS_UE4SS_ROOT) { $env:MS_UE4SS_ROOT } else { 'F:\MSDemoMods\build\RE-UE4SS' }
$win64 = "$game\Windows\MainSequence\Binaries\Win64"
$dll   = "$re\build\Game__Shipping__Win64\bin\MSCore.dll"
$tar   = Join-Path $env:SystemRoot 'System32\tar.exe'   # Git's tar cannot write zip
$utf8  = New-Object System.Text.UTF8Encoding($false)
$rel   = Join-Path $root "build\release\$Version"
$work  = Join-Path $env:TEMP ('msc_release_' + [guid]::NewGuid().ToString('N'))

# version must match mscore.mod
$modFile = Join-Path $root 'mods\MSCore\mscore.mod'
$modVer = [regex]::Match([IO.File]::ReadAllText($modFile), '(?m)^\s*version\s*=\s*(\S+)').Groups[1].Value
if ($modVer -ne $Version) { throw "mscore.mod version '$modVer' != -Version '$Version'" }
Write-Host "note: manifest min_mscore_native=$MinNative (default 0.5.0, kept; pass -MinNative only if a mod needs a newer native)"
if (-not $env:MS_UE4SS_ROOT) { Write-Warning "MS_UE4SS_ROOT not set: building in the shared tree $re. Set it to an isolated mirror if the dev probe build uses that tree." }

# build (no -Install: game dir untouched)
$t0 = Get-Date
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'build.ps1 failed' }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\build-manager.ps1')
if ($LASTEXITCODE -ne 0) { throw 'build-manager.ps1 failed' }
if (-not (Test-Path $dll)) { throw "missing $dll" }
if ((Get-Item $dll).LastWriteTime -lt $t0) { throw 'MSCore.dll not rebuilt' }
$exe = Join-Path $root 'build\MSCoreManager.exe'
if (-not (Test-Path $exe)) { throw "missing $exe" }

# game UE4SS files (read-only source)
$ue = Join-Path $win64 'ue4ss'
$ueFiles = @((Join-Path $win64 'dwmapi.dll'), (Join-Path $ue 'UE4SS.dll'), (Join-Path $ue 'UE4SS-settings.ini'), (Join-Path $ue 'LICENSE'))
foreach ($f in $ueFiles) { if (-not (Test-Path -LiteralPath $f)) { throw "missing game file $f (paths differ?)" } }
$third = Join-Path $ue 'THIRD-PARTY.txt'
$hasThird = Test-Path -LiteralPath $third

try {
    New-Item -ItemType Directory -Force $work | Out-Null

    # MSCore zip: Mods/MSCore/{dlls/main.dll, Scripts/*.lua, mscore.mod, enabled.txt}
    $ms = Join-Path $work 'mscore'
    $mm = Join-Path $ms 'Mods\MSCore'
    New-Item -ItemType Directory -Force "$mm\dlls", "$mm\Scripts" | Out-Null
    Copy-Item -LiteralPath $dll "$mm\dlls\main.dll"
    Copy-Item -Path (Join-Path $root 'mods\MSCore\Scripts\*.lua') -Destination "$mm\Scripts\"
    Copy-Item -LiteralPath $modFile "$mm\mscore.mod"
    $en = Join-Path $root 'mods\MSCore\enabled.txt'
    if (Test-Path $en) { Copy-Item -LiteralPath $en "$mm\enabled.txt" }
    $zipName = "MSCore-$Version.zip"

    # ue4ss zip: dwmapi.dll at root, ue4ss/{UE4SS.dll, UE4SS-settings.ini, licenses}
    $us = Join-Path $work 'ue4ss'
    New-Item -ItemType Directory -Force "$us\ue4ss" | Out-Null
    Copy-Item -LiteralPath $ueFiles[0] "$us\dwmapi.dll"
    Copy-Item -LiteralPath $ueFiles[1] "$us\ue4ss\UE4SS.dll"
    Copy-Item -LiteralPath $ueFiles[2] "$us\ue4ss\UE4SS-settings.ini"
    Copy-Item -LiteralPath $ueFiles[3] "$us\ue4ss\UE4SS-LICENSE.txt"
    if ($hasThird) { Copy-Item -LiteralPath $third "$us\ue4ss\THIRD-PARTY.txt" }
    $ueZipName = "ue4ss-$Ue4ssVersion.zip"

    if (Test-Path $rel) { Remove-Item -LiteralPath $rel -Recurse -Force }
    New-Item -ItemType Directory -Force $rel | Out-Null
    $zipPath = Join-Path $rel $zipName
    $uePath  = Join-Path $rel $ueZipName
    & $tar -a -cf $zipPath -C $ms Mods
    if ($LASTEXITCODE -ne 0) { throw 'tar (MSCore zip) failed' }
    $ueItems = @('dwmapi.dll', 'ue4ss')
    & $tar -a -cf $uePath -C $us $ueItems
    if ($LASTEXITCODE -ne 0) { throw 'tar (ue4ss zip) failed' }
    Copy-Item -LiteralPath $exe (Join-Path $rel 'MSCoreManager.exe')

    # manifest (BOM-less, LF)
    $zSha = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLower()
    $uSha = (Get-FileHash -LiteralPath $uePath -Algorithm SHA256).Hash.ToLower()
    $lines = @(
        'name=MSCore'
        "version=$Version"
        "zip=$zipName"
        "sha256=$zSha"
        "size=$((Get-Item $zipPath).Length)"
        "min_mscore_native=$MinNative"
        "ue4ss_version=$Ue4ssVersion"
        "ue4ss_zip=$ueZipName"
        "ue4ss_sha256=$uSha"
        "ue4ss_size=$((Get-Item $uePath).Length)"
    )
    [IO.File]::WriteAllText((Join-Path $rel 'manifest.txt'), (($lines -join "`n") + "`n"), $utf8)
} finally {
    if (Test-Path $work) { Remove-Item -LiteralPath $work -Recurse -Force }
}

Get-ChildItem -LiteralPath $rel | ForEach-Object { "{0}  {1:N0} B" -f $_.Name, $_.Length }
if (-not $hasThird) { Write-Warning 'THIRD-PARTY.txt not found in game ue4ss dir: not shipped in ue4ss zip' }
"release: $rel"
