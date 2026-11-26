# Create / refresh the UE4SS v4 build tree used by build.ps1 (MSCore, Fleet, ShipControl, MiningBenchMod all build inside it).
# Usage: powershell -File scripts\setup-ue4ss-v4.ps1 [-Tree F:\mscore-build\RE-UE4SS-v4] [-Tag v4.0.0]
# Clones RE-UE4SS at the tag, inits submodules (no --remote), configures with the same toolchain as the old 3.0.1 tree,
# and registers the mods' cppmods folders (build.ps1 copies the sources in and adds add_subdirectory itself).
param([string]$Tree = 'F:\mscore-build\RE-UE4SS-v4', [string]$Tag = 'v4.0.0')
$ErrorActionPreference = 'Stop'
$bt = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
if (-not (Test-Path "$Tree\.git")) { git clone https://github.com/UE4SS-RE/RE-UE4SS.git $Tree; if ($LASTEXITCODE) { throw 'clone failed' } }
git -C $Tree fetch --tags --quiet
git -C $Tree checkout $Tag; if ($LASTEXITCODE) { throw "checkout $Tag failed" }
git -C $Tree submodule update --init --recursive; if ($LASTEXITCODE) { throw 'submodules failed' }
# cppmods: build.ps1 of each mod repo mirrors its sources here. ShipControl comes from the Fleet repo's submodule (external\ShipControl).
$cm = "$Tree\cppmods\CMakeLists.txt"
$t = [IO.File]::ReadAllText($cm)
foreach ($m in 'MSCore', 'ShipControl', 'Fleet', 'MiningBenchMod') {
    New-Item -ItemType Directory -Force "$Tree\cppmods\$m" | Out-Null
    if ($t -notmatch "add_subdirectory\(`"$m`"\)") { $t = $t.Replace('add_subdirectory("EventViewerMod")', "add_subdirectory(`"EventViewerMod`")`r`nadd_subdirectory(`"$m`")") }
}
[IO.File]::WriteAllText($cm, $t)
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] } }
$env:PATH = "$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
Set-Location $Tree
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DBUILD_SHARED_LIBS=OFF -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc" -DFETCHCONTENT_BASE_DIR="$Tree-deps" -DUE4SS_SUPPRESS_THIRD_PARTY_WARNINGS=ON
if ($LASTEXITCODE) { throw 'configure failed' }
"tree ready: $Tree ($Tag). Build UE4SS + dwmapi: cmake --build build --target UE4SS proxy"
