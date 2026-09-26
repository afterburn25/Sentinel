[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'Install-Local-AI.ps1')
& (Join-Path $PSScriptRoot 'Configure-Sentinel.ps1')
& (Join-Path $PSScriptRoot 'Start-Sentinel-With-AI.ps1') -NoLaunch
