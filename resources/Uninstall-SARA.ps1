$ErrorActionPreference = "Stop"
$dest = Join-Path $env:LOCALAPPDATA "Programs\SARA"
$shortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "SARA.lnk"

if (Test-Path $shortcut) { Remove-Item $shortcut -Force }
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }

Write-Host "SARA application files removed."
Write-Host "Case, evidence, model, persona, memory, and settings data under LOCALAPPDATA\Sentinel is intentionally preserved."
