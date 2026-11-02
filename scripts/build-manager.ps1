# Build MSCoreManager.exe (Win32 GUI, MSVC /MT). Usage: powershell -File scripts\build-manager.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$mgr  = Join-Path $root 'cpp\manager'
$out  = Join-Path $root 'build'
$bt   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'

cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $out 'MSCoreManager.exe'

Push-Location $mgr
try {
    & rc /nologo "/fo$out\manager.res" manager.rc
    if ($LASTEXITCODE -ne 0) { throw "RC FAILED" }
    & cl /nologo /std:c++17 /EHsc /MT /O2 /DUNICODE /D_UNICODE /I "$root\cpp\MSCore" main.cpp "$out\manager.res" "/Fe:$exe" "/Fo:$out\main.obj" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib winhttp.lib bcrypt.lib
    if ($LASTEXITCODE -ne 0) { throw "COMPILE FAILED" }
} finally {
    Pop-Location
}

$f = Get-Item $exe
"{0}  {1:N0} B  {2}" -f $f.Name, $f.Length, $f.LastWriteTime
