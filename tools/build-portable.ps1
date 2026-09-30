param([switch]$RunTests)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeExe = Join-Path $PSScriptRoot 'toolchain/cmake-4.4.3-windows-x86_64/bin/cmake.exe'
$ctestExe = Join-Path $PSScriptRoot 'toolchain/cmake-4.4.3-windows-x86_64/bin/ctest.exe'
if (-not (Test-Path -LiteralPath $cmakeExe)) { throw 'Portable toolchain is absent. Use the documented MSVC presets, or tools/fetch-toolchain.py.' }
Push-Location $projectRoot
try {
    & $cmakeExe --preset windows-portable-clang
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
    & $cmakeExe --build --preset windows-portable-clang-release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if ($RunTests) {
        & $ctestExe --preset windows-portable-clang-release
        if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
    }
    & (Join-Path $PSScriptRoot 'package-release.ps1') -CMakeExecutable $cmakeExe
} finally { Pop-Location }
