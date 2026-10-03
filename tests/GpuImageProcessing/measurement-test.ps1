# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param([string]$ScriptPath = (Join-Path $PSScriptRoot '../../scripts/testar-processamento-gpu.ps1'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $ScriptPath)) { throw 'Task6 expected RED: measurement script absent.' }
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($ScriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
$definitions = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] },$true))
foreach ($definition in $definitions) { Invoke-Expression $definition.Extent.Text }
function Check([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Fails([scriptblock]$Action,[string]$Message) {
    $failed = $false; try { & $Action | Out-Null } catch { $failed = $true }
    Check $failed $Message
}
Check ([Math]::Abs((Get-NormalizedCpuPercentage 1 10 16) - 0.625) -lt 0.0000001) 'CPU normalization must be 0.625%.'
foreach ($values in @(@(-1,10,16),@(1,0,16),@(1,10,0),@([double]::NaN,10,16),@(1,[double]::PositiveInfinity,16))) {
    Fails { Get-NormalizedCpuPercentage $values[0] $values[1] $values[2] } 'Invalid CPU operands must fail.'
}
function Record([double]$At,[bool]$On) {
    $requested = 0; $state = 'Off'; $pixels = 100*1280*720; $stage = 0
    if ($On) { $requested = 1; $state = 'Active'; $pixels = 100*256*144; $stage = 1 }
    [pscustomobject]@{ Kind='stats'; ReceivedAt=$At; EmittedAt=$At; Fields=@{
        processing_version='1'; filter_id='1'; settings_fingerprint='1111111111111111'; source_fingerprint='2222222222222222'
        generation='3'; frame_id='200'; requested="$requested"; state=$state; effective_inference='DmlExecutionProvider'
        preprocess_active="$stage"; mask_active="$stage"; similarity_full_readback='0'; source_width='1280'; source_height='720'
        input_width='256'; input_height='144'; reason='none'; fps_num='30'; fps_den='1'; enable_threshold='1'; threshold='0.5'
        temporal_smooth_factor='0.1'; contour_filter='0'; smooth_contour='0'; feather='0'; mask_expansion='0'; image_similarity='0'
        similarity_threshold='35'; mask_every_x_frames='1'; blur_background='0'; focal_blur='0'; focus_point='0.1'; focus_depth='0'
        processed='99'; skipped='1'; stale='0'; captured='100'; input_readback_pixels="$pixels"; similarity_readback_pixels='0'
        capture_host_elapsed_ms='100'; inference_host_elapsed_ms='50'; mask_host_elapsed_ms='40'; interval_host_elapsed_ms='5000'
        obs_counters_available='1'; obs_rendered_frames="$([int]($At*30))"; obs_lagged_frames='0'; obs_average_frame_host_ns='10000'
    } }
}
function Block([bool]$On,[double]$Start=0,[double]$Cpu=3) {
    $samples = @(); for ($i=0; $i -le 20; $i++) {
        $seconds = $i * $Cpu/100 * 16
        $samples += [pscustomobject]@{ At=($Start+$i); CpuSeconds=$seconds; Pid=101; StartTicks=98765 }
    }
    [pscustomobject]@{
        On=$On; Start=$Start; MeasureStart=($Start+5); End=($Start+20); LogicalProcessors=16; Samples=$samples
        Records=@((Record ($Start) $On),(Record ($Start+5) $On),(Record ($Start+10) $On),(Record ($Start+15) $On),(Record ($Start+20) $On))
        Errors=@(); FilterId='1'; Similarity=0; CpuPercentage=$Cpu; Valid=$true
    }
}
$off = Block $false
Check (Test-GpuProcessingBlock $off) 'Valid OFF block rejected.'
$processing=Get-GpuProcessingEvidence $off
Check ($processing.Count -eq 3 -and $processing.Captured -eq 300 -and $processing.Elapsed -eq 15) 'Interval counters were not aggregated within disclosed measured bounds.'
$lag=Get-GpuLagEvidence $off
Check ($lag.Status -eq 'Available' -and $lag.Elapsed -eq 15) 'Fresh OBS lag counter deltas missing.'
$off.Records[4].Fields.obs_lagged_frames='-1'
Check ((Get-GpuLagEvidence $off).Status -eq 'CounterReset') 'OBS counter reset interpreted as improvement.'
$off=Block $false
$off.Records[4].Fields.obs_counters_available='0'
Check ((Get-GpuLagEvidence $off).Status -eq 'Incomplete') 'Unavailable OBS counters fabricated.'
$off=Block $false
$on = Block $true
Check (Test-GpuProcessingBlock $on) 'Valid ON block rejected.'
# Every mutation represents a plausible invalid live comparison and exercises production validation.
foreach ($mutation in @(
    { param($b) $b.Samples[7].Pid=102 },
    { param($b) $b.Samples[2].Pid=102 },
    { param($b) $b.Samples[7].StartTicks=222 },
    { param($b) $b.Samples[10].CpuSeconds=-1 },
    { param($b) $b.MeasureStart=4 },
    { param($b) $b.End=19 },
    { param($b) $b.Samples[10].At+=3 },
    { param($b) $b.Records[2].Fields.filter_id='2' },
    { param($b) $b.Records[2].Fields.generation='4' },
    { param($b) $b.Records[2].Fields.settings_fingerprint='3333333333333333' },
    { param($b) $b.Records[2].Fields.source_fingerprint='3333333333333333' },
    { param($b) $b.Records[2].Fields.effective_inference='CPUExecutionProvider' },
    { param($b) $b.Records[2].Fields.state='CpuProcessingFallback' },
    { param($b) $b.Records[2].Fields.preprocess_active='0' },
    { param($b) $b.Records[2].Fields.input_readback_pixels='999' },
    { param($b) $b.Records[2].Fields.source_width='1' },
    { param($b) $b.Records[2].Fields.fps_den='0' },
    { param($b) $b.Records[2].Fields.settings_fingerprint='' },
    { param($b) $b.Records[2].Fields.Remove('captured') },
    { param($b) $b.Records[2].ReceivedAt+=3 },
    { param($b) $b.Records=@() },
    { param($b) $b.Errors=@('processing-failed') }
)) {
    $bad = Block $true
    & $mutation $bad
    Check (-not (Test-GpuProcessingBlock $bad)) 'Invalid live block accepted.'
}
$similar = Block $true
$similar.Similarity=1
foreach ($r in $similar.Records) { $r.Fields.image_similarity='1'; $r.Fields.similarity_full_readback='1'; $r.Fields.similarity_readback_pixels='92160000' }
Check (Test-GpuProcessingBlock $similar) 'Preserved similarity block rejected.'
$similar.Records[2].Fields.image_similarity='0'
Check (-not (Test-GpuProcessingBlock $similar)) 'Changed preserved similarity accepted.'
$settled = Block $false
for ($i=0; $i -lt 5; $i++) { $settled.Samples[$i].CpuSeconds=-100+$i }
Check (Test-GpuProcessingBlock $settled) 'Settling CPU must not enter measured CPU.'
Check ([Math]::Abs((Get-BlockCpuPercentage $settled)-3) -lt 0.0000001) 'Settling included in CPU result.'
$blocks = @((Block $false 0 4),(Block $true 30 2),(Block $true 60 2.5),(Block $false 90 4.5))
$comparison = Compare-GpuProcessingBlocks $blocks
Check ($comparison.Status -eq 'ObservedImprovement') 'Separated ranges must show observed improvement.'
Check ([Math]::Abs($comparison.PercentagePoints - 2) -lt 0.0000001) 'Percentage points wrong.'
Check ([Math]::Abs($comparison.RelativePercent - (100*2/4.25)) -lt 0.000001) 'Relative percent conflated with points.'
$mixed=@((Block $false 0 4),(Block $true 30 2),(Block $true 60 2.5),(Block $false 90 4.5))
for ($i=0;$i -lt $mixed.Count;$i++) {
    $other=Record $mixed[$i].Start $true;$other.Fields.filter_id='99';$other.Fields.settings_fingerprint=("{0:x16}" -f ($i+100))
    $mixed[$i].Records=@($other)+$mixed[$i].Records
}
Check ((Compare-GpuProcessingBlocks $mixed).Status -eq 'ObservedImprovement') 'Other active filter records contaminated chosen filter.'
$blocks[1]=Block $true 30 4.1
Check ((Compare-GpuProcessingBlocks $blocks).Status -eq 'Inconclusive') 'Overlapping ranges must be inconclusive.'
$blocks[1]=Block $true 10 2
Check ((Compare-GpuProcessingBlocks $blocks).Status -eq 'Inconclusive') 'Overlapping measured windows accepted.'
Check ($null -eq (ConvertFrom-GpuProcessingLine '   ' 1 1)) 'Whitespace fabricated a record.'
Check ($null -eq (ConvertFrom-GpuProcessingLine 'GPUImageProcessing stats processing_version=1' 1 1)) 'Incomplete log fabricated readiness.'
$r=Record 1 $true
$line='GPUImageProcessing stats ' + (($r.Fields.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' ')
Check ($null -ne (ConvertFrom-GpuProcessingLine $line 1 1)) 'Complete log did not parse.'
Check ($null -eq (ConvertFrom-GpuProcessingLine ($line+' captured=999') 1 1)) 'Duplicate keys accepted.'
Check ($null -eq (ConvertFrom-GpuProcessingLine ($line.Replace('captured=100','captured=NaN')) 1 1)) 'Malformed counters accepted.'
$root=Join-Path ([IO.Path]::GetTempPath()) ('gpu-installed-'+[guid]::NewGuid().ToString('N'))
$savedProgramData=$env:ProgramData;$env:ProgramData=$root
try {
    $installed=Join-Path $root 'obs-studio/plugins/obs-backgroundremoval'
    New-Item -Path (Join-Path $installed 'bin/64bit') -ItemType Directory -Force|Out-Null
    $dll=Join-Path $installed 'bin/64bit/obs-backgroundremoval.dll';'fixture'|Set-Content -LiteralPath $dll -Encoding UTF8
    $manifest=[pscustomobject]@{SourceSha=('a'*40);ZipSha256=('b'*64);MeasurementSha256=(Get-FileHash -LiteralPath $ScriptPath).Hash
        Files=@([pscustomobject]@{Path='bin/64bit/obs-backgroundremoval.dll';Sha256=(Get-FileHash -LiteralPath $dll).Hash;Length=(Get-Item -LiteralPath $dll).Length})}
    $receipt=Join-Path $root 'receipt.json'
    @{State='installed';SourceSha=$manifest.SourceSha;ZipSha256=$manifest.ZipSha256;MeasurementSha256=$manifest.MeasurementSha256;Installed=$installed}|
        ConvertTo-Json|Set-Content -LiteralPath $receipt -Encoding UTF8
    $null=Assert-GpuInstalled $ScriptPath $receipt $manifest
    'unexpected runtime'|Set-Content -LiteralPath (Join-Path $installed 'bin/64bit/extra.dll') -Encoding UTF8
    Fails {Assert-GpuInstalled $ScriptPath $receipt $manifest} 'Unexpected installed file accepted.'
} finally {$env:ProgramData=$savedProgramData;Remove-Item -LiteralPath $root -Recurse -Force}
'measurement helpers PASS'
