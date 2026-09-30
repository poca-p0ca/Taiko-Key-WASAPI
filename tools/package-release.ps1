param(
    [string]$BuildDirectory = 'build/portable',
    [string]$Configuration = 'Release',
    [string]$CMakeExecutable = 'cmake',
    [string]$OutputDirectory = 'dist/releases'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $projectText = Get-Content -Raw -LiteralPath 'CMakeLists.txt'
    $match = [regex]::Match($projectText, 'project\(TaikoKeyWASAPI VERSION (\d+\.\d+\.\d+)')
    if (-not $match.Success) { throw 'Project version not found' }
    $version = $match.Groups[1].Value
    # Fresh staging avoids packaging old licenses, reports or a running local EXE.
    $stage = Join-Path $projectRoot ('build/release-staging/' + [guid]::NewGuid().ToString('N'))
    $package = Join-Path $stage 'TaikoKeyWASAPI'
    & $CMakeExecutable --install $BuildDirectory --config $Configuration --prefix $package
    if ($LASTEXITCODE -ne 0) { throw 'CMake install failed' }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $zip = Join-Path $OutputDirectory "TaikoKeyWASAPI-$version-windows-x64.zip"
    Compress-Archive -LiteralPath $package -DestinationPath $zip -Force
    $checksum = "$zip.sha256"
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($zip))" | Set-Content -LiteralPath $checksum -Encoding ascii
    if ($env:GITHUB_OUTPUT) {
        "version=$version" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
        "zip=$zip" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
        "checksum=$checksum" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
    }
    Write-Output "Release package: $zip"
    Write-Output "SHA-256: $checksum"
} finally {
    Pop-Location
}
