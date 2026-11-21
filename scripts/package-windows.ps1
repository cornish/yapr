# Builds the Windows release artifacts into dist\:
#   yapr-<version>-windows-x64-portable.zip  (always)
#   yapr-<version>-windows-x64-setup.exe     (if Inno Setup's ISCC is found)
# Requires MSYS2 UCRT64 with gcc, cmake, ninja, qt6-base, qt6-pdf, qt6-tools.
# Expects an up-to-date Release build in build\ (cmake --build build).
param(
    [string]$Msys2Root = 'C:\msys64'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# Version comes from CMakeLists.txt, the single source of truth.
$version = (Select-String -Path "$repoRoot\CMakeLists.txt" -Pattern '^\s*VERSION\s+(\d+\.\d+\.\d+)').Matches[0].Groups[1].Value

$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "MSYS2 bash not found at $bash" }

# Assemble dist\yapr (exe + Qt + runtime DLLs). windeployqt prints warnings
# to stderr; under 'Stop' PowerShell 5.1 treats any native stderr output as
# fatal, so judge success by exit code only.
$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
$ErrorActionPreference = 'Continue'
& $bash -l -c "'$($PSScriptRoot -replace '\\','/' -replace '^([A-Za-z]):','/$1')/package-windows.sh'" 2>&1 | ForEach-Object { "$_" }
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw "package-windows.sh failed (exit $LASTEXITCODE)" }

$distRoot = Join-Path $repoRoot 'dist'
$appDir = Join-Path $distRoot 'yapr'

# Portable zip: same files plus a "portable" marker, which makes yapr keep
# its settings in a data\ folder beside the exe instead of %APPDATA%.
$staging = Join-Path $distRoot 'portable-staging'
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging | Out-Null
Copy-Item $appDir (Join-Path $staging 'yapr') -Recurse
Set-Content -Path (Join-Path $staging 'yapr\portable') -Value 'Settings are stored in the data folder next to yapr.exe. Delete this file to use the normal per-user location.' -Encoding ascii
$zipPath = Join-Path $distRoot "yapr-$version-windows-x64-portable.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path (Join-Path $staging 'yapr') -DestinationPath $zipPath
Remove-Item $staging -Recurse -Force
Write-Host "portable zip ready: $zipPath"

# Installer, when Inno Setup is available.
$iscc = @(
    (Get-Command iscc -ErrorAction SilentlyContinue).Source,
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($iscc) {
    & $iscc /Q "/DAppVersion=$version" "/DSourceDir=$appDir" "/DOutputDir=$distRoot" `
        (Join-Path $repoRoot 'packaging\windows\yapr.iss')
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed (exit $LASTEXITCODE)" }
    Write-Host "installer ready: $(Join-Path $distRoot "yapr-$version-windows-x64-setup.exe")"
} else {
    Write-Host 'Inno Setup (ISCC) not found - skipping installer.'
}
