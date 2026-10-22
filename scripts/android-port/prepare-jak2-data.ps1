param(
    [Parameter(Mandatory = $true)][string]$SourceData,
    [Parameter(Mandatory = $true)][string]$BuildData
)
# Preserve the existing Jak 2 preparation command. (AI-assisted)
& "$PSScriptRoot/prepare-game-data.ps1" -Game jak2 -SourceData $SourceData -BuildData $BuildData
