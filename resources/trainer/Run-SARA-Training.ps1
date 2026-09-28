[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Database,
  [Parameter(Mandatory=$true)][string]$JobId
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$dataRoot=Split-Path -Parent $Database
$venvPython=Join-Path $dataRoot 'trainer-venv\Scripts\python.exe'
$python=$null
if(Test-Path $venvPython){
  $python=$venvPython
  Write-Host ('Using SARA trainer environment: ' + $venvPython)
}
if(-not $python){
  foreach($candidate in @('py.exe','python.exe','python3.exe')){
    $cmd=Get-Command $candidate -ErrorAction SilentlyContinue
    if($cmd){$python=$cmd.Source;break}
  }
}
if(-not $python){
  Write-Host ''
  Write-Host 'SARA Trainer requires Python 3.10+.' -ForegroundColor Yellow
  Write-Host 'Install Python, then use Prepare Env in Model Lab / Train.'
  Read-Host 'Press Enter to close'
  exit 2
}
Write-Host 'SARA Model Trainer' -ForegroundColor Cyan
Write-Host ('Job: ' + $JobId)
Write-Host ('Database: ' + $Database)
Write-Host ''
$appRoot=Split-Path -Parent $PSScriptRoot
& $python (Join-Path $root 'train_sara.py') --db $Database --job $JobId --app-root $appRoot
$code=$LASTEXITCODE
if($code -ne 0){
  Write-Host ''
  Write-Host ('Training job failed with exit code ' + $code) -ForegroundColor Red
  Read-Host 'Press Enter to close'
}
exit $code
