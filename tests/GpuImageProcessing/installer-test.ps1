# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [string]$InstallerPath=(Join-Path $PSScriptRoot '../../scripts/instalar-processamento-gpu.ps1.in'),
    [Parameter(Mandatory)][string]$PluginZip
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if (-not (Test-Path -LiteralPath $InstallerPath)) { throw 'Task6 expected RED: installer template absent.' }
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($InstallerPath,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($definition in $ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$true)) { Invoke-Expression $definition.Extent.Text }
$script:deliveryFaultPoint=$null
# Fault injection lives only in fixtures. Wrappers still execute actual filesystem operations.
$script:realMove=${function:Move-DeliveryDirectory}
$script:realCopy=${function:Copy-DeliveryDirectory}
$script:realWrite=${function:Write-DeliveryReceipt}
$script:realExpand=${function:Expand-VerifiedPlugin}
function Fault([string]$Point) {
    if ($script:deliveryFaultPoint -eq $Point) { $script:deliveryFaultPoint=$null; throw "Injected filesystem failure: $Point" }
}
function Move-DeliveryDirectory([string]$Source,[string]$Destination) {
    & $script:realMove $Source $Destination
    if ((Split-Path -Leaf $Destination) -eq 'previous-plugin') { Fault 'PreviousMoved' }
    elseif ((Split-Path -Leaf $Destination) -like 'withdrawn-new-*') { Fault 'RestoreWithdrawn' }
    elseif ((Split-Path -Leaf $Source) -like 'restore-*') { Fault 'RestoreInstalled' }
    elseif ((Split-Path -Leaf $Source) -eq 'obs-backgroundremoval') { Fault 'NewInstalled' }
}
function Copy-DeliveryDirectory([string]$Source,[string]$Destination) {
    & $script:realCopy $Source $Destination
    if ((Split-Path -Leaf $Destination) -like 'restore-*') { Fault 'RestoreStaged' }
}
function Write-DeliveryReceipt($Receipt,[string]$Path) {
    & $script:realWrite $Receipt $Path
    if ($Receipt.State -eq 'installed') { Fault 'ReceiptInstalled' }
    if ($Receipt.State -eq 'restored') { Fault 'ReceiptRestored' }
}
function Expand-VerifiedPlugin([string]$Zip,[string]$Destination,$Manifest) { & $script:realExpand $Zip $Destination $Manifest; Fault 'StageVerified' }
function Check([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Fails([scriptblock]$Action,[string]$Message) { $failed=$false; try { & $Action | Out-Null } catch {$failed=$true}; Check $failed $Message }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temp=Join-Path ([IO.Path]::GetTempPath()) ('gpu-installer-'+[guid]::NewGuid().ToString('N'))
New-Item -Path $temp -ItemType Directory | Out-Null
try {
    $archive=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($PluginZip))
    $files=@()
    try {
        foreach ($entry in $archive.Entries) {
            if (-not $entry.Name) { continue }
            Check ($entry.FullName.StartsWith('obs-backgroundremoval/')) 'Fixture must be the actual plugin ZIP.'
            $stream=$entry.Open(); $algorithm=[Security.Cryptography.SHA256]::Create()
            try { $hash=[BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
            finally { $algorithm.Dispose(); $stream.Dispose() }
            $files+=[pscustomobject]@{Path=$entry.FullName.Substring('obs-backgroundremoval/'.Length); Sha256=$hash; Length=$entry.Length}
        }
    } finally {$archive.Dispose()}
    $manifest=[pscustomobject]@{SchemaVersion=1; SourceSha=('a'*40); ZipSha256=(Get-DeliveryHash $PluginZip); MeasurementSha256=('b'*64)
        Files=$files; RunId='1'; ArtifactName='fixture'; WindowsMlVersion='2.2.12'}
    $stage=Join-Path $temp 'package'
    Expand-VerifiedPlugin $PluginZip $stage $manifest
    Assert-DeliveryTree (Join-Path $stage 'obs-backgroundremoval') $files
    $installed=Join-Path $temp 'live/plugin'; $backup=Join-Path $temp 'backups/transaction'; $receipt=Join-Path $temp 'receipts/new.json'
    New-Item -Path $installed -ItemType Directory -Force | Out-Null
    'accepted previous bytes' | Set-Content -LiteralPath (Join-Path $installed 'previous.txt') -Encoding UTF8
    $priorReceipt=Join-Path $temp 'receipts/accepted.json'
    New-Item -Path (Split-Path -Parent $priorReceipt) -ItemType Directory -Force | Out-Null
    '{"State":"installed","accepted":"actual prior receipt"}' | Set-Content -LiteralPath $priorReceipt -Encoding UTF8
    $previousHash=Get-DeliveryHash (Join-Path $installed 'previous.txt'); $previousReceiptHash=Get-DeliveryHash $priorReceipt
    $r=Install-DeliveryPlugin $PluginZip $installed $backup $receipt $priorReceipt $manifest
    Check ($r.State -eq 'installed') 'Installed receipt not committed.'
    Check ((Get-DeliveryHash $priorReceipt) -eq $previousReceiptHash) 'Accepted prior receipt mutated.'
    Check ((Get-DeliveryHash (Join-Path $r.Previous 'previous.txt')) -eq $previousHash) 'Accepted prior tree missing.'
    Restore-DeliveryPlugin $receipt $manifest
    Check ((Get-DeliveryHash (Join-Path $installed 'previous.txt')) -eq $previousHash) 'Previous plugin not restored.'
    Check ((Get-DeliveryHash $priorReceipt) -eq $previousReceiptHash) 'Actual accepted receipt not restored/preserved.'
    foreach ($point in @('StageVerified','PreviousMoved','NewInstalled','ReceiptInstalled')) {
        $base=Join-Path $temp $point; $live=Join-Path $base 'live'; $back=Join-Path $base 'backup'; $rec=Join-Path $base 'receipt.json'
        New-Item -Path $live -ItemType Directory -Force | Out-Null
        'accepted' | Set-Content -LiteralPath (Join-Path $live 'previous.txt') -Encoding UTF8
        $old=Get-DeliveryHash (Join-Path $live 'previous.txt')
        $script:deliveryFaultPoint=$point
        Fails { Install-DeliveryPlugin $PluginZip $live $back $rec $priorReceipt $manifest } "Fault $point did not fail."
        $script:deliveryFaultPoint=$null
        Check ((Get-DeliveryHash (Join-Path $live 'previous.txt')) -eq $old) "Fault $point lost accepted live tree."
        Check ((Get-DeliveryHash $priorReceipt) -eq $previousReceiptHash) "Fault $point lost prior receipt."
    }
    foreach ($point in @('RestoreStaged','RestoreWithdrawn','RestoreInstalled','ReceiptRestored')) {
        $base=Join-Path $temp $point; $live=Join-Path $base 'live'; $back=Join-Path $base 'backup'; $rec=Join-Path $base 'receipt.json'
        New-Item -Path $live -ItemType Directory -Force | Out-Null
        'accepted' | Set-Content -LiteralPath (Join-Path $live 'previous.txt') -Encoding UTF8
        $r=Install-DeliveryPlugin $PluginZip $live $back $rec $priorReceipt $manifest
        $newHash=Get-DeliveryHash (Join-Path $live 'bin/64bit/obs-backgroundremoval.dll')
        $script:deliveryFaultPoint=$point
        Fails { Restore-DeliveryPlugin $rec $manifest } "Restore fault $point did not fail."
        $script:deliveryFaultPoint=$null
        Check ((Get-DeliveryHash (Join-Path $live 'bin/64bit/obs-backgroundremoval.dll')) -eq $newHash) "Restore fault $point lost live new tree."
        Check ((Get-Content -LiteralPath $rec -Raw | ConvertFrom-Json).State -eq 'installed') "Restore fault $point receipt state untruthful."
        Check ((Get-DeliveryHash $priorReceipt) -eq $previousReceiptHash) "Restore fault $point changed accepted receipt."
    }
    $copies=Join-Path $temp 'downloads'; New-Item -Path $copies -ItemType Directory | Out-Null
    Copy-Item -LiteralPath $PluginZip -Destination (Join-Path $copies 'arbitrary (2).zip')
    Check ((Find-DeliveryZip $null $copies $copies $manifest.ZipSha256) -eq (Join-Path $copies 'arbitrary (2).zip')) 'Collision ZIP discovery failed.'
    $badManifest=$manifest.PSObject.Copy(); $badManifest.ZipSha256=('0'*64)
    Fails { Expand-VerifiedPlugin $PluginZip (Join-Path $temp 'badzip') $badManifest } 'Wrong ZIP digest accepted.'
    $badManifest=$manifest.PSObject.Copy(); $badManifest.Files=@($files | Select-Object -Skip 1)
    Fails { Expand-VerifiedPlugin $PluginZip (Join-Path $temp 'missing') $badManifest } 'Unexpected ZIP member accepted.'
    foreach ($name in @('bin/64bit/obs-backgroundremoval.dll','bin/64bit/onnxruntime.dll','data/effects/input_downscale.effect')) {
        $badManifest=$manifest.PSObject.Copy()
        $badManifest.Files=@($files | ForEach-Object {$copy=$_.PSObject.Copy();if($copy.Path -ceq $name){$copy.Sha256=('0'*64)};$copy})
        Fails { Expand-VerifiedPlugin $PluginZip (Join-Path $temp ([guid]::NewGuid().ToString('N'))) $badManifest } "Wrong file digest accepted: $name"
    }
    # Historical installed-state receipts cannot be used for a different currently installed build.
    $receiptRoots=Join-Path $temp 'receipt-discovery';New-Item -Path $receiptRoots -ItemType Directory | Out-Null
    $dll=Join-Path $temp 'discovery-live/bin/64bit/obs-backgroundremoval.dll'
    New-Item -Path (Split-Path -Parent $dll) -ItemType Directory -Force | Out-Null
    'current DLL'|Set-Content -LiteralPath $dll -Encoding UTF8
    foreach($name in @('old','current')) {
        $directory=Join-Path $receiptRoots $name;New-Item -Path $directory -ItemType Directory|Out-Null
        $hash=('0'*64);if($name -eq 'current'){$hash=Get-DeliveryHash $dll}
        @{State='installed';Installed=(Join-Path $temp 'discovery-live');PluginDllSha256=$hash}|ConvertTo-Json|
            Set-Content -LiteralPath (Join-Path $directory 'installation.json') -Encoding UTF8
    }
    Check ((Find-PreviousDeliveryReceipt (Join-Path $temp 'discovery-live') @($receiptRoots)) -eq
        (Join-Path $receiptRoots 'current/installation.json')) 'Historical receipt selected or caused false ambiguity.'
    Fails { Assert-DeliveryRelativePath '../escape.dll' } 'Traversal accepted.'
    Fails { Assert-DeliveryRelativePath 'C:/escape.dll' } 'Absolute path accepted.'
    Fails { Assert-DeliveryRelativePath 'bin/64bit/a.dll:stream' } 'Alternate data stream accepted.'
    Fails { Assert-DeliveryRelativePath 'bin//a.dll' } 'Noncanonical path accepted.'
    $linked=Join-Path $temp 'linked'
    if ($env:OS -eq 'Windows_NT') {New-Item -ItemType Junction -Path $linked -Target $stage|Out-Null}
    else {New-Item -ItemType SymbolicLink -Path $linked -Target $stage|Out-Null}
    Fails { Assert-DeliveryTree $linked $files } 'Reparse/symlink root accepted.'
    $installedWithExtra=Join-Path $temp 'extra-dir'
    Copy-Item -LiteralPath (Join-Path $stage 'obs-backgroundremoval') -Destination $installedWithExtra -Recurse
    $nestedLink=Join-Path $installedWithExtra 'extra-link'
    if ($env:OS -eq 'Windows_NT') {New-Item -ItemType Junction -Path $nestedLink -Target $stage|Out-Null}
    else {New-Item -ItemType SymbolicLink -Path $nestedLink -Target $stage|Out-Null}
    Fails { Assert-DeliveryTree $installedWithExtra $files } 'Nested reparse/symlink directory accepted.'
    'installer real-ZIP and transaction faults PASS'
} finally { Remove-Item -LiteralPath $temp -Recurse -Force }
