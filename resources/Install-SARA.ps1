param(
    [string]$Source = $PSScriptRoot,
    [switch]$NoShortcut
)

$ErrorActionPreference = "Stop"
$dest = Join-Path $env:LOCALAPPDATA "Programs\SARA"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Get-ChildItem -LiteralPath $Source -Force | Where-Object {
    $_.Name -notin @("Install-SARA.ps1","Uninstall-SARA.ps1","Install-Sentinel.ps1","Uninstall-Sentinel.ps1")
} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $dest -Recurse -Force
}

if (-not (Test-Path (Join-Path $dest "SARA.exe"))) {
    throw "SARA.exe was not found after installation."
}

if (-not $NoShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $desktop = [Environment]::GetFolderPath("Desktop")
    $shortcut = $shell.CreateShortcut((Join-Path $desktop "SARA.lnk"))
    $shortcut.TargetPath = Join-Path $dest "SARA.exe"
    $shortcut.WorkingDirectory = $dest
    $shortcut.Description = "SARA - Synthetic Adaptive Response Agent"
    $shortcut.Save()
}

Write-Host "SARA installed to $dest"
Write-Host "Run: $(Join-Path $dest 'SARA.exe')"
Write-Host "Existing application data under LOCALAPPDATA\Sentinel is preserved for compatibility."
