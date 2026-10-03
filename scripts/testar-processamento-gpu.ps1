# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
# Windows PowerShell 5.1. Complete logs and process samples remain local.
[CmdletBinding()]
param([switch]$ManterSimilaridade)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-NormalizedCpuPercentage([double]$CpuDeltaSeconds,[double]$ElapsedSeconds,[int]$LogicalProcessors) {
    if ([double]::IsNaN($CpuDeltaSeconds) -or [double]::IsInfinity($CpuDeltaSeconds) -or $CpuDeltaSeconds -lt 0 -or
        [double]::IsNaN($ElapsedSeconds) -or [double]::IsInfinity($ElapsedSeconds) -or $ElapsedSeconds -le 0 -or $LogicalProcessors -le 0) {
        throw 'CPU delta/elapsed/logical processor count is invalid.'
    }
    return 100.0*$CpuDeltaSeconds/($ElapsedSeconds*$LogicalProcessors)
}
function Get-GpuIntegerFields {
    @('processing_version','filter_id','generation','frame_id','requested','preprocess_active','mask_active','similarity_full_readback',
      'source_width','source_height','input_width','input_height','fps_num','fps_den','enable_threshold','mask_expansion','image_similarity',
      'mask_every_x_frames','blur_background','focal_blur','processed','skipped','stale','captured','input_readback_pixels',
      'similarity_readback_pixels','obs_counters_available','obs_rendered_frames','obs_lagged_frames','obs_average_frame_host_ns')
}
function Get-GpuRealFields {
    @('threshold','temporal_smooth_factor','contour_filter','smooth_contour','feather','similarity_threshold','focus_point','focus_depth',
      'capture_host_elapsed_ms','inference_host_elapsed_ms','mask_host_elapsed_ms','interval_host_elapsed_ms')
}
function Test-GpuFields($Fields) {
    try {
        foreach ($key in (Get-GpuIntegerFields)) {
            if (-not $Fields.ContainsKey($key) -or $Fields[$key] -cnotmatch '^-?[0-9]+$') { return $false }
            $number = [double]::Parse($Fields[$key],[cultureinfo]::InvariantCulture)
            if ([double]::IsInfinity($number) -or ($key -notin @('mask_expansion','blur_background') -and $number -lt 0)) { return $false }
        }
        foreach ($key in (Get-GpuRealFields)) {
            if (-not $Fields.ContainsKey($key)) { return $false }
            $number = [double]::Parse($Fields[$key],[cultureinfo]::InvariantCulture)
            if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) { return $false }
        }
        foreach ($key in @('settings_fingerprint','source_fingerprint')) {
            if (-not $Fields.ContainsKey($key) -or $Fields[$key] -cnotmatch '^[0-9a-f]{16}$') { return $false }
        }
        foreach ($key in @('state','effective_inference','reason')) { if (-not $Fields.ContainsKey($key) -or -not $Fields[$key]) { return $false } }
        if ($Fields.processing_version -cne '1') { return $false }
        foreach ($key in @('filter_id','generation','source_width','source_height','input_width','input_height','fps_num','fps_den','mask_every_x_frames')) {
            if ([double]$Fields[$key] -le 0) { return $false }
        }
        foreach ($key in @('requested','preprocess_active','mask_active','similarity_full_readback','enable_threshold','image_similarity','focal_blur','obs_counters_available')) {
            if ($Fields[$key] -notin @('0','1')) { return $false }
        }
        return $true
    } catch { return $false }
}
function ConvertFrom-GpuProcessingLine([string]$Line,[double]$ReceivedAt,[double]$EmittedAt) {
    if ($Line -notmatch 'GPUImageProcessing (stats|state) (.+)$') { return $null }
    $kind=$Matches[1]; $body=$Matches[2]; $fields=@{}
    foreach ($token in ($body -split '\s+')) {
        if ($token -cnotmatch '^([a-z_]+)=([^\s=]+)$') { return $null }
        if ($fields.ContainsKey($Matches[1])) { return $null }
        $fields[$Matches[1]]=$Matches[2]
    }
    if (-not (Test-GpuFields $fields)) { return $null }
    [pscustomobject]@{ Kind=$kind; ReceivedAt=$ReceivedAt; EmittedAt=$EmittedAt; Fields=$fields }
}
function Get-BlockCpuPercentage($Block) {
    $measured=@($Block.Samples | Where-Object { $_.At -ge $Block.MeasureStart -and $_.At -le $Block.End })
    if ($measured.Count -lt 2) { throw 'CPU endpoints missing.' }
    Get-NormalizedCpuPercentage ($measured[-1].CpuSeconds-$measured[0].CpuSeconds) ($measured[-1].At-$measured[0].At) $Block.LogicalProcessors
}
function Test-GpuProcessingBlock([pscustomobject]$Block) {
    try {
        if ($Block.Errors.Count -or $Block.LogicalProcessors -le 0 -or
            [Math]::Abs(($Block.MeasureStart-$Block.Start)-5) -gt 0.05 -or
            [Math]::Abs(($Block.End-$Block.MeasureStart)-15) -gt 0.05) { return $false }
        if ($Block.Samples.Count -ne 21) { return $false }
        for ($i=0; $i -lt $Block.Samples.Count; $i++) {
            $sample=$Block.Samples[$i]
            if ($sample.Pid -ne $Block.Samples[0].Pid -or $sample.StartTicks -ne $Block.Samples[0].StartTicks) { return $false }
            foreach ($value in @($sample.At,$sample.QueryStart,$sample.QueryEnd,$sample.AcquisitionSeconds)) {
                if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { return $false }
            }
            if ($sample.QueryStart -lt ($Block.Start+$i-0.001) -or $sample.QueryEnd -lt $sample.QueryStart -or
                $sample.At -ne $sample.QueryEnd -or $sample.AcquisitionSeconds -lt 0 -or $sample.AcquisitionSeconds -gt 0.05 -or
                [Math]::Abs($sample.AcquisitionSeconds-($sample.QueryEnd-$sample.QueryStart)) -gt 0.000001 -or
                [Math]::Abs($sample.At-($Block.Start+$i)) -gt 0.05) { return $false }
        }
        $samples=@($Block.Samples | Where-Object { $_.At -ge $Block.MeasureStart -and $_.At -le $Block.End })
        if ($samples.Count -ne 16 -or [Math]::Abs($samples[0].At-$Block.MeasureStart) -gt 0.05 -or
            [Math]::Abs($samples[-1].At-$Block.End) -gt 0.05) { return $false }
        for ($i=0; $i -lt $samples.Count; $i++) {
            if ($samples[$i].Pid -ne $samples[0].Pid -or $samples[$i].StartTicks -ne $samples[0].StartTicks) { return $false }
            if ($i -and ($samples[$i].CpuSeconds -lt $samples[$i-1].CpuSeconds -or
                [Math]::Abs(($samples[$i].At-$samples[$i-1].At)-1) -gt 0.15)) { return $false }
        }
        $cpu=Get-BlockCpuPercentage $Block
        if ($cpu -gt 100.05) { return $false }
        # CPU stops at End. A separate bounded observation closes telemetry and drains allowed late lines.
        if ($Block.ObservationEnd -lt ($Block.End+1) -or $Block.ObservationEnd -gt ($Block.End+6.25)) { return $false }
        $records=@($Block.Records | Where-Object {
            $_.ReceivedAt -ge $Block.Start -and $_.ReceivedAt -le $Block.ObservationEnd -and $_.Fields.filter_id -ceq $Block.FilterId
        })
        $stats=@($records | Where-Object { $_.Kind -eq 'stats' })
        $closing=@($stats | Where-Object { $_.EmittedAt -ge $Block.End -and
            $_.EmittedAt-[double]$_.Fields.interval_host_elapsed_ms/1000 -le $Block.End })
        if ($stats.Count -lt 4 -or $stats[0].ReceivedAt -gt ($Block.Start+1) -or -not $closing.Count) { return $false }
        $first=$stats[0].Fields
        foreach ($record in $records) {
            $f=$record.Fields
            if (-not (Test-GpuFields $f) -or $record.EmittedAt -lt ($Block.Start-1) -or
                $record.ReceivedAt-$record.EmittedAt -gt 1 -or $record.EmittedAt-$record.ReceivedAt -gt 0.25) { return $false }
            foreach ($key in @('filter_id','generation','settings_fingerprint','source_fingerprint','source_width','source_height',
                               'input_width','input_height','fps_num','fps_den')) { if ($f[$key] -cne $first[$key]) { return $false } }
            if ($f.effective_inference -cne 'DmlExecutionProvider' -or $f.reason -cne 'none' -or [int]$f.image_similarity -ne $Block.Similarity) { return $false }
            if ($Block.On) {
                if ($f.requested -cne '1' -or $f.state -cne 'Active' -or $f.preprocess_active -cne '1' -or
                    $f.mask_active -cne '1' -or [int]$f.similarity_full_readback -ne $Block.Similarity) { return $false }
            } elseif ($f.requested -cne '0' -or $f.state -cne 'Off' -or $f.preprocess_active -cne '0' -or
                $f.mask_active -cne '0' -or $f.similarity_full_readback -cne '0') { return $false }
            if ($record.Kind -eq 'stats') {
                if ([double]$f.captured -le 0 -or [double]$f.processed -le 0 -or [double]$f.interval_host_elapsed_ms -lt 4999) { return $false }
                $area=[double]$f.source_width*[double]$f.source_height
                $inputArea=$area; if ($Block.On) { $inputArea=[double]$f.input_width*[double]$f.input_height }
                if ([double]$f.input_readback_pixels -ne ([double]$f.captured*$inputArea)) { return $false }
                $similarityPixels=0; if ($Block.On -and $Block.Similarity) { $similarityPixels=[double]$f.captured*$area }
                if ([double]$f.similarity_readback_pixels -ne $similarityPixels) { return $false }
            }
        }
        for ($i=1; $i -lt $stats.Count; $i++) {
            if ($stats[$i].ReceivedAt-$stats[$i-1].ReceivedAt -gt 6.2) { return $false }
        }
        return $true
    } catch { return $false }
}
function Get-GpuLagEvidence($Block) {
    $stats=@($Block.Records | Where-Object { $_.Kind -eq 'stats' -and $_.Fields.filter_id -ceq $Block.FilterId })
    $before=@($stats | Where-Object { $_.ReceivedAt -le $Block.MeasureStart })
    $after=@($stats | Where-Object { $_.ReceivedAt -le $Block.End })
    if (-not $before.Count -or -not $after.Count) { return [pscustomobject]@{ Status='Incomplete'; DroppedFramesStatus='Unavailable' } }
    $a=$before[-1]; $b=$after[-1]
    if ($a.Fields.obs_counters_available -ne '1' -or $b.Fields.obs_counters_available -ne '1' -or $b.EmittedAt -le $a.EmittedAt) {
        return [pscustomobject]@{ Status='Incomplete'; DroppedFramesStatus='Unavailable' }
    }
    $frames=[double]$b.Fields.obs_rendered_frames-[double]$a.Fields.obs_rendered_frames
    $lag=[double]$b.Fields.obs_lagged_frames-[double]$a.Fields.obs_lagged_frames
    if ($frames -le 0 -or $lag -lt 0 -or $lag -gt $frames) { return [pscustomobject]@{ Status='CounterReset'; DroppedFramesStatus='Unavailable' } }
    [pscustomobject]@{ Status='Available'; Start=$a.EmittedAt; End=$b.EmittedAt; Elapsed=($b.EmittedAt-$a.EmittedAt)
        RenderedFrames=$frames; LaggedFrames=$lag; LaggedPercent=(100*$lag/$frames); DroppedFramesStatus='Unavailable' }
}
function Get-GpuProcessingEvidence($Block) {
    $stats=@($Block.Records | Where-Object {
        $_.Kind -eq 'stats' -and $_.Fields.filter_id -ceq $Block.FilterId -and
        ($_.EmittedAt-[double]$_.Fields.interval_host_elapsed_ms/1000) -ge $Block.MeasureStart -and $_.EmittedAt -le $Block.End
    })
    $result=[ordered]@{Count=$stats.Count;Start=$null;End=$null;Elapsed=0.0;Captured=0.0;Processed=0.0;Skipped=0.0;Stale=0.0
        InputReadbackPixels=0.0;SimilarityReadbackPixels=0.0;CaptureHostMs=0.0;InferenceHostMs=0.0;MaskHostMs=0.0}
    if($stats.Count) {
        $result.Start=$stats[0].EmittedAt-[double]$stats[0].Fields.interval_host_elapsed_ms/1000
        $result.End=$stats[-1].EmittedAt
        foreach($r in $stats) {
            $result.Elapsed+=[double]$r.Fields.interval_host_elapsed_ms/1000
            $result.Captured+=[double]$r.Fields.captured;$result.Processed+=[double]$r.Fields.processed
            $result.Skipped+=[double]$r.Fields.skipped;$result.Stale+=[double]$r.Fields.stale
            $result.InputReadbackPixels+=[double]$r.Fields.input_readback_pixels;$result.SimilarityReadbackPixels+=[double]$r.Fields.similarity_readback_pixels
            $result.CaptureHostMs+=[double]$r.Fields.capture_host_elapsed_ms;$result.InferenceHostMs+=[double]$r.Fields.inference_host_elapsed_ms
            $result.MaskHostMs+=[double]$r.Fields.mask_host_elapsed_ms
        }
    }
    return [pscustomobject]$result
}
function Compare-GpuProcessingBlocks([object[]]$Blocks) {
    $result=[ordered]@{ Status='Inconclusive'; Reason='invalid blocks'; OffMean=$null; OnMean=$null; OffMin=$null; OffMax=$null
        OnMin=$null; OnMax=$null; PercentagePoints=$null; RelativePercent=$null }
    if ($Blocks.Count -ne 4) { return [pscustomobject]$result }
    $pattern=@($false,$true,$true,$false)
    for ($i=0; $i -lt 4; $i++) {
        if ($Blocks[$i].On -ne $pattern[$i] -or -not (Test-GpuProcessingBlock $Blocks[$i])) { return [pscustomobject]$result }
        if ($i -and $Blocks[$i].Start -lt $Blocks[$i-1].End) { $result.Reason='overlapping blocks'; return [pscustomobject]$result }
        $a=@($Blocks[0].Records | Where-Object {$_.Kind -eq 'stats' -and $_.Fields.filter_id -ceq $Blocks[0].FilterId})[0].Fields
        $b=@($Blocks[$i].Records | Where-Object {$_.Kind -eq 'stats' -and $_.Fields.filter_id -ceq $Blocks[$i].FilterId})[0].Fields
        foreach ($key in @('filter_id','settings_fingerprint','source_fingerprint','source_width','source_height','input_width','input_height','fps_num','fps_den')) {
            if ($a[$key] -cne $b[$key]) { $result.Reason='different comparison settings/source'; return [pscustomobject]$result }
        }
        if ($Blocks[$i].Samples[-1].Pid -ne $Blocks[0].Samples[-1].Pid -or
            $Blocks[$i].Samples[-1].StartTicks -ne $Blocks[0].Samples[-1].StartTicks) { $result.Reason='process restarted'; return [pscustomobject]$result }
    }
    $off=@((Get-BlockCpuPercentage $Blocks[0]),(Get-BlockCpuPercentage $Blocks[3]))
    $on=@((Get-BlockCpuPercentage $Blocks[1]),(Get-BlockCpuPercentage $Blocks[2]))
    $result.OffMean=($off[0]+$off[1])/2; $result.OnMean=($on[0]+$on[1])/2
    $result.OffMin=[Math]::Min($off[0],$off[1]); $result.OffMax=[Math]::Max($off[0],$off[1])
    $result.OnMin=[Math]::Min($on[0],$on[1]); $result.OnMax=[Math]::Max($on[0],$on[1])
    $result.PercentagePoints=$result.OffMean-$result.OnMean
    if ($result.OffMean -gt 0) { $result.RelativePercent=100*$result.PercentagePoints/$result.OffMean }
    $result.Reason='ranges overlap'
    if ($result.OffMin -gt $result.OnMax) { $result.Status='ObservedImprovement'; $result.Reason='observed block ranges separated; no statistical confidence claimed' }
    [pscustomobject]$result
}
function Read-GpuYesNo([string]$Question) {
    while ($true) {
        $answer=(Read-Host ($Question+' [s/n]')).Trim().ToLowerInvariant()
        if ($answer -in @('s','sim')) { return $true }
        if ($answer -in @('n','nao','não')) { return $false }
    }
}
function Get-GpuManifest([string]$InstallerPath) {
    $text=Get-Content -LiteralPath $InstallerPath -Raw
    if ($text -notmatch '(?m)^# HANDOFF_MANIFEST:([A-Za-z0-9+/=]+)\r?$') { throw 'Instalador concreto em falta ou incompleto nesta pasta.' }
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Matches[1])) | ConvertFrom-Json
}
function Assert-GpuSafePath([string]$Path) {
    $current=[IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if (((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {throw 'Link/junction na instalação; comparação interrompida.'}
        }
        $parent=Split-Path -Parent $current;if($parent -eq $current){break};$current=$parent
    }
}
function Get-GpuInstalledFiles([string]$Root) {
    Assert-GpuSafePath $Root
    $directories=New-Object 'System.Collections.Generic.Stack[string]';$directories.Push($Root)
    $files=@()
    while($directories.Count) {
        foreach($item in @(Get-ChildItem -LiteralPath $directories.Pop() -Force)) {
            Assert-GpuSafePath $item.FullName
            if($item.PSIsContainer){$directories.Push($item.FullName)}else{$files+=$item}
        }
    }
    return $files
}
function Assert-GpuInstalled([string]$ScriptPath,[string]$ReceiptPath,$Manifest) {
    Assert-GpuSafePath $ReceiptPath;Assert-GpuSafePath $ScriptPath
    $receipt=Get-Content -LiteralPath $ReceiptPath -Raw | ConvertFrom-Json
    if ($receipt.State -cne 'installed' -or $receipt.SourceSha -cne $Manifest.SourceSha -or
        $receipt.ZipSha256 -cne $Manifest.ZipSha256 -or $receipt.MeasurementSha256 -cne $Manifest.MeasurementSha256) {
        throw 'Recibo não corresponde à entrega. Preserva os ficheiros e volta ao instalador desta pasta.'
    }
    if ((Get-FileHash -LiteralPath $ScriptPath -Algorithm SHA256).Hash -ine $Manifest.MeasurementSha256) { throw 'Script de comparação diferente da entrega.' }
    $expected=Join-Path $env:ProgramData 'obs-studio/plugins/obs-backgroundremoval'
    if ([IO.Path]::GetFullPath($receipt.Installed) -ine [IO.Path]::GetFullPath($expected)) { throw 'Pasta instalada diferente do recibo esperado.' }
    if (@(Get-GpuInstalledFiles $receipt.Installed).Count -ne $Manifest.Files.Count) {throw 'Inventário instalado diferente da entrega.'}
    foreach ($file in $Manifest.Files) {
        if ($file.Path -match '[:\\]' -or $file.Path.StartsWith('/') -or $file.Path -match '(^|/)\.\.(/|$)') {throw 'Caminho no manifesto inválido.'}
        $path=Join-Path $receipt.Installed $file.Path
        Assert-GpuSafePath $path
        if ((Get-Item -LiteralPath $path).Length -ne $file.Length -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.Sha256) { throw "Ficheiro instalado diferente: $($file.Path)" }
    }
    return $receipt
}
function Get-GpuProcessSample($Clock,[int]$ExpectedPid,[long]$ExpectedStartTicks) {
    $before=$Clock.Elapsed.TotalSeconds
    $process=Get-Process -Id $ExpectedPid -ErrorAction Stop
    $process.Refresh()
    $startTicks=$process.StartTime.ToUniversalTime().Ticks
    if ($process.Id -ne $ExpectedPid -or $startTicks -ne $ExpectedStartTicks -or $process.HasExited) { throw 'OBS reiniciado. Esta comparação é inconclusiva.' }
    $cpu=$process.TotalProcessorTime.TotalSeconds
    $after=$Clock.Elapsed.TotalSeconds
    # Timestamp the acquired counter, not the scheduled query. Reject >50ms acquisition in block validation.
    [pscustomobject]@{ At=$after; QueryStart=$before; QueryEnd=$after; AcquisitionSeconds=($after-$before)
        Pid=$process.Id; StartTicks=$startTicks; CpuSeconds=$cpu }
}
function New-GpuLogReader([string]$Path,[datetime]$WallStart) {
    $item=Get-Item -LiteralPath $Path
    [pscustomobject]@{ Path=$Path; Offset=$item.Length; CreationTicks=$item.CreationTimeUtc.Ticks; Pending=''; WallStart=$WallStart
        Records=(New-Object 'System.Collections.Generic.List[object]'); Errors=(New-Object 'System.Collections.Generic.List[object]') }
}
function Read-GpuFreshLog($Reader,[double]$Now) {
    $item=Get-Item -LiteralPath $Reader.Path
    if ($item.CreationTimeUtc.Ticks -ne $Reader.CreationTicks -or $item.Length -lt $Reader.Offset) { throw 'Log OBS substituído/truncado. Recomeça a comparação.' }
    $stream=New-Object IO.FileStream($Reader.Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try {
        $null=$stream.Seek($Reader.Offset,[IO.SeekOrigin]::Begin)
        $textReader=New-Object IO.StreamReader($stream,[Text.Encoding]::UTF8,$true,4096,$true)
        try { $chunk=$textReader.ReadToEnd(); $Reader.Offset=$stream.Position } finally { $textReader.Dispose() }
    } finally { $stream.Dispose() }
    $text=$Reader.Pending+$chunk; $parts=$text -split "\n"
    $Reader.Pending=$parts[-1]
    for ($i=0; $i -lt $parts.Count-1; $i++) {
        $line=$parts[$i].TrimEnd([char]13)
        if ($line -notmatch '^(\d{2}:\d{2}:\d{2}\.\d{3})[: ]') { continue }
        $tod=[TimeSpan]::ParseExact($Matches[1],'hh\:mm\:ss\.fff',[cultureinfo]::InvariantCulture)
        $wallNow=$Reader.WallStart.AddSeconds($Now)
        $wall=$wallNow.Date.Add($tod)
        if ($wall-$wallNow -gt [TimeSpan]::FromHours(12)) { $wall=$wall.AddDays(-1) }
        if ($wallNow-$wall -gt [TimeSpan]::FromHours(12)) { $wall=$wall.AddDays(1) }
        $emitted=($wall-$Reader.WallStart).TotalSeconds
        if ($line -match 'GPUImageProcessing ') {
            $record=ConvertFrom-GpuProcessingLine $line $Now $emitted
            if ($null -ne $record) { $Reader.Records.Add($record) }
            else { $Reader.Errors.Add([pscustomobject]@{ At=$Now; EmittedAt=$emitted; Reason='malformed processing record' }) }
        } elseif ($line -match '\[obs-backgroundremoval\]' -and $line -match '(?i)error|failed|exception|crash|outcome=(?!Ready)') {
            $Reader.Errors.Add([pscustomobject]@{ At=$Now; EmittedAt=$emitted; Reason='plugin error' })
        }
    }
}
function Wait-GpuMode($Reader,$Clock,[bool]$On,[string]$FilterId,[int]$Similarity,[double]$After) {
    $deadline=$Clock.Elapsed.TotalSeconds+20
    while ($Clock.Elapsed.TotalSeconds -lt $deadline) {
        Read-GpuFreshLog $Reader $Clock.Elapsed.TotalSeconds
        foreach ($r in @($Reader.Records | Where-Object { $_.Kind -eq 'stats' -and $_.ReceivedAt -ge $After -and $_.EmittedAt -ge $After -and $_.Fields.filter_id -ceq $FilterId })) {
            $f=$r.Fields
            if ($r.ReceivedAt-$r.EmittedAt -gt 1 -or [int]$f.image_similarity -ne $Similarity) { continue }
            if ($f.effective_inference -cne 'DmlExecutionProvider') { throw 'Seleciona GPU - DirectML; o processador efetivo ainda não é DirectML.' }
            if ($f.state -in @('CpuProcessingFallback','Unavailable')) { throw 'Processamento indisponível/fallback. Preserva o resumo; reinitialização explícita da sessão necessária.' }
            if (($On -and $f.state -eq 'Active' -and $f.requested -eq '1' -and $f.mask_active -eq '1' -and $f.preprocess_active -eq '1') -or
                (-not $On -and $f.state -eq 'Off' -and $f.requested -eq '0')) { return $r }
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'Não apareceu telemetria fresca do modo escolhido. Confirma MediaPipe, DirectML, checkbox e similaridade e volta a executar.'
}
function Invoke-GpuBlock($Reader,$Clock,$Ready,[bool]$On,[int]$Similarity,[int]$ObsPid,[long]$StartTicks,[int]$LogicalProcessors) {
    $start=$Ready.ReceivedAt
    # Align the readiness record to collector receipt time; preserve actual emitted/received times.
    $block=[pscustomobject]@{ On=$On; Start=$start; MeasureStart=0.0; End=0.0; LogicalProcessors=$LogicalProcessors
        Samples=@(); Records=@(); Errors=@(); FilterId=$Ready.Fields.filter_id; Similarity=$Similarity
        ObservationEnd=0.0; ClosingStatsAt=$null }
    $initial=$Ready
    $block.Records=@($initial)
    for ($i=0; $i -le 20; $i++) {
        $target=$start+$i
        while ($Clock.Elapsed.TotalSeconds -lt $target) {
            Read-GpuFreshLog $Reader $Clock.Elapsed.TotalSeconds
            Start-Sleep -Milliseconds 20
        }
        $sample=Get-GpuProcessSample $Clock $ObsPid $StartTicks
        $block.Samples+=$sample
        if ($i -eq 5) { $block.MeasureStart=$sample.At }
        if ($i -eq 20) { $block.End=$sample.At }
        Read-GpuFreshLog $Reader $Clock.Elapsed.TotalSeconds
    }
    # Allow one further five-second producer interval plus existing one-second delivery latency.
    # Do not take more CPU samples or change either measured endpoint while observing this tail.
    $deadline=$block.End+6.2
    while ($true) {
        $now=$Clock.Elapsed.TotalSeconds
        Read-GpuFreshLog $Reader $now
        $closing=@($Reader.Records | Where-Object { $_.Kind -eq 'stats' -and $_.Fields.filter_id -ceq $block.FilterId -and
            $_.EmittedAt -ge $block.End -and $_.EmittedAt-[double]$_.Fields.interval_host_elapsed_ms/1000 -le $block.End })
        $observed=$Clock.Elapsed.TotalSeconds
        # Completion time does not advance this read's byte snapshot. Require a read begun after the boundary.
        if (($now -ge ($block.End+1) -and $closing.Count -and -not $Reader.Pending) -or $now -ge $deadline) { break }
        Start-Sleep -Milliseconds 20
    }
    $block.ObservationEnd=$observed
    if ($closing.Count) { $block.ClosingStatsAt=$closing[0].EmittedAt }
    $block.Records=@($Reader.Records | Where-Object { $_.ReceivedAt -ge ($start-0.1) -and $_.ReceivedAt -le $block.ObservationEnd })
    if ($block.Records -notcontains $initial) { $block.Records=@($initial)+$block.Records }
    # Attribute delayed failures to emission, including the final block with no following prompt/drain.
    $block.Errors=@($Reader.Errors | Where-Object { $_.EmittedAt -ge $start -and $_.EmittedAt -le $block.End -and $_.At -le $block.ObservationEnd })
    if ($Reader.Pending) { $block.Errors+=[pscustomobject]@{ At=$now; Reason='incomplete line at bounded observation end' } }
    return $block
}

try {
    if ($env:OS -ne 'Windows_NT') { throw 'Executa esta comparação no Windows com OBS aberto.' }
    $manifest=Get-GpuManifest (Join-Path $PSScriptRoot 'instalar-processamento-gpu.ps1')
    $receiptPath=Join-Path $env:ProgramData ('obs-br-gpu-image-receipts/'+$manifest.SourceSha+'/installation.json')
    $receipt=Assert-GpuInstalled $PSCommandPath $receiptPath $manifest
    $processes=@(Get-Process obs64 -ErrorAction SilentlyContinue)
    if ($processes.Count -ne 1) { throw 'Abre uma única instância OBS x64.' }
    $obsProcess=$processes[0]; $obsPid=$obsProcess.Id; $startTicks=$obsProcess.StartTime.ToUniversalTime().Ticks
    if (-not $receipt.InstalledAt -or $obsProcess.StartTime.ToUniversalTime() -lt
        [datetime]::Parse($receipt.InstalledAt,[cultureinfo]::InvariantCulture).ToUniversalTime()) {throw 'Fecha/reabre OBS depois da instalação para medir a DLL exata desta entrega.'}
    $loaded=@($obsProcess.Modules | Where-Object { $_.ModuleName -ieq 'obs-backgroundremoval.dll' })
    $dllPath=Join-Path $receipt.Installed 'bin/64bit/obs-backgroundremoval.dll'
    if ($loaded.Count -ne 1 -or $loaded[0].FileName -ine $dllPath) { throw 'OBS não carregou esta instalação. Fecha/reabre OBS; usa o mesmo PowerShell administrador se o acesso for negado.' }
    $logical=[int](Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
    $log=Get-ChildItem -LiteralPath (Join-Path $env:APPDATA 'obs-studio/logs') -Filter '*.txt' -File | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (-not $log -or $log.LastWriteTimeUtc -lt $obsProcess.StartTime.ToUniversalTime()) { throw 'Log da sessão OBS atual não encontrado.' }
    $output=Join-Path $env:LOCALAPPDATA ('obs-br-gpu-image/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
    New-Item -Path $output -ItemType Directory -Force | Out-Null
    Write-Host 'Mantém cena, câmera, luz, resolução/FPS, movimento e definições. Seleciona MediaPipe e GPU - DirectML.'
    $null=Read-Host 'Confirma essas definições mantendo a tua similaridade atual e prime Enter'
    $clock=[Diagnostics.Stopwatch]::StartNew(); $reader=New-GpuLogReader $log.FullName (Get-Date)
    $cutoff=$clock.Elapsed.TotalSeconds
    Write-Host 'A aguardar telemetria fresca dos filtros ativos...'
    while ($clock.Elapsed.TotalSeconds -lt 7) { Read-GpuFreshLog $reader $clock.Elapsed.TotalSeconds; Start-Sleep -Milliseconds 100 }
    $active=@($reader.Records | Where-Object { $_.Kind -eq 'stats' -and $_.EmittedAt -ge $cutoff } | Group-Object { $_.Fields.filter_id })
    if (-not $active.Count) { throw 'Nenhum filtro ativo com telemetria completa; confirma a fonte visível.' }
    $filterId=$active[0].Name
    if ($active.Count -gt 1) {
        Write-Host ('Filtros ativos (IDs): '+(($active | Select-Object -ExpandProperty Name)-join ', '))
        $filterId=(Read-Host 'Indica o ID único do filtro a comparar').Trim()
        if ($filterId -notin @($active | Select-Object -ExpandProperty Name)) { throw 'Filtro não identificado; comparação inconclusiva.' }
    }
    $original=@($reader.Records | Where-Object { $_.Kind -eq 'stats' -and $_.Fields.filter_id -ceq $filterId })[-1].Fields
    $preserved=[int]$original.image_similarity
    $similarity=0
    if ($ManterSimilaridade) { $similarity=$preserved; Write-Host 'Mantém a tua similaridade atual; o valor efetivo foi registado e será validado em todos os blocos.' }
    else { Write-Host 'Para esta comparação desliga Similaridade de imagem nos dois modos. No final repõe a tua opção registada.' }
    $blocks=@()
    foreach ($on in @($false,$true,$true,$false)) {
        $mode='OFF'; if ($on) { $mode='ON' }
        $null=Read-Host ("Define Processamento de imagem na GPU = $mode e prime Enter")
        $ready=Wait-GpuMode $reader $clock $on $filterId $similarity $clock.Elapsed.TotalSeconds
        Write-Host "${mode}: 5 segundos para estabilizar, depois 15 segundos de medição; até 6,2 segundos adicionais para fechar os logs. Mantém trabalho/movimento comparável."
        $block=Invoke-GpuBlock $reader $clock $ready $on $similarity $obsPid $startTicks $logical
        $blocks+=$block
        if (-not (Test-GpuProcessingBlock $block)) { Write-Host 'Bloco inválido; o resumo será inconclusivo.' }
    }
    $comparison=Compare-GpuProcessingBlocks $blocks
    $quality=Read-GpuYesNo 'Máscara, cabelo/bordos e movimento mantiveram qualidade e resposta sem flicker/halos novos?'
    $switch=Read-GpuYesNo 'Checkbox ON/OFF e CPU -> DirectML -> CPU funcionaram sem crash?'
    $resize=Read-GpuYesNo 'Alteraste o tamanho da fonte e a máscara manteve o alinhamento?'
    $recreated=Read-GpuYesNo 'Removeste e recriaste o filtro e a máscara voltou a funcionar?'
    if (-not $ManterSimilaridade -and $preserved) { Write-Host 'Repõe agora Similaridade de imagem na tua opção anterior (ligada).' }
    $lag=@($blocks | ForEach-Object { Get-GpuLagEvidence $_ })
    $processing=@($blocks | ForEach-Object { Get-GpuProcessingEvidence $_ })
    $facts=[ordered]@{ SourceSha=$manifest.SourceSha; ObsVersion=$obsProcess.MainModule.FileVersionInfo.FileVersion; LogicalProcessors=$logical; Pid=$obsPid; ProcessStartTicks=$startTicks
        WindowsVersion=[Environment]::OSVersion.Version.ToString(); Drivers=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
        Similarity=$similarity; OriginalSettings=$original; PreservedSimilarityRun=[bool]$ManterSimilaridade; Comparison=$comparison; Rendering=$lag; Processing=$processing
        FunctionalQuality=[ordered]@{ Quality=$quality; Switching=$switch; Resize=$resize; Recreated=$recreated }
        Blocks=$blocks; DroppedFrameEvidence='Unavailable unless owner supplies fresh OBS output statistics' }
    $facts | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'resultado-local.json') -Encoding UTF8
    Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $output 'obs-log-local.txt')
    $summary=@('GPU image processing — comparação OBS'; "Fonte: $($manifest.SourceSha)"; "Similaridade: $similarity; modo preservado: $([bool]$ManterSimilaridade)"
        "CPU: $($comparison.Status); OFF média $($comparison.OffMean)%; ON média $($comparison.OnMean)%; diferença $($comparison.PercentagePoints) pontos percentuais; relativo $($comparison.RelativePercent)%"
        "Variação: OFF [$($comparison.OffMin), $($comparison.OffMax)]%; ON [$($comparison.OnMin), $($comparison.OnMax)]%"
        "Qualidade: $quality; alternância: $switch; resize: $resize; recriação: $recreated"
        "OBS: $($facts.ObsVersion); processadores lógicos: $logical; Windows: $($facts.WindowsVersion)"
        'Comparação sequencial ao vivo; sem confiança estatística. Tempos de componentes são host elapsed, incluindo esperas.'
        'CPU usa os instantes reais de leitura; aquisição >50 ms ou desvio da janela >50 ms invalida o bloco. Observação dos logs até 6,2 s após CPU, sem prolongar a medição.'
        'Rendering lag usa limites dos registos de telemetria, diferentes da janela exata CPU; dropped/network frames: evidência incompleta.'
        'Os logs e amostras completos ficam locais. Envia apenas este resumo.')
    $settings=@($blocks[0].Records | Where-Object {$_.Kind -eq 'stats' -and $_.Fields.filter_id -ceq $filterId})[0].Fields
    $summary+=("Filtro: $filterId; fonte: $($settings.source_fingerprint); definições: $($settings.settings_fingerprint); fonte $($settings.source_width)x$($settings.source_height); input $($settings.input_width)x$($settings.input_height); FPS $($settings.fps_num)/$($settings.fps_den)")
    $summary+=("Máscara: threshold=$($settings.enable_threshold)/$($settings.threshold); temporal=$($settings.temporal_smooth_factor); contour=$($settings.contour_filter); smooth=$($settings.smooth_contour); feather=$($settings.feather); expansion=$($settings.mask_expansion); skip=$($settings.mask_every_x_frames); similarity_threshold=$($settings.similarity_threshold); blur=$($settings.blur_background); focal=$($settings.focal_blur)/$($settings.focus_point)/$($settings.focus_depth)")
    foreach ($driver in $facts.Drivers) { $summary+=("GPU: $($driver.Name); driver: $($driver.DriverVersion)") }
    for ($i=0; $i -lt $blocks.Count; $i++) {
        $records=@($blocks[$i].Records | Where-Object {$_.Kind -eq 'stats' -and $_.Fields.filter_id -ceq $filterId})
        $last=$records[-1].Fields
        $queryMax=($blocks[$i].Samples | Measure-Object -Property AcquisitionSeconds -Maximum).Maximum
        $summary+=("Limites bloco "+($i+1)+": CPU=$($blocks[$i].MeasureStart)..$($blocks[$i].End)s; fecho_telemetria=$($blocks[$i].ClosingStatsAt)s; observação_até=$($blocks[$i].ObservationEnd)s; aquisição_CPU_máxima_ms="+($queryMax*1000))
        $summary+=("Bloco "+($i+1)+": válido="+(Test-GpuProcessingBlock $blocks[$i])+"; PID=$obsPid; geração=$($last.generation); provider=$($last.effective_inference); requested=$($last.requested); state=$($last.state); stages=$($last.preprocess_active)/$($last.mask_active); captured=$($last.captured); input_pixels=$($last.input_readback_pixels); similarity_pixels=$($last.similarity_readback_pixels); intervalo_telemetria_host_ms=$($last.interval_host_elapsed_ms); capture/inference/mask_host_ms=$($last.capture_host_elapsed_ms)/$($last.inference_host_elapsed_ms)/$($last.mask_host_elapsed_ms)")
    }
    for ($i=0; $i -lt $lag.Count; $i++) { $summary+=("Bloco "+($i+1)+" render: "+($lag[$i] | ConvertTo-Json -Compress)) }
    for ($i=0; $i -lt $processing.Count; $i++) { $summary+=("Bloco "+($i+1)+" totais de intervalos completos dentro da janela CPU: "+($processing[$i] | ConvertTo-Json -Compress)) }
    $summary | Set-Content -LiteralPath (Join-Path $output 'resumo.txt') -Encoding UTF8
    $summary | ForEach-Object { Write-Host $_ }
    Write-Host "Ficheiros locais: $output"
    exit 0
} catch {
    Write-Host ('INCONCLUSIVO: '+$_.Exception.Message)
    if (Get-Variable output -ErrorAction SilentlyContinue) {
        if (Get-Variable blocks -ErrorAction SilentlyContinue) { $blocks | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'blocos-incompletos.json') -Encoding UTF8 }
        if (Get-Variable log -ErrorAction SilentlyContinue) { Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $output 'obs-log-local.txt') -ErrorAction SilentlyContinue }
    }
    Write-Host 'Preserva os ficheiros locais. Nenhum resultado de poupança foi aceite.'
    exit 1
}
