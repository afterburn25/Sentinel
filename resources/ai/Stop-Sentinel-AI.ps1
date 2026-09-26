$pidFile=Join-Path $PSScriptRoot 'llama-server.pid'
if(Test-Path $pidFile){
  $pidValue=(Get-Content $pidFile -ErrorAction SilentlyContinue | Select-Object -First 1)
  if($pidValue -match '^\d+$'){Stop-Process -Id ([int]$pidValue) -Force -ErrorAction SilentlyContinue}
  Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
}
Get-Process llama-server -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
