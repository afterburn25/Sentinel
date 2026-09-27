$ErrorActionPreference = 'Stop'

function Require-Contains {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Needle
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required baseline file missing: $Path"
    }

    $content = Get-Content -LiteralPath $Path -Raw
    if (-not $content.Contains($Needle)) {
        throw "Baseline regression in $Path. Missing required text: $Needle"
    }
}

$installer = 'installer/Sentinel.iss'
$gui = 'src/Gui/Main.cpp'
$workflow = '.github/workflows/windows-build.yml'
$splash = 'resources/assets/SARA-Splash.png'

# 1.0.15 local-model installer contract.
Require-Contains $installer "ModelFileName = 'Qwen3.5-9B-Q4_K_M.gguf'"
Require-Contains $installer "ModelSHA256 = '03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8'"
Require-Contains $installer 'function FileSHA256Matches'
Require-Contains $installer 'function ModelMarkerValid'
Require-Contains $installer 'procedure SaveModelMarker'
Require-Contains $installer 'function VerifyInstalledModel'
Require-Contains $installer 'Verifying the existing 5.68 GB SARA model'
Require-Contains $installer 'Downloaded SARA model failed SHA-256 verification.'
Require-Contains $installer 'function RuntimeValid'
Require-Contains $installer 'procedure InstallRuntime'
Require-Contains $installer 'procedure InstallModel'
Require-Contains $installer 'procedure ConfigureAI'
Require-Contains $installer 'ModelAlreadyValid := VerifyInstalledModel;'
Require-Contains $installer 'InstallRuntime;'
Require-Contains $installer 'InstallModel;'
Require-Contains $installer 'ConfigureAI;'

# 1.0.15 splash/startup contract.
Require-Contains $gui 'ExeDir()/L"assets"/L"SARA-Splash.png"'
Require-Contains $gui 'elapsed>=7000ULL'
Require-Contains $gui 'if(splashCtx.readyEvent) SetEvent(splashCtx.readyEvent);'
Require-Contains $gui 'if(splashThread) WaitForSingleObject(splashThread,INFINITE);'
Require-Contains $gui 'ShowWindow(hwnd,show);'


# Startup must not perform slow AI repair/service/network work while the splash is waiting.
$guiText = Get-Content -LiteralPath $gui -Raw
$autoStart = $guiText.IndexOf('    void AutoInitializeLocalAi() {')
$repairStart = $guiText.IndexOf('    void InstallOrRepairLocalAi()', $autoStart)
if ($autoStart -lt 0 -or $repairStart -lt 0) {
    throw 'Unable to locate AutoInitializeLocalAi recovery boundary.'
}
$autoBlock = $guiText.Substring($autoStart, $repairStart - $autoStart)
foreach ($forbidden in @('RunBundledAiSetup(', 'StartBundledAiService(', 'DiscoverOpenAICompatibleModels(')) {
    if ($autoBlock.Contains($forbidden)) {
        throw "Splash-hang regression: AutoInitializeLocalAi contains blocking startup work: $forbidden"
    }
}

# CI must still verify the exact approved splash.
Require-Contains $workflow "assert im.size==(1672,941)"
Require-Contains $workflow "dc298f498c76975af80aa14fd1e9125ae64746c54181c340f138869a24548cb4"

if (-not (Test-Path -LiteralPath $splash)) {
    throw "Approved SARA splash PNG is missing."
}

$hash = (Get-FileHash -LiteralPath $splash -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne 'dc298f498c76975af80aa14fd1e9125ae64746c54181c340f138869a24548cb4') {
    throw "Approved SARA splash hash changed: $hash"
}

Write-Host 'SARA 1.0.15 recovery baseline guard: PASS'
