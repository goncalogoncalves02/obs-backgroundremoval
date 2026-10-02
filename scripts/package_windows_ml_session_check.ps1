# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$ModelPath,
    [Parameter(Mandatory)][string]$WindowsMlRoot,
    [Parameter(Mandatory)][string]$SourceCommit,
    [Parameter(Mandatory)][string]$DestinationZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Require-SafeFile([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer -or $item.Length -eq 0) {
        throw "Required nonempty file is missing: $Path"
    }
    # Reject links/junctions in both the file and every ancestor.
    $current = $item
    while ($null -ne $current) {
        if (($current.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse points are prohibited: $($current.FullName)"
        }
        if ($current -is [IO.FileInfo]) { $current = $current.Directory }
        else { $current = $current.Parent }
    }
    return $item.FullName
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath (Require-SafeFile $Path) -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Invoke-SourceGit([string[]]$Arguments) {
    $result = & git -C $repository @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Source verification failed: git $Arguments" }
    return ($result -join "`n").Trim()
}

$repository = Split-Path -Parent $PSScriptRoot
$destination = [IO.Path]::GetFullPath($DestinationZip)
if (Test-Path -LiteralPath $destination) { throw "Destination already exists: $destination" }
if ([IO.Path]::GetFileName($destination) -cne 'windows-ml-session-check_2.2.12_x64.zip') {
    throw 'Destination must be windows-ml-session-check_2.2.12_x64.zip'
}
if ($SourceCommit -cnotmatch '^[0-9a-f]{40}$') { throw 'SourceCommit must be an immutable full lowercase source SHA' }
if ((Invoke-SourceGit @('rev-parse', 'HEAD')) -cne $SourceCommit) { throw 'Actual checkout HEAD differs from SourceCommit' }
$trackedBlob = Invoke-SourceGit @('rev-parse', "${SourceCommit}:data/models/mediapipe.onnx")
$model = Require-SafeFile $ModelPath
if ((Invoke-SourceGit @('hash-object', '--no-filters', '--', $model)) -cne $trackedBlob) {
    throw 'Model bytes differ from the tracked MediaPipe model at SourceCommit'
}

$versionFile = Require-SafeFile (Join-Path $WindowsMlRoot 'build/cmake/microsoft.windows.ai.machinelearning-config.cmake')
$versionContent = Get-Content -LiteralPath $versionFile -Raw
$versionHash = Get-Sha256 $versionFile
if ($versionContent -cnotmatch 'set\(WINML_VERSION\s+"2\.2\.12"\)') {
    throw 'The Windows ML package must declare pinned version 2.2.12'
}

$sources = [ordered]@{}
$origins = [ordered]@{}
$sources['windows-ml-session-check.exe'] = Require-SafeFile (Join-Path $BuildDirectory 'windows-ml-session-check.exe')
$origins['windows-ml-session-check.exe'] = 'build/windows-ml-session-check.exe'
$sources['mediapipe.onnx'] = $model
$origins['mediapipe.onnx'] = 'source/data/models/mediapipe.onnx'
foreach ($name in @('Microsoft.Windows.AI.MachineLearning.dll', 'onnxruntime.dll', 'DirectML.dll')) {
    $runtime = Require-SafeFile (Join-Path $BuildDirectory $name)
    $packageFile = Require-SafeFile (Join-Path $WindowsMlRoot "runtimes/win-x64/native/$name")
    if ((Get-Sha256 $runtime) -cne (Get-Sha256 $packageFile)) {
        throw "Runtime differs from the pinned Windows ML package: $name"
    }
    $sources[$name] = $runtime
    $origins[$name] = "windows_ml/runtimes/win-x64/native/$name"
}
$legalFiles = [ordered]@{
    'windows-ml-license.txt' = 'license.txt'
    'windows-ml-third-party-notices.txt' = 'ThirdPartyNotices.txt'
}
foreach ($name in $legalFiles.Keys) {
    $sources[$name] = Require-SafeFile (Join-Path $WindowsMlRoot $legalFiles[$name])
    $origins[$name] = "windows_ml/$($legalFiles[$name])"
}

$stage = Join-Path ([IO.Path]::GetTempPath()) ([IO.Path]::GetRandomFileName())
$createdArchive = $false
try {
    New-Item -Path $stage -ItemType Directory | Out-Null
    $receipts = @()
    foreach ($name in $sources.Keys) {
        $hash = Get-Sha256 $sources[$name]
        Copy-Item -LiteralPath $sources[$name] -Destination (Join-Path $stage $name)
        if ((Get-Sha256 (Join-Path $stage $name)) -cne $hash) { throw "Staged file changed: $name" }
        $receipts += [ordered]@{ path = $name; sha256 = $hash; origin = $origins[$name] }
    }
    $manifest = [ordered]@{
        schema_version = 1
        source_commit = $SourceCommit
        windows_ml_package_version = '2.2.12'
        model_git_blob = $trackedBlob
        files = $receipts
    }
    $manifestPath = Join-Path $stage 'manifest.json'
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $hashes = @{}
    foreach ($receipt in $receipts) { $hashes[$receipt.path] = $receipt.sha256 }
    $hashes['manifest.json'] = Get-Sha256 $manifestPath
    $stageFiles = @(Get-ChildItem -LiteralPath $stage -File)
    $createdArchive = $true
    Compress-Archive -LiteralPath $stageFiles.FullName -DestinationPath $destination
    $archive = [IO.Compression.ZipFile]::OpenRead($destination)
    try {
        if ($archive.Entries.Count -ne $hashes.Count) { throw 'Archive has an unexpected entry count' }
        $seen = @{}
        foreach ($entry in $archive.Entries) {
            # Exact allowlist also forbids directories, traversal, alternate case and duplicate paths.
            if ($entry.FullName -cne $entry.Name -or -not ($hashes.Keys -ccontains $entry.FullName) -or
                $seen.ContainsKey($entry.FullName)) { throw "Unsafe or unexpected archive entry: $($entry.FullName)" }
            $seen[$entry.FullName] = $true
            $stream = $entry.Open()
            $algorithm = [Security.Cryptography.SHA256]::Create()
            try { $digest = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
            finally { $algorithm.Dispose(); $stream.Dispose() }
            if ($digest -cne $hashes[$entry.FullName]) { throw "Archive bytes changed: $($entry.FullName)" }
        }
    } finally { $archive.Dispose() }
    # Recheck provenance after compression, rather than trusting a mutable source receipt.
    if ((Get-Sha256 $versionFile) -cne $versionHash) { throw 'Package version metadata changed while packaging' }
    foreach ($name in $sources.Keys) {
        if ((Get-Sha256 $sources[$name]) -cne $hashes[$name]) { throw "Source changed while packaging: $name" }
        if ($origins[$name].StartsWith('windows_ml/')) {
            $relative = $origins[$name].Substring('windows_ml/'.Length)
            if ((Get-Sha256 (Join-Path $WindowsMlRoot $relative)) -cne $hashes[$name]) {
                throw "Package origin changed while packaging: $name"
            }
        }
    }
    if ((Invoke-SourceGit @('rev-parse', 'HEAD')) -cne $SourceCommit) { throw 'Source HEAD changed while packaging' }
    if ((Invoke-SourceGit @('hash-object', '--no-filters', '--', $model)) -cne $trackedBlob) {
        throw 'Tracked model changed while packaging'
    }
    "source_commit=$SourceCommit"
    "archive_sha256=$(Get-Sha256 $destination)"
    'status=ok'
} catch {
    if ($createdArchive -and (Test-Path -LiteralPath $destination)) { Remove-Item -LiteralPath $destination }
    throw
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
