# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param([string]$ScriptPath)
if (-not $ScriptPath) { $ScriptPath=Join-Path $PSScriptRoot '../../scripts/testar-processamento-gpu.ps1' }
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
foreach($path in @($ScriptPath,(Join-Path $PSScriptRoot 'measurement-test.ps1'))) {
 $tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
 if($errors.Count){throw ($errors|Out-String)}
 foreach($definition in $ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$true)){Invoke-Expression $definition.Extent.Text}
}
$script:birth=[datetime]::UtcNow
if($env:OS -ne 'Windows_NT') {
 function Get-Item([string]$LiteralPath) {
  $item=Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath
  [pscustomobject]@{Length=$item.Length;CreationTimeUtc=$script:birth}
 }
}
$script:clock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=0.0}}
$script:processStart=[datetime]::Parse('2026-10-03T12:00:00Z').ToUniversalTime()
$script:wall=[datetime]::Parse('2026-10-03T12:00:00')
$script:path=Join-Path ([IO.Path]::GetTempPath()) ('gpu-boundary-'+[guid]::NewGuid().ToString('N')+'.txt')
function Line($Record) {
 $script:wall.AddSeconds($Record.EmittedAt).ToString('HH:mm:ss.fff',[cultureinfo]::InvariantCulture)+': [obs-backgroundremoval] GPUImageProcessing '+$Record.Kind+' '+(($Record.Fields.GetEnumerator()|ForEach-Object{"$($_.Key)=$($_.Value)"})-join ' ')
}
function Emit {
 foreach($event in $script:events) {
  if(-not $event.Sent -and $event.At -le $script:clock.Elapsed.TotalSeconds+0.000001) {
   [IO.File]::AppendAllText($script:path,$event.Text);$event.Sent=$true
  }
 }
}
function Start-Sleep([int]$Milliseconds) {$script:clock.Elapsed.TotalSeconds+=$Milliseconds/1000.0;Emit}
function Delay([string]$Stage) {
 if($script:delayStage -eq $Stage -and [Math]::Abs($script:clock.Elapsed.TotalSeconds-$script:delayAt) -lt 0.05) {
  $script:clock.Elapsed.TotalSeconds+=4;$script:delayStage='';Emit
 }
}
function Get-Process([int]$Id) {
 Delay 'GetProcess'
 $value=[pscustomobject]@{Id=$Id;StartTime=$script:processStart;HasExited=$false}
 $value|Add-Member -MemberType ScriptMethod -Name Refresh -Value {Delay 'Refresh'}
 $value|Add-Member -MemberType ScriptProperty -Name TotalProcessorTime -Value {Delay 'Counter';[TimeSpan]::FromSeconds($script:clock.Elapsed.TotalSeconds*0.48)}
 return $value
}
function Event([double]$At,[string]$Text) {[pscustomobject]@{At=$At;Text=$Text;Sent=$false}}
function Collect([string]$Case='Healthy',[string]$Stage='',[double]$Endpoint=20) {
 $script:clock.Elapsed.TotalSeconds=0;$script:delayStage=$Stage;$script:delayAt=$Endpoint
 [IO.File]::WriteAllText($script:path,'')
 $script:events=@()
 foreach($at in @(5,10,15,20,25)) {
  if($Case -eq 'MissingTail' -and $at -ge 20){continue}
  $emitted=[double]$at
  if($Case -eq 'OffsetCadence'){$emitted+=0.2}
  $r=Record $emitted $true
  $line=(Line $r)+[Environment]::NewLine
  if($at -eq 20 -and $Case -eq 'IncompleteTail'){$line=(Line $r);$script:events+=Event 20 $line;break}
  if($at -eq 20 -and $Case -eq 'CompletedTail') {
   $script:events+=Event 20 $line.Substring(0,100)
   $script:events+=Event 20.4 $line.Substring(100)
  } else {$script:events+=Event ($emitted+0.05) $line}
 }
 if($Case -eq 'LateFallback') {
  $r=Record 19.8 $true;$r.Kind='state';$r.Fields.state='CpuProcessingFallback';$r.Fields.reason='processing-failed'
  $script:events+=Event 20.2 ((Line $r)+[Environment]::NewLine)
 }
 if($Case -eq 'LateError'){$script:events+=Event 20.2 ('12:00:19.800: [obs-backgroundremoval] ERROR processing failed'+[Environment]::NewLine)}
 $reader=New-GpuLogReader $script:path $script:wall
 $ready=Record 0 $true;$reader.Records.Add($ready)
 Invoke-GpuBlock $reader $script:clock $ready $true 0 101 $script:processStart.Ticks 16
}
# Adapt only the return boundary of the real file read: bytes can arrive after its snapshot.
# Local function adapters do not alter the production parser or leak into the other cases.
function Collect-TerminalBoundary([string]$Scenario) {
 $actualReader=${function:Read-GpuFreshLog}
 $deadline=$Scenario -like 'Deadline*'
 $boundary=21.0;if($deadline){$boundary=26.2}
 $race=[pscustomobject]@{Injected=$false;LastReadStart=-1.0;Reader=$null}
 Set-Item function:local:Start-Sleep -Value {
  param([int]$Milliseconds)
  $script:clock.Elapsed.TotalSeconds=[Math]::Round(($script:clock.Elapsed.TotalSeconds+$Milliseconds/1000.0),2)
  Emit
 }
 Set-Item function:local:Read-GpuFreshLog -Value {
  param($Reader,[double]$Now)
  & $actualReader $Reader $Now
  $race.LastReadStart=$Now;$race.Reader=$Reader
  if(-not $race.Injected -and $Now -ge ($boundary-0.02) -and $Now -lt $boundary) {
   $script:clock.Elapsed.TotalSeconds=$boundary-0.01
   if($Scenario -eq 'MinimumError') {
    [IO.File]::AppendAllText($script:path,'12:00:20.000: [obs-backgroundremoval] ERROR terminal processing failed'+[Environment]::NewLine)
   } elseif($Scenario -eq 'DeadlineCompletedLine') {
    # Finish the real previously ingested partial line after this read's byte snapshot.
    [IO.File]::AppendAllText($script:path,[Environment]::NewLine)
   }
   $script:clock.Elapsed.TotalSeconds=$boundary+0.02
   if($Scenario -eq 'DeadlineOverrun'){$script:clock.Elapsed.TotalSeconds=$boundary+0.10}
   $race.Injected=$true
  }
 }
 $case='Healthy';if($deadline){$case='IncompleteTail'}
 $block=Collect $case
 [pscustomobject]@{Block=$block;Injected=$race.Injected;LastReadStart=$race.LastReadStart;Pending=$race.Reader.Pending;Boundary=$boundary}
}
$failures=@()
function Case([string]$Name,[scriptblock]$Action) {
 try{& $Action;Write-Output "$Name PASS"}catch{$script:failures+=$Name+': '+$_.Exception.Message;Write-Output "$Name FAIL: $($_.Exception.Message)"}
}
try {
 Case 'healthy actual collector' {$b=Collect;Check (Test-GpuProcessingBlock $b) 'Fresh closing stats rejected';Check ([Math]::Abs((Get-BlockCpuPercentage $b)-3) -lt 0.00001) 'Constant 3% differs';Check ([Math]::Abs($b.End-$b.MeasureStart-15) -lt 0.05) 'CPU interval changed'}
 Case 'missing final tail' {$b=Collect 'MissingTail';Check (-not (Test-GpuProcessingBlock $b)) 'Unobserved final five seconds accepted'}
 foreach($kind in @('LateFallback','LateError','IncompleteTail')) {Case $kind {$b=Collect $kind;Check (-not (Test-GpuProcessingBlock $b)) 'Pre-end failure/incomplete closing accepted'}}
 foreach($kind in @('OffsetCadence','CompletedTail')) {Case $kind {$b=Collect $kind;Check (Test-GpuProcessingBlock $b) 'Fresh actual closing record rejected';Check ($b.Records[-1].EmittedAt -ge $b.End) 'Closed without endpoint coverage';Check ([Math]::Abs($b.End-$b.MeasureStart-15) -lt 0.05) 'Observation changed CPU interval'}}
 foreach($stage in @('GetProcess','Refresh','Counter')) {foreach($endpoint in @(5,20)) {
  Case ("acquisition $stage at $endpoint") {$b=Collect 'Healthy' $stage $endpoint;Check (-not (Test-GpuProcessingBlock $b)) ("Delayed $stage accepted CPU="+(Get-BlockCpuPercentage $b));Check ($b.Samples[$endpoint].At -ge ($endpoint+4)) 'Sample labelled before actual CPU acquisition'}
 }}
 foreach($scenario in @('MinimumError','MinimumHealthy','DeadlineCompletedLine','DeadlineOverrun')) {
  Case ("terminal snapshot $scenario") {
   $r=Collect-TerminalBoundary $scenario;$b=$r.Block
   Check $r.Injected 'Terminal read did not cross its actual byte-snapshot boundary'
   Check ($b.MeasureStart -eq 5 -and $b.End -eq 20 -and $b.Samples.Count -eq 21) 'Terminal drain changed CPU endpoints or sample count'
   Check ($b.ObservationEnd -eq $script:clock.Elapsed.TotalSeconds) 'Observation end is not actual completion time'
   if($scenario -eq 'MinimumError') {
    Write-Output ("terminal error end=$($b.End); observationEnd=$($b.ObservationEnd); capturedErrors=$($b.Errors.Count); accepted="+(Test-GpuProcessingBlock $b))
    Check ($b.Errors.Count -eq 1 -and -not (Test-GpuProcessingBlock $b)) 'Permitted terminal error after byte snapshot was lost'
   } elseif($scenario -eq 'MinimumHealthy') {
    Check (Test-GpuProcessingBlock $b) 'Healthy closing statistics rejected after final barrier'
    Check ([Math]::Abs((Get-BlockCpuPercentage $b)-3) -lt 0.00001) 'Terminal read changed constant 3% CPU'
   } else {
    Check (-not (Test-GpuProcessingBlock $b)) 'Late/incomplete deadline telemetry or overlong observation accepted'
    if($scenario -eq 'DeadlineCompletedLine'){Check (-not $r.Pending) 'Timeout accepted a pre-deadline partial-byte snapshot'}
    else{Check ([bool]$r.Pending -and $b.ObservationEnd -gt ($b.End+6.25)) 'Overrun did not preserve incomplete pending bytes and actual latency'}
   }
   Check ($r.LastReadStart -ge $r.Boundary) 'Collector terminated on a read started before the required boundary'
  }
 }
 if($failures.Count){throw ($failures -join [Environment]::NewLine)}
 'collector end coverage and actual OS acquisition PASS'
} finally {Remove-Item -LiteralPath $script:path -Force}
