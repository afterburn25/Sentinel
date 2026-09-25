$ErrorActionPreference = "Stop"
$dest = Join-Path $env:LOCALAPPDATA "Programs\Sentinel"
$shortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "Sentinel.lnk"
if (Test-Path $shortcut) { Remove-Item $shortcut -Force }
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
Write-Host "Sentinel application files removed."
Write-Host "Case/evidence data under LOCALAPPDATA\Sentinel is intentionally preserved."
