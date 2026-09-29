[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$sara=Join-Path $env:LOCALAPPDATA 'SARA'
$legacy=Join-Path $env:LOCALAPPDATA 'Sentinel'
if(Test-Path $sara){$data=$sara}
elseif(Test-Path $legacy){$data=$legacy}
else{$data=$sara}
$ini=Join-Path $data 'simulation.ini'
New-Item -ItemType Directory -Force -Path $data | Out-Null
$lines=if(Test-Path $ini){@(Get-Content -LiteralPath $ini)}else{@()}
function Set-Key([string[]]$src,[string]$key,[string]$value){
  $prefix="$key=";$found=$false;$out=@()
  foreach($line in $src){
    if($line.StartsWith($prefix,[System.StringComparison]::Ordinal)){
      if(-not $found){$out+="$key=$value";$found=$true}
    }else{$out+=$line}
  }
  if(-not $found){$out+="$key=$value"}
  return ,$out
}
function Ensure-Key([string[]]$src,[string]$key,[string]$value){
  $prefix="$key="
  foreach($line in $src){
    if($line.StartsWith($prefix,[System.StringComparison]::Ordinal)){return ,$src}
  }
  return ,($src+"$key=$value")
}
$lines=Set-Key $lines 'endpoint' 'http://127.0.0.1:1234/v1/chat/completions'
$lines=Set-Key $lines 'model' 'sentinel-chat'
$lines=Ensure-Key $lines 'temperature' '0.75'
$lines=Ensure-Key $lines 'maxTokens' '220'
Set-Content -LiteralPath $ini -Value $lines -Encoding UTF8
