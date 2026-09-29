param(
    [string]$IsccPath = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$versionFile = Join-Path $repo "VERSION"
$installer = Join-Path $repo "installer\Sentinel.iss"

if (-not (Test-Path -LiteralPath $versionFile)) {
    throw "SARA VERSION file is missing: $versionFile"
}
$version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Invalid SARA version: $version"
}
if (-not (Test-Path -LiteralPath $IsccPath)) {
    throw "Inno Setup compiler not found: $IsccPath"
}

Write-Host "Building SARA Setup $version"
& $IsccPath "/DMyAppVersion=$version" $installer
if ($LASTEXITCODE -ne 0) {
    throw "SARA installer build failed for version $version"
}

$setup = Join-Path $repo "installer\output\SARA-Setup-$version.exe"
if (-not (Test-Path -LiteralPath $setup)) {
    throw "Expected installer output was not created: $setup"
}
Write-Host "SARA installer created: $setup"
