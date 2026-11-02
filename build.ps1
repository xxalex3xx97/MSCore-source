# Build MSCore native DLL, optionally install DLL + Lua into the game. Usage: .\build.ps1 [-Install]
# Env overrides: MS_UE4SS_ROOT = RE-UE4SS source tree (build root), MS_GAME_DIR = game root (folder that contains Windows\).
param([switch]$Install)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$re   = if ($env:MS_UE4SS_ROOT) { $env:MS_UE4SS_ROOT } else { 'F:\MSDemoMods\build\RE-UE4SS' }
$game = if ($env:MS_GAME_DIR) { $env:MS_GAME_DIR } else { 'F:\SteamLibrary\steamapps\common\Main Sequence Demo' }
$bt   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$mod  = "$game\Windows\MainSequence\Binaries\Win64\ue4ss\Mods\MSCore"

# mirror sources into the RE-UE4SS tree (build compiles from there)
New-Item -ItemType Directory -Force "$re\cppmods\MSCore" | Out-Null
Copy-Item "$root\cpp\MSCore\*.cpp","$root\cpp\MSCore\*.hpp","$root\cpp\MSCore\CMakeLists.txt" "$re\cppmods\MSCore\" -Force -ErrorAction SilentlyContinue
$cm = "$re\cppmods\CMakeLists.txt"
if (-not (Select-String -Path $cm -SimpleMatch 'add_subdirectory("MSCore")' -Quiet)) {
    (Get-Content $cm -Raw).Replace('add_subdirectory("MiningBenchMod")', "add_subdirectory(`"MiningBenchMod`")`r`nadd_subdirectory(`"MSCore`")") | Set-Content $cm -NoNewline
}

cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$env:PATH = "$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"

Set-Location $re
cmake --build build --target MSCore
if ($LASTEXITCODE -ne 0) { throw "BUILD FAILED" }

$dll = "$re\build\Game__Shipping__Win64\bin\MSCore.dll"
$f = Get-Item $dll
"{0}  {1:N0} B  {2}" -f $f.Name, $f.Length, $f.LastWriteTime
if ((Get-Date) - $f.LastWriteTime -gt [TimeSpan]::FromMinutes(5)) { Write-Warning "DLL is older than 5 min - build may be stale" }

if ($Install) {
    if (Get-Process -Name 'MainSequence*' -ErrorAction SilentlyContinue) { throw "Game is running - close it first" }
    $stamp = Get-Date -f yyyyMMdd-HHmmss
    $bk = "$root\build\backups\$stamp"
    New-Item -ItemType Directory -Force $bk | Out-Null
    if (Test-Path "$mod\dlls\main.dll") { Copy-Item "$mod\dlls\main.dll" "$bk\main.dll" }
    if (Test-Path "$mod\Scripts") { Copy-Item "$mod\Scripts" "$bk\Scripts" -Recurse }
    New-Item -ItemType Directory -Force "$mod\dlls","$mod\Scripts" | Out-Null
    Copy-Item $dll "$mod\dlls\main.dll" -Force
    Copy-Item "$root\mods\MSCore\Scripts\*.lua" "$mod\Scripts\" -Force
    "Installed. Backup: $bk"
}
