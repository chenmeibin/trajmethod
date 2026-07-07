param(
    [Parameter(Mandatory = $true)]
    [string]$InputDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [string]$Prefix = "dataset",
    [int]$Rep1Count = 30,
    [int]$Rep2Count = 30
)

$ErrorActionPreference = "Stop"

function Ensure-Dir {
    param([string]$Path)
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Get-SampledFiles {
    param(
        [object[]]$Items,
        [int]$TargetCount,
        [int]$Phase
    )
    if ($Items.Count -le $TargetCount) {
        return @($Items)
    }
    $picked = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $TargetCount; $i++) {
        $position = ($i + $Phase) % $Items.Count
        $index = [math]::Floor(($position * ($Items.Count - 1)) / [math]::Max(1, ($TargetCount - 1)))
        $picked.Add($Items[$index]) | Out-Null
    }
    return @($picked | Select-Object -Unique)
}

$files = Get-ChildItem -LiteralPath $InputDir -File | ForEach-Object {
    [pscustomobject]@{
        File = $_
        LineCount = (Get-Content -LiteralPath $_.FullName | Measure-Object -Line).Lines
    }
} | Where-Object { $_.LineCount -gt 1 } | Sort-Object LineCount, @{ Expression = { $_.File.Name } }

if ($files.Count -eq 0) {
    throw "No valid files found in $InputDir"
}

$rep1Dir = Join-Path $OutputRoot ($Prefix + "_rep1")
$rep2Dir = Join-Path $OutputRoot ($Prefix + "_rep2")
$unionDir = Join-Path $OutputRoot ($Prefix + "_union")
Ensure-Dir $rep1Dir
Ensure-Dir $rep2Dir
Ensure-Dir $unionDir

$rep1 = Get-SampledFiles -Items $files -TargetCount $Rep1Count -Phase 0
$remaining = $files | Where-Object { ($rep1.File.FullName -notcontains $_.File.FullName) }
if ($remaining.Count -lt $Rep2Count) {
    $remaining = $files
}
$rep2 = Get-SampledFiles -Items $remaining -TargetCount $Rep2Count -Phase 1

foreach ($item in $rep1) {
    Copy-Item -LiteralPath $item.File.FullName -Destination (Join-Path $rep1Dir $item.File.Name) -Force
    Copy-Item -LiteralPath $item.File.FullName -Destination (Join-Path $unionDir $item.File.Name) -Force
}
foreach ($item in $rep2) {
    Copy-Item -LiteralPath $item.File.FullName -Destination (Join-Path $rep2Dir $item.File.Name) -Force
    Copy-Item -LiteralPath $item.File.FullName -Destination (Join-Path $unionDir $item.File.Name) -Force
}

$meta = @(
    "input_dir=$InputDir",
    "rep1_count=$((Get-ChildItem -LiteralPath $rep1Dir -File).Count)",
    "rep2_count=$((Get-ChildItem -LiteralPath $rep2Dir -File).Count)",
    "union_count=$((Get-ChildItem -LiteralPath $unionDir -File).Count)"
)
$meta | Set-Content -Encoding UTF8 (Join-Path $OutputRoot ($Prefix + "_subsets.txt"))

Write-Output "Created subsets:"
Write-Output "  rep1=$rep1Dir"
Write-Output "  rep2=$rep2Dir"
Write-Output "  union=$unionDir"
