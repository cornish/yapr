# Builds a portable yapr zip: dist/yapr-win64-portable.zip
# Requires MSYS2 UCRT64 with qt6-base, qt6-pdf, qt6-tools installed.
param(
    [string]$Msys2Root = 'C:\msys64'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "MSYS2 bash not found at $bash" }

$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
& $bash -l -c "'$($PSScriptRoot -replace '\\','/' -replace '^([A-Za-z]):','/$1')/package-windows.sh'"
if ($LASTEXITCODE -ne 0) { throw "package-windows.sh failed (exit $LASTEXITCODE)" }

$distDir = Join-Path $repoRoot 'dist\yapr'
$zipPath = Join-Path $repoRoot 'dist\yapr-win64-portable.zip'
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path $distDir -DestinationPath $zipPath
Write-Host "portable zip ready: $zipPath"
