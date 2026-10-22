param(
    [Parameter(Mandatory = $true)][ValidateSet('jak2', 'jak3')][string]$Game,
    [Parameter(Mandatory = $true)][string]$SourceData,
    [Parameter(Mandatory = $true)][string]$BuildData
)

# Read extracted inputs through junctions, but keep every build output separate.
# This never deletes files or overwrites existing generated FR3 assets. (AI-assisted)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $SourceData).Path.TrimEnd('\', '/')
$buildRoot = [IO.Path]::GetFullPath($BuildData).TrimEnd('\', '/')
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if ($buildRoot.Equals($sourceRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $buildRoot.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildData must be outside the original extracted data.'
}
foreach ($required in @("iso_data/$Game/buildinfo.json", "decompiler_out/$Game/raw_obj",
                         "out/$Game/fr3", 'custom_assets')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $required))) {
        throw "Missing extracted input: $required"
    }
}

function Ensure-RealDirectory([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        $entry = Get-Item -LiteralPath $Path
        if (-not $entry.PSIsContainer -or $entry.LinkType) {
            throw "Build output must be a real directory, not a link: $Path"
        }
    } else {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Ensure-InputLink([string]$Name, [string]$Target) {
    $linkPath = Join-Path $buildRoot $Name
    $expected = (Resolve-Path -LiteralPath $Target).Path
    if (Test-Path -LiteralPath $linkPath) {
        $entry = Get-Item -LiteralPath $linkPath
        if ($entry.LinkType -ne 'Junction' -or $entry.Target -ne $expected) {
            throw "Existing input link does not match; left untouched: $linkPath"
        }
    } else {
        New-Item -ItemType Junction -Path $linkPath -Target $expected | Out-Null
    }
}

Ensure-RealDirectory $buildRoot
foreach ($name in @('goal_src', 'game', 'decompiler')) {
    Ensure-InputLink $name (Join-Path $repoRoot $name)
}
foreach ($name in @('iso_data', 'decompiler_out', 'custom_assets')) {
    Ensure-InputLink $name (Join-Path $sourceRoot $name)
}
foreach ($name in @('out', "out/$Game", "out/$Game/fr3")) {
    Ensure-RealDirectory (Join-Path $buildRoot $name)
}
$fr3Output = Join-Path $buildRoot "out/$Game/fr3"
$copied = 0
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $sourceRoot "out/$Game/fr3") -File) {
    $destination = Join-Path $fr3Output $file.Name
    if (-not (Test-Path -LiteralPath $destination)) {
        Copy-Item -LiteralPath $file.FullName -Destination $destination
        $copied++
    }
}
Write-Output "$Game ARM64 workspace ready: $buildRoot; copied $copied missing FR3 files. (AI-assisted)"
