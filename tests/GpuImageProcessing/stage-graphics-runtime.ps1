# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

# Test-only staging. Explicit verified workflow roots; never install or modify PATH.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ObsBuildDirectory,
    [Parameter(Mandatory)][string]$ObsDepsPrefix,
    [Parameter(Mandatory)][string]$VcpkgInstalledPrefix,
    [Parameter(Mandatory)][string]$TestDirectory
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


function Find-BuiltFile([string]$Root, [string]$Name) {
    $matches = @(Get-ChildItem -LiteralPath $Root -Recurse -Force -Filter $Name)
    if ($matches.Count -eq 0) { throw "Pinned OBS build output is missing: $Name" }
    $first = Require-SafeItem $matches[0].FullName $false
    $hash = Get-Sha256 $first
    # OBS also copies its output into rundir. Accept duplicated outputs only if byte-identical.
    foreach ($entry in $matches) {
        if ((Get-Sha256 $entry.FullName) -cne $hash) { throw "Conflicting pinned OBS outputs: $Name" }
    }
    return $first
}

$build = Require-SafeItem $ObsBuildDirectory $true
$deps = Require-SafeItem (Join-Path (Require-SafeItem $ObsDepsPrefix $true) 'bin') $true
$vcpkg = Require-SafeItem $VcpkgInstalledPrefix $true
$destination = Require-SafeItem $TestDirectory $true
$null = Find-File $destination 'gpu-image-processing-native.exe' $true
foreach ($root in @($build, $deps, $vcpkg)) {
    $rootPath = [IO.Path]::TrimEndingDirectorySeparator($root)
    $destPath = [IO.Path]::TrimEndingDirectorySeparator($destination)
    $separator = [IO.Path]::DirectorySeparatorChar
    $comparison = [StringComparison]::OrdinalIgnoreCase
    if ($destPath.Equals($rootPath, $comparison) -or
        $destPath.StartsWith($rootPath + $separator, $comparison) -or
        $rootPath.StartsWith($destPath + $separator, $comparison)) {
        throw 'Test directory and explicit runtime source roots must not overlap'
    }
}

$module = Find-BuiltFile $build 'libobs-d3d11.dll'
# Verify the pinned D3D11 target's actual PE imports. Unproven additions are an unmet gate.
$imports = & dumpbin /dependents $module
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect pinned D3D11 module imports' }
$importNames = @($imports | ForEach-Object {
    if ($_ -match '^\s*([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1].ToLowerInvariant() }
})
if ('obs.dll' -notin $importNames) { throw 'D3D11 module must import the pinned libobs' }
$systemImports = @('kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll', 'ole32.dll',
                  'shell32.dll', 'shcore.dll', 'd3d9.dll', 'd3d11.dll', 'dxgi.dll',
                  'd3dcompiler_47.dll', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
                  'ucrtbase.dll', 'ntdll.dll')
foreach ($name in $importNames) {
    if ($name -ne 'obs.dll' -and $name -notin $systemImports -and $name -notmatch '^api-ms-win-.*\.dll$') {
        throw "Unproven graphics module dependency: $name"
    }
    Write-Output "graphics-module-import=$name"
}

# Same OBS 0052d024 / hash-verified windows-deps-2026-07-15 closure as the adapter fixture.
$closure = @('avcodec-62.dll', 'avformat-62.dll', 'avutil-60.dll', 'librist.dll',
             'libx264-164.dll', 'srt.dll', 'swresample-6.dll', 'swscale-9.dll', 'zlib.dll')
$origins = @{}
$origins['libobs-d3d11.dll'] = $module
$origins['obs.dll'] = Find-BuiltFile $build 'obs.dll'
$origins['w32-pthreads.dll'] = Find-BuiltFile $build 'w32-pthreads.dll'
foreach ($name in $closure) { $origins[$name] = Find-File $deps $name $true }

# CMake already stages direct libobs/OpenCV runtime imports. Prove every such DLL;
# reject unexpected Windows ML/ORT or other runtimes rather than broaden the fixture.
foreach ($item in @(Get-ChildItem -LiteralPath $destination -Force -Filter '*.dll')) {
    $name = $item.Name.ToLowerInvariant()
    $null = Require-SafeItem $item.FullName $false
    if (-not $origins.ContainsKey($name)) {
        if ($name -notmatch '^opencv_(core|imgproc)[0-9]+\.dll$') {
            throw "Unexpected pre-staged graphics runtime: $name"
        }
        # Static triplets legitimately have no bin. Require/prove it only for actual DLL imports.
        $vcpkgBin = Require-SafeItem (Join-Path $vcpkg 'bin') $true
        $origins[$name] = Find-File $vcpkgBin $name $true
    }
}
$deployment = @()
foreach ($name in @($origins.Keys | Sort-Object)) {
    $source = $origins[$name]
    $hash = Get-Sha256 $source
    $existing = Find-File $destination $name $false
    if ($null -ne $existing -and (Get-Sha256 $existing) -cne $hash) {
        throw "Pre-staged DLL differs from its pinned origin: $name"
    }
    $target = if ($null -ne $existing) { $existing } else { Join-Path $destination $name }
    $deployment += [pscustomobject]@{ Name=$name; Source=$source; Destination=$target; Hash=$hash; Exists=($null -ne $existing) }
}
# Complete preflight precedes copies. Never overwrite collisions.
foreach ($entry in $deployment) {
    if (-not $entry.Exists) { [IO.File]::Copy($entry.Source, $entry.Destination, $false) }
    if ((Get-Sha256 $entry.Source) -cne $entry.Hash -or (Get-Sha256 $entry.Destination) -cne $entry.Hash) {
        throw "Graphics runtime origin changed during staging: $($entry.Name)"
    }
    Write-Output "$($entry.Name) origin=$($entry.Source) sha256=$($entry.Hash)"
}
