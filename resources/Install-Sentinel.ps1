param(
    [string]$Source = $PSScriptRoot,
    [switch]$NoShortcut
)
Write-Warning "Install-Sentinel.ps1 is retained for compatibility. Using the SARA installer helper."
& (Join-Path $PSScriptRoot "Install-SARA.ps1") -Source $Source -NoShortcut:$NoShortcut
