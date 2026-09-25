param(
    [string]$Source = $PSScriptRoot,
    [switch]$NoShortcut
)

$ErrorActionPreference = "Stop"
$dest = Join-Path $env:LOCALAPPDATA "Programs\Sentinel"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Get-ChildItem -LiteralPath $Source -Force | Where-Object {
    $_.Name -notin @("Install-Sentinel.ps1","Uninstall-Sentinel.ps1")
} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $dest -Recurse -Force
}

if (-not $NoShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $desktop = [Environment]::GetFolderPath("Desktop")
    $shortcut = $shell.CreateShortcut((Join-Path $desktop "Sentinel.lnk"))
    $shortcut.TargetPath = Join-Path $dest "Sentinel.exe"
    $shortcut.WorkingDirectory = $dest
    $shortcut.Description = "Sentinel Secure Evidence & Integrity"
    $shortcut.Save()
}

Write-Host "Sentinel installed to $dest"
Write-Host "Run: $(Join-Path $dest 'Sentinel.exe')"
