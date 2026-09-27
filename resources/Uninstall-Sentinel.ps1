$ErrorActionPreference = "Stop"
Write-Warning "Uninstall-Sentinel.ps1 is retained for compatibility. Using the SARA uninstaller helper."
& (Join-Path $PSScriptRoot "Uninstall-SARA.ps1")
