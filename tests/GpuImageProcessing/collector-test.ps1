# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param([string]$ScriptPath=(Join-Path $PSScriptRoot '../../scripts/testar-processamento-gpu.ps1'))
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
foreach ($path in @($ScriptPath,(Join-Path $PSScriptRoot 'measurement-test.ps1'))) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    if ($errors.Count) {throw ($errors | Out-String)}
    foreach ($definition in $ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$true)) { Invoke-Expression $definition.Extent.Text }
}
# Real ingestion: append complete/partial/stale records, then truncate the same filesystem log.
$temp=Join-Path ([IO.Path]::GetTempPath()) ('gpu-collector-'+[guid]::NewGuid().ToString('N')+'.txt')
# Linux supplemental FileInfo.CreationTimeUtc maps to ctime and changes on append.
# Model Windows birth time only in this fixture; native PS5.1 uses actual FileInfo.
if ($env:OS -ne 'Windows_NT') {
    $script:birth=[datetime]::UtcNow
    function Get-Item([string]$LiteralPath) {
        $item=Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath
        [pscustomobject]@{Length=$item.Length;CreationTimeUtc=$script:birth}
    }
}
function Line($Record,[string]$Time) {
    $Time+': [obs-backgroundremoval] GPUImageProcessing '+$Record.Kind+' '+(($Record.Fields.GetEnumerator()|ForEach-Object{"$($_.Key)=$($_.Value)"})-join ' ')
}
try {
    [IO.File]::WriteAllText($temp,'old unrelated log')
    $wall=[datetime]::Parse('2026-10-03T23:59:59')
    $reader=New-GpuLogReader $temp $wall
    $line=Line (Record 1 $true) '00:00:00.000'
    [IO.File]::AppendAllText($temp,$line.Substring(0,100))
    Read-GpuFreshLog $reader 1
    Check ($reader.Records.Count -eq 0) 'Partial line fabricated readiness.'
    [IO.File]::AppendAllText($temp,$line.Substring(100)+[Environment]::NewLine)
    Read-GpuFreshLog $reader 1.1
    Check ($reader.Records.Count -eq 1) 'Completed log line not ingested.'
    Check ([Math]::Abs($reader.Records[0].EmittedAt-1) -lt 0.001) 'Midnight emission mapped to wrong date.'
    [IO.File]::AppendAllText($temp,(Line (Record 0 $true) '23:59:58.000')+[Environment]::NewLine)
    Read-GpuFreshLog $reader 2
    Check ($reader.Records[-1].EmittedAt -lt 0) 'Old line was assigned fresh emission.'
    [IO.File]::WriteAllText($temp,'truncated')
    Fails { Read-GpuFreshLog $reader 3 } 'Log truncation was accepted.'
    $reader=New-GpuLogReader $temp $wall
    $reader.CreationTicks-=1
    Fails {Read-GpuFreshLog $reader 4} 'Log replacement identity was ignored.'
} finally {Remove-Item -LiteralPath $temp -Force}
# Use the production collector and CPU-process reader. Only live OS sources/clock/log are adapted.
$script:realRead=${function:Read-GpuFreshLog}
$script:clock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=0.001}}
$script:restart=$false; $script:fallback=$false; $script:late=$false; $script:stall=$false
$script:processStart=[datetime]::Parse('2026-10-03T12:00:00Z').ToUniversalTime()
function Start-Sleep([int]$Milliseconds) {
    $script:clock.Elapsed.TotalSeconds+=$Milliseconds/1000.0
    if($script:stall -and $script:clock.Elapsed.TotalSeconds -gt 6){$script:clock.Elapsed.TotalSeconds+=3;$script:stall=$false}
}
function Get-Process([int]$Id) {
    $start=$script:processStart
    if ($script:restart -and $script:clock.Elapsed.TotalSeconds -gt 9) {$start=$start.AddSeconds(1)}
    $value=[pscustomobject]@{Id=$Id;StartTime=$start;HasExited=$false;TotalProcessorTime=[TimeSpan]::FromSeconds($script:clock.Elapsed.TotalSeconds*0.48)}
    $value|Add-Member -MemberType ScriptMethod -Name Refresh -Value {}
    return $value
}
function Read-GpuFreshLog($Reader,[double]$Now) {
    $next=5*($Reader.Records.Count)
    if ($Now -ge $next -and $next -le 20) {
        $record=Record $next $true; $record.ReceivedAt=$Now
        if ($script:late) {$record.EmittedAt=$Now-3}
        if ($script:fallback -and $Now -ge 10) {$record.Fields.state='CpuProcessingFallback';$record.Fields.reason='processing-failed'}
        $Reader.Records.Add($record)
    }
}
function FakeReader {
    [pscustomobject]@{Records=(New-Object 'System.Collections.Generic.List[object]');Errors=(New-Object 'System.Collections.Generic.List[object]')}
}
$reader=FakeReader; $ready=Record 0 $true; $reader.Records.Add($ready)
$block=Invoke-GpuBlock $reader $script:clock $ready $true 0 101 $script:processStart.Ticks 16
Check (Test-GpuProcessingBlock $block) 'Actual collector with fresh readiness, scheduled endpoints and CPU samples rejected.'
Check ([Math]::Abs((Get-BlockCpuPercentage $block)-3) -lt 0.0001) 'Collector CPU includes wrong endpoints.'
Check ([Math]::Abs(($block.End-$block.MeasureStart)-15) -lt 0.05) 'Collector measured duration wrong.'
$script:clock.Elapsed.TotalSeconds=0.001; $script:restart=$true; $reader=FakeReader;$reader.Records.Add($ready)
Fails { Invoke-GpuBlock $reader $script:clock $ready $true 0 101 $script:processStart.Ticks 16 } 'Production process reader accepted same-PID restart.'
$script:restart=$false;$script:fallback=$true;$script:clock.Elapsed.TotalSeconds=0.001;$reader=FakeReader;$reader.Records.Add($ready)
$block=Invoke-GpuBlock $reader $script:clock $ready $true 0 101 $script:processStart.Ticks 16
Check (-not (Test-GpuProcessingBlock $block)) 'Production collector accepted processing fallback.'
$script:fallback=$false;$script:late=$true;$script:clock.Elapsed.TotalSeconds=0.001;$reader=FakeReader;$reader.Records.Add($ready)
$block=Invoke-GpuBlock $reader $script:clock $ready $true 0 101 $script:processStart.Ticks 16
Check (-not (Test-GpuProcessingBlock $block)) 'Production collector accepted late log lines.'
$script:late=$false;$script:stall=$true;$script:clock.Elapsed.TotalSeconds=0.001;$reader=FakeReader;$reader.Records.Add($ready)
$block=Invoke-GpuBlock $reader $script:clock $ready $true 0 101 $script:processStart.Ticks 16
Check (-not (Test-GpuProcessingBlock $block)) 'Production collector fabricated once-per-second CPU after scheduler stall.'
'collector real ingestion and synthetic timing/process/status PASS'
