[CmdletBinding()]
param(
  [switch]$NoLaunch,
  [switch]$ForceRestart,
  [ValidateRange(10,600)][int]$StartupTimeoutSeconds=60
)
$ErrorActionPreference='Stop'
$ai=$PSScriptRoot
$app=Split-Path -Parent $ai
$model=Join-Path $ai 'models\Qwen3.5-9B-Q4_K_M.gguf'
$saraData=Join-Path $env:LOCALAPPDATA 'SARA'
$legacyData=Join-Path $env:LOCALAPPDATA 'Sentinel'
if(Test-Path $saraData){$data=$saraData}
elseif(Test-Path $legacyData){$data=$legacyData}
else{$data=$saraData}
$runtimeConfig=Join-Path $data 'active-runtime.ini'
$activeLora=''
$runtimeAlias='sentinel-chat'
if(Test-Path $runtimeConfig){
  foreach($line in Get-Content -LiteralPath $runtimeConfig){
    if($line.StartsWith('model=')){ $candidate=$line.Substring(6); if(Test-Path $candidate){$model=$candidate} }
    elseif($line.StartsWith('lora=')){ $candidate=$line.Substring(5); if(Test-Path $candidate){$activeLora=$candidate} }
    elseif($line.StartsWith('alias=')){ $candidate=$line.Substring(6); if($candidate){$runtimeAlias=$candidate} }
  }
}
$logs=Join-Path $ai 'logs'
$pidFile=Join-Path $ai 'llama-server.pid'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
& (Join-Path $ai 'Configure-Sentinel.ps1')

function Health {
  try {
    $code=& curl.exe --silent --noproxy "*" --connect-timeout 1 --max-time 2 -o NUL -w "%{http_code}" "http://127.0.0.1:1234/health"
    if($LASTEXITCODE -eq 0){return ($code|Select-Object -Last 1).Trim()}
  } catch {}
  return '000'
}
if(-not (Test-Path $model)){throw 'Local SARA model is not installed.'}
if((Health) -eq '200' -and -not $ForceRestart){
  if(-not $NoLaunch){Start-Process (Join-Path $app 'SARA.exe') -WorkingDirectory $app}
  exit 0
}
& (Join-Path $ai 'Stop-Sentinel-AI.ps1')
$gpu=Get-ChildItem (Join-Path $ai 'runtime') -Filter llama-server.exe -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
$cpu=Get-ChildItem (Join-Path $ai 'runtime_cpu') -Filter llama-server.exe -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
if(-not $gpu -and -not $cpu){throw 'SARA llama.cpp runtime is not installed.'}

function Start-One($server,[int]$layers,[string]$tag){
  $out=Join-Path $logs "llama-server-$tag.out.log"
  $err=Join-Path $logs "llama-server-$tag.err.log"
  $args=@('-m',$model,'--alias',$runtimeAlias,'--host','127.0.0.1','--port','1234','-c','4096','-ngl',"$layers",'-b','128','-ub','64','--reasoning','off','--jinja')
  if($activeLora){$args+=@('--lora',$activeLora)}
  $p=Start-Process -FilePath $server.FullName -ArgumentList $args -WorkingDirectory $server.DirectoryName -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
  Set-Content $pidFile $p.Id -Encoding ASCII
  $deadline=(Get-Date).AddSeconds($StartupTimeoutSeconds)
  while((Get-Date)-lt $deadline){
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if($p.HasExited){return $false}
    if((Health)-eq '200'){return $true}
  }
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  return $false
}
$ready=$false
if($gpu){$ready=Start-One $gpu 999 'gpu'}
if(-not $ready -and $cpu){$ready=Start-One $cpu 0 'cpu'}
if(-not $ready){throw 'SARA AI backend failed to start. See ai\logs.'}
if(-not $NoLaunch){Start-Process (Join-Path $app 'SARA.exe') -WorkingDirectory $app}
