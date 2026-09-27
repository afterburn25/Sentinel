[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Database,
  [Parameter(Mandatory=$true)][string]$JobId
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$python=$null
foreach($candidate in @('py.exe','python.exe','python3.exe')){
  $cmd=Get-Command $candidate -ErrorAction SilentlyContinue
  if($cmd){$python=$cmd.Source;break}
}
if(-not $python){
  Write-Host ''
  Write-Host 'SARA Trainer requires Python 3.10+ for weight training.' -ForegroundColor Yellow
  Write-Host 'Install Python, then rerun this job from SARA Trainer.'
  Read-Host 'Press Enter to close'
  exit 2
}
Write-Host 'SARA Model Trainer' -ForegroundColor Cyan
Write-Host ('Job: ' + $JobId)
Write-Host ('Database: ' + $Database)
Write-Host ''
& $python (Join-Path $root 'train_sara.py') --db $Database --job $JobId
$code=$LASTEXITCODE
if($code -ne 0){
  Write-Host ''
  Write-Host ('Training job failed with exit code ' + $code) -ForegroundColor Red
  Read-Host 'Press Enter to close'
}
exit $code
