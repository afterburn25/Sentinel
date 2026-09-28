[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$DataRoot
)

$ErrorActionPreference='Stop'
$venv=Join-Path $DataRoot 'trainer-venv'
$marker=Join-Path $DataRoot 'trainer-env.version'
$requirements=Join-Path $PSScriptRoot 'requirements.txt'

function Find-Python {
  foreach($candidate in @('py.exe','python.exe','python3.exe')) {
    $cmd=Get-Command $candidate -ErrorAction SilentlyContinue
    if($cmd) { return $cmd.Source }
  }
  return $null
}

Write-Host 'SARA Trainer Environment Setup' -ForegroundColor Cyan
Write-Host ('Data root: ' + $DataRoot)
Write-Host ''

$python=Find-Python
if(-not $python) {
  throw 'Python 3.10+ is required. Install Python, then run Prepare Env again.'
}

New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null

if(-not (Test-Path (Join-Path $venv 'Scripts\python.exe'))) {
  Write-Host 'Creating isolated SARA trainer environment...'
  & $python -m venv $venv
  if($LASTEXITCODE -ne 0) { throw 'Unable to create the SARA trainer virtual environment.' }
}

$venvPython=Join-Path $venv 'Scripts\python.exe'
Write-Host 'Updating pip tooling...'
& $venvPython -m pip install --upgrade pip setuptools wheel
if($LASTEXITCODE -ne 0) { throw 'Unable to update trainer pip tooling.' }

Write-Host 'Installing SARA training packages. This may take several minutes...'
& $venvPython -m pip install -r $requirements
if($LASTEXITCODE -ne 0) { throw 'Unable to install one or more SARA training packages.' }

Write-Host 'Validating trainer imports...'
& $venvPython -c "import torch, transformers, datasets, peft, accelerate, safetensors; import bitsandbytes; print('torch',torch.__version__); print('cuda',torch.cuda.is_available()); print('transformers',transformers.__version__)"
if($LASTEXITCODE -ne 0) { throw 'Trainer package validation failed.' }

@(
  'sara-trainer-env-v1'
  ('python=' + $venvPython)
  ('configured_utc=' + [DateTime]::UtcNow.ToString('o'))
) | Set-Content -Encoding ascii $marker

Write-Host ''
Write-Host 'SARA Trainer environment is ready.' -ForegroundColor Green
Write-Host ('Environment: ' + $venv)
Read-Host 'Press Enter to close'
