# Build + run the msc_refund.hpp fake-function harness (no game, no UE4SS). Usage: powershell -File scripts\test-refund.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bt   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'

cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}

# the real msc_refund.hpp + msc_pattern.hpp next to the FAKE msc_natives.hpp (quote includes search the including file's folder first)
$dir = Join-Path $env:TEMP 'msc_refund_test'
New-Item -ItemType Directory -Force $dir | Out-Null
Copy-Item "$root\cpp\MSCore\msc_refund.hpp", "$root\cpp\MSCore\msc_pattern.hpp", "$root\cpp\refund_test\msc_natives.hpp", "$root\cpp\refund_test\test.cpp" $dir -Force

$exe = Join-Path $dir 'refund_test.exe'
& cl /nologo /std:c++20 /EHsc /MT /W3 "/Fe:$exe" "/Fo:$dir\" "$dir\test.cpp"
if ($LASTEXITCODE -ne 0) { throw "COMPILE FAILED" }

& $exe
if ($LASTEXITCODE -ne 0) { throw "TESTS FAILED (exit $LASTEXITCODE)" }
