# Build + run msc_loader tests (no network). Usage: powershell -File scripts\test-loader.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bt   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'

cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}

$exe = Join-Path $env:TEMP 'msc_loader_test.exe'
$obj = $env:TEMP + '\'
& cl /nologo /std:c++17 /EHsc /MT /W3 /I "$root\cpp\MSCore" "$root\cpp\loader_test\test.cpp" "/Fe:$exe" "/Fo:$obj"
if ($LASTEXITCODE -ne 0) { throw "COMPILE FAILED" }

& $exe
if ($LASTEXITCODE -ne 0) { throw "TESTS FAILED (exit $LASTEXITCODE)" }
