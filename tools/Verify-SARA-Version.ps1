param()

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

function Read-Text([string]$relative) {
    $path = Join-Path $repo $relative
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required version-contract file is missing: $relative"
    }
    return Get-Content -LiteralPath $path -Raw
}

$version = (Read-Text "VERSION").Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Invalid SARA VERSION value: $version"
}

$cmake = Read-Text "CMakeLists.txt"
$rc = Read-Text "resources\SentinelVersion.rc"
$installer = Read-Text "installer\Sentinel.iss"
$workflow = Read-Text ".github\workflows\windows-build.yml"
$installerBuilder = Read-Text "tools\Build-SARA-Installer.ps1"

foreach ($token in @(
    'file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SARA_VERSION LIMIT_COUNT 1)',
    'project(SARA VERSION ${SARA_VERSION}',
    'configure_file(',
    'SARA_VERSION_STR="${PROJECT_VERSION}"'
)) {
    if (-not $cmake.Contains($token)) {
        throw "CMake version contract is missing: $token"
    }
}

foreach ($token in @(
    'FILEVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0',
    'PRODUCTVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0',
    'VALUE "FileVersion", "@PROJECT_VERSION@.0\0"',
    'VALUE "ProductVersion", "@PROJECT_VERSION@.0\0"'
)) {
    if (-not $rc.Contains($token)) {
        throw "Windows version-resource contract is missing: $token"
    }
}

foreach ($token in @(
    '#ifndef MyAppVersion',
    '#error MyAppVersion must be supplied from the repository VERSION file.',
    'AppVersion={#MyAppVersion}',
    'OutputBaseFilename=SARA-Setup-{#MyAppVersion}',
    'VersionInfoVersion={#MyAppVersion}.0',
    'VersionInfoProductVersion={#MyAppVersion}.0',
    '#define PackageDir "..\package\SARA-windows-x64"'
)) {
    if (-not $installer.Contains($token)) {
        throw "Installer version contract is missing: $token"
    }
}

foreach ($token in @(
    'Get-Content -LiteralPath $versionFile -Raw',
    '"/DMyAppVersion=$version"',
    '"installer\output\SARA-Setup-$version.exe"'
)) {
    if (-not $installerBuilder.Contains($token)) {
        throw "Installer build helper version contract is missing: $token"
    }
}

foreach ($token in @(
    'id: sara_version',
    'Get-Content -LiteralPath VERSION -Raw',
    'SARA-${{ steps.sara_version.outputs.version }}-windows-x64',
    'SARA-Setup-${{ steps.sara_version.outputs.version }}',
    '--prefix package/SARA-windows-x64'
)) {
    if (-not $workflow.Contains($token)) {
        throw "Windows workflow version contract is missing: $token"
    }
}

$forbiddenCurrentPackageTokens = @(
    'package/SARA-1.0.15-windows-x64',
    'SARA-Setup-1.0.15.exe',
    'SARA-Setup-1.0.15-SHA256.txt',
    'name: SARA-1.0.15-windows-x64'
)
foreach ($token in $forbiddenCurrentPackageTokens) {
    if ($workflow.Contains($token)) {
        throw "Current release packaging still hard-codes the protected 1.0.15 baseline: $token"
    }
}

Write-Host "SARA canonical version contract: PASS ($version)"
