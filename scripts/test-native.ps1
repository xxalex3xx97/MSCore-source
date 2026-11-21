# Compile and run the native unit tests (tests\test_*.cpp that need no game headers). Usage: powershell -File scripts\test-native.ps1
# Compiler: cl (VS 2022 BuildTools, vcvars64) when present, else g++ on PATH (CI). No compiler = SKIP with a message. Exit 1 on failure.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bt   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$inc  = Join-Path $root 'cpp\MSCore'
$tmp  = if ($env:TEMP) { $env:TEMP } else { '/tmp' }
$tests = @('tests\test_presence.cpp', 'tests\test_rockreg.cpp', 'tests\test_designer_stub.cpp', 'tests\test_wclass.cpp')

$vcvars = "$bt\VC\Auxiliary\Build\vcvars64.bat"
$useCl = Test-Path $vcvars
if ($useCl) {
    cmd /c "`"$vcvars`" >nul 2>nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
    }
} elseif (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Host "native tests SKIP: no cl (BuildTools) and no g++ on PATH"; exit 0
}

$bad = 0
foreach ($t in $tests) {
    $src = Join-Path $root $t
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    if ($useCl) {
        $exe = Join-Path $tmp "$name.exe"
        & cl /nologo /std:c++17 /EHsc /W3 /WX /I $inc $src "/Fe:$exe" "/Fo:$tmp\" | Out-Host
        if ($LASTEXITCODE -ne 0) { Write-Host "COMPILE FAILED: $t"; $bad++; continue }
    } else {
        $exe = Join-Path $tmp "$name"
        & g++ -std=c++17 -Wall -Wextra -Werror -I $inc $src -o $exe | Out-Host
        if ($LASTEXITCODE -ne 0) { Write-Host "COMPILE FAILED: $t"; $bad++; continue }
    }
    & $exe
    if ($LASTEXITCODE -ne 0) { Write-Host "TESTS FAILED: $t"; $bad++ }
}
if ($bad) { exit 1 } else { Write-Host "native tests OK"; exit 0 }
