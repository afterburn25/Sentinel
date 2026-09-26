[CmdletBinding()]
param()
$ai=$PSScriptRoot
$model=Join-Path $ai 'models\Qwen3.5-9B-Q4_K_M.gguf'
$expected='03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8'
Write-Host '=== Sentinel AI diagnostics ==='
Write-Host ('Model: '+$(if(Test-Path $model){'FOUND'}else{'MISSING'}))
if(Test-Path $model){
  $hash=(Get-FileHash $model -Algorithm SHA256).Hash.ToLowerInvariant()
  Write-Host ('Model SHA-256: '+$(if($hash -eq $expected){'PASS'}else{'FAIL'}))
}
$servers=@(Get-ChildItem (Join-Path $ai 'runtime'),(Join-Path $ai 'runtime_cpu') -Filter llama-server.exe -Recurse -File -ErrorAction SilentlyContinue)
Write-Host ('Runtime binaries: '+$servers.Count)
try{$r=Invoke-RestMethod 'http://127.0.0.1:1234/v1/models' -TimeoutSec 5 -Proxy $null;Write-Host 'AI endpoint: READY'}catch{Write-Host 'AI endpoint: OFFLINE'}
Read-Host 'Press Enter to close'
