[CmdletBinding()]
param([switch]$Force)
$ErrorActionPreference='Stop'
$ai=$PSScriptRoot
$models=Join-Path $ai 'models'
$runtime=Join-Path $ai 'runtime'
$cpu=Join-Path $ai 'runtime_cpu'
$temp=Join-Path $env:TEMP 'sentinel-ai-repair'
$build='b10977'
$model=Join-Path $models 'Qwen3.5-9B-Q4_K_M.gguf'
$modelUrl='https://huggingface.co/llmware/qwen3.5-9b-gguf/resolve/main/Qwen3.5-9B-Q4_K_M.gguf?download=true'
$modelSha='03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8'
New-Item -ItemType Directory -Force -Path $models,$runtime,$cpu,$temp | Out-Null
& (Join-Path $ai 'Stop-Sentinel-AI.ps1')

function Get-Asset {
  param([string]$url,[string]$dest)
  if((Test-Path $dest)-and -not $Force){return}
  & curl.exe -L --fail --retry 5 --retry-all-errors --retry-delay 3 -C - -o $dest $url
  if($LASTEXITCODE -ne 0){throw "Download failed: $url"}
}
$gpu=''
$nvsmi=Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
if($nvsmi){try{$gpu=(& $nvsmi.Source --query-gpu=name --format=csv,noheader|Select-Object -First 1).Trim()}catch{}}
if($gpu -match 'RTX\s*50\d\d'){
  $main="llama-$build-bin-win-cuda-13.4-x64.zip";$cuda="cudart-llama-bin-win-cuda-13.4-x64.zip"
}elseif($gpu){
  $main="llama-$build-bin-win-cuda-12.4-x64.zip";$cuda="cudart-llama-bin-win-cuda-12.4-x64.zip"
}else{
  $main="llama-$build-bin-win-cpu-x64.zip";$cuda=$null
}
$base="https://github.com/ggml-org/llama.cpp/releases/download/$build"
$mainZip=Join-Path $temp $main
Get-Asset "$base/$main" $mainZip
Remove-Item $runtime -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $runtime|Out-Null
Expand-Archive $mainZip $runtime -Force
if($cuda){
  $cudaZip=Join-Path $temp $cuda
  Get-Asset "$base/$cuda" $cudaZip
  Expand-Archive $cudaZip $runtime -Force
}
$cpuAsset="llama-$build-bin-win-cpu-x64.zip"
$cpuZip=Join-Path $temp $cpuAsset
Get-Asset "$base/$cpuAsset" $cpuZip
Remove-Item $cpu -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $cpu|Out-Null
Expand-Archive $cpuZip $cpu -Force
$need=$true
if(Test-Path $model){
  $hash=(Get-FileHash $model -Algorithm SHA256).Hash.ToLowerInvariant()
  if($hash -eq $modelSha){$need=$false}else{Remove-Item $model -Force}
}
if($need){Get-Asset $modelUrl $model}
$hash=(Get-FileHash $model -Algorithm SHA256).Hash.ToLowerInvariant()
if($hash -ne $modelSha){throw 'Model checksum verification failed.'}
Set-Content (Join-Path $ai 'runtime.version') $build -Encoding ASCII
