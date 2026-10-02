# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

# Test-only deployment for the real libobs adapter fixture; never installed or packaged.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ObsDepsPrefix,
    [Parameter(Mandatory)][string]$AdapterDirectory,
    [Parameter(Mandatory)][string]$WindowsMlRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Require-SafeItem([string]$Path, [bool]$Directory) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($Directory) {
        if ($item -isnot [IO.DirectoryInfo]) { throw "Required directory is missing: $Path" }
    } elseif ($item -isnot [IO.FileInfo] -or $item.Length -eq 0) {
        throw "Required nonempty regular file is missing: $Path"
    }
    # Check every ancestor as well as the file: junctions/symlinks must not redirect
    # the explicit prefixes. Hard links are also unsafe destination/source aliases.
    $current = $item
    while ($null -ne $current) {
        if (($current.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            -not [string]::IsNullOrEmpty($current.LinkType)) {
            throw "Filesystem aliases are prohibited: $($current.FullName)"
        }
        if ($current -is [IO.FileInfo]) { $parent = $current.Directory }
        else { $parent = $current.Parent }
        if ($null -eq $parent) { break }
        $current = Get-Item -LiteralPath $parent.FullName -Force
    }
    return $item.FullName
}

function Find-File([string]$Directory, [string]$Name, [bool]$Required) {
    # Include directories/hidden entries so a conflicting non-file cannot be missed.
    $candidates = @(Get-ChildItem -LiteralPath $Directory -Force | Where-Object { $_.Name -ieq $Name })
    if ($candidates.Count -gt 1) { throw "Duplicate case-insensitive basename: $Name in $Directory" }
    if ($candidates.Count -eq 0) {
        if ($Required) { throw "Required file is missing: $Name in $Directory" }
        return $null
    }
    return Require-SafeItem $candidates[0].FullName $false
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath (Require-SafeItem $Path $false) -Algorithm SHA256).Hash.ToLowerInvariant()
}

$prefix = Require-SafeItem $ObsDepsPrefix $true
$sourceBin = Require-SafeItem (Join-Path $prefix 'bin') $true
$adapter = Require-SafeItem $AdapterDirectory $true
$package = Require-SafeItem $WindowsMlRoot $true
$native = Require-SafeItem (Join-Path $package 'runtimes/win-x64/native') $true
foreach ($sourceDirectory in @($sourceBin, $native)) {
    $sourcePath = [IO.Path]::TrimEndingDirectorySeparator($sourceDirectory)
    $adapterPath = [IO.Path]::TrimEndingDirectorySeparator($adapter)
    $separator = [IO.Path]::DirectorySeparatorChar
    $comparison = [StringComparison]::OrdinalIgnoreCase
    if ($adapterPath.Equals($sourcePath, $comparison) -or
        $adapterPath.StartsWith($sourcePath + $separator, $comparison) -or
        $sourcePath.StartsWith($adapterPath + $separator, $comparison)) {
        throw 'Adapter and supplied source directories must not overlap'
    }
}
$null = Find-File $adapter 'windows-ml-plugin-session.exe' $true

# The package is the already validated, pinned Windows ML 2.2.12 workflow output.
# Preserve all five original runtimes; prove the protected three against that origin.
$protected = @()
foreach ($name in @('obs.dll', 'w32-pthreads.dll', 'Microsoft.Windows.AI.MachineLearning.dll',
                    'onnxruntime.dll', 'DirectML.dll')) {
    $path = Find-File $adapter $name $true
    $hash = Get-Sha256 $path
    $origin = $null
    if ($name -notin @('obs.dll', 'w32-pthreads.dll')) {
        $origin = Find-File $native $name $true
        if ((Get-Sha256 $origin) -cne $hash) { throw "Runtime differs from pinned Windows ML origin: $name" }
    }
    $protected += [pscustomobject]@{ Name = $name; Path = $path; Hash = $hash; Origin = $origin }
}

# Proven closure for OBS 0052d024 + hash-verified windows-deps-2026-07-15-x64.zip.
# Do not broaden this list without rechecking the pinned package's PE imports.
$closure = @('avcodec-62.dll', 'avformat-62.dll', 'avutil-60.dll', 'librist.dll',
             'libx264-164.dll', 'srt.dll', 'swresample-6.dll', 'swscale-9.dll', 'zlib.dll')
$deployment = @()
foreach ($name in $closure) {
    $source = Find-File $sourceBin $name $true
    $hash = Get-Sha256 $source
    $existing = Find-File $adapter $name $false
    if ($null -ne $existing -and (Get-Sha256 $existing) -cne $hash) {
        throw "Existing adapter DLL differs from supplied OBS dependency: $name"
    }
    $destination = if ($null -ne $existing) { $existing } else { Join-Path $adapter $name }
    $deployment += [pscustomobject]@{
        Name = $name; Source = $source; Destination = $destination; Hash = $hash; Exists = ($null -ne $existing)
    }
}

# Every input and collision has passed preflight before the first copy.
foreach ($entry in $deployment) {
    if (-not $entry.Exists) {
        # No overwrite, including a destination created after preflight.
        [IO.File]::Copy($entry.Source, $entry.Destination, $false)
    }
    if ((Get-Sha256 $entry.Destination) -cne $entry.Hash -or
        (Get-Sha256 $entry.Source) -cne $entry.Hash) {
        throw "Staged OBS dependency differs from its supplied bytes: $($entry.Name)"
    }
    Write-Output "$($entry.Name) sha256=$($entry.Hash)"
}
foreach ($entry in $protected) {
    if ((Get-Sha256 $entry.Path) -cne $entry.Hash -or
        ($null -ne $entry.Origin -and (Get-Sha256 $entry.Origin) -cne $entry.Hash)) {
        throw "Protected runtime or its pinned origin changed during staging: $($entry.Name)"
    }
}
