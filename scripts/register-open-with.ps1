# Registers yapr in the "Open with" list for .pdf files (current user only,
# no admin needed). Windows protects the *default* association, so after
# running this: right-click a PDF -> Open with -> yapr -> "Always".
#
# Usage:
#   .\register-open-with.ps1 [-ExePath <path\to\yapr.exe>]
#   .\register-open-with.ps1 -Unregister
param(
    [string]$ExePath,
    [switch]$Unregister
)

$ErrorActionPreference = 'Stop'
$progId = 'yapr.pdf'
$classes = 'HKCU:\Software\Classes'

if ($Unregister) {
    if (Test-Path "$classes\$progId") {
        Remove-Item "$classes\$progId" -Recurse -Force -Confirm:$false
    }
    $openWith = "$classes\.pdf\OpenWithProgids"
    if (Test-Path $openWith) {
        try { Remove-ItemProperty -Path $openWith -Name $progId -ErrorAction Stop } catch {}
    }
    Write-Host 'yapr unregistered from Open with.'
    return
}

if (-not $ExePath) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    foreach ($candidate in @("$repoRoot\dist\yapr\yapr.exe", "$repoRoot\build\yapr.exe")) {
        if (Test-Path $candidate) { $ExePath = $candidate; break }
    }
}
if (-not $ExePath -or -not (Test-Path $ExePath)) {
    throw 'yapr.exe not found - pass -ExePath or build/package first.'
}
$ExePath = (Resolve-Path $ExePath).Path

New-Item -Path "$classes\$progId\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path "$classes\$progId" -Name '(Default)' -Value 'PDF Document (yapr)'
Set-ItemProperty -Path "$classes\$progId\shell\open\command" -Name '(Default)' `
    -Value "`"$ExePath`" `"%1`""
New-Item -Path "$classes\$progId\DefaultIcon" -Force | Out-Null
Set-ItemProperty -Path "$classes\$progId\DefaultIcon" -Name '(Default)' -Value "`"$ExePath`",0"

New-Item -Path "$classes\.pdf\OpenWithProgids" -Force | Out-Null
Set-ItemProperty -Path "$classes\.pdf\OpenWithProgids" -Name $progId -Value ''

Write-Host "Registered $ExePath for 'Open with' on .pdf files."
Write-Host "To make it the default: right-click a PDF -> Open with -> Choose another app -> yapr -> Always."
