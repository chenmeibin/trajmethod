param(
    [string]$TargetRoot,
    [string[]]$SourceRoots,
    [string]$OutputSuffix = "exclude_gt60_timeout"
)

$ErrorActionPreference = "Stop"

function Get-SkippedFilesFromRunInfo {
    param([string]$RunInfoPath)

    if (-not (Test-Path $RunInfoPath)) {
        return @()
    }

    $line = (Get-Content $RunInfoPath | Where-Object { $_ -like 'skipped_timeout_list=*' } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($line)) {
        return @()
    }

    $raw = $line.Substring('skipped_timeout_list='.Length)
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return @()
    }

    return ($raw -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Round-Or-Zero {
    param(
        [double]$Value,
        [int]$Digits = 6
    )

    if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value)) {
        return 0
    }

    return [Math]::Round($Value, $Digits)
}

if (-not (Test-Path $TargetRoot)) {
    throw "Target root not found: $TargetRoot"
}

$summaryPath = Join-Path $TargetRoot "summary.csv"
$metricsPath = Join-Path $TargetRoot "metrics_summary.csv"
$runInfoPath = Join-Path $TargetRoot "run_info.txt"

if (-not (Test-Path $summaryPath)) {
    throw "Missing summary.csv in $TargetRoot"
}

$excludedSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($sourceRoot in $SourceRoots) {
    $sourceRunInfo = Join-Path $sourceRoot "run_info.txt"
    foreach ($item in Get-SkippedFilesFromRunInfo -RunInfoPath $sourceRunInfo) {
        [void]$excludedSet.Add($item)
    }
}

$summaryRows = Import-Csv $summaryPath
$filteredRows = New-Object System.Collections.Generic.List[object]
foreach ($row in $summaryRows) {
    if (-not $excludedSet.Contains($row.relative_file)) {
        $filteredRows.Add($row) | Out-Null
    }
}

$inputTotals = ($filteredRows | Measure-Object -Property input_points -Sum).Sum
$outputTotals = ($filteredRows | Measure-Object -Property output_points -Sum).Sum
$runtimeTotals = ($filteredRows | Measure-Object -Property runtime_seconds -Sum).Sum
$fileCompressionAverage = ($filteredRows | Measure-Object -Property compression_ratio -Average).Average
$fileRuntimeAverage = ($filteredRows | Measure-Object -Property runtime_seconds -Average).Average

if ($null -eq $inputTotals) { $inputTotals = 0 }
if ($null -eq $outputTotals) { $outputTotals = 0 }
if ($null -eq $runtimeTotals) { $runtimeTotals = 0 }
if ($null -eq $fileCompressionAverage) { $fileCompressionAverage = 0 }
if ($null -eq $fileRuntimeAverage) { $fileRuntimeAverage = 0 }

$overallCompressionRatio = if ($inputTotals -gt 0) {
    Round-Or-Zero -Value ($outputTotals / $inputTotals)
}
else {
    0
}
$savedRatio = if ($inputTotals -gt 0) {
    Round-Or-Zero -Value (1 - ($outputTotals / $inputTotals))
}
else {
    0
}
$pointsPerSecond = if ($runtimeTotals -gt 0) {
    Round-Or-Zero -Value ($inputTotals / $runtimeTotals) -Digits 3
}
else {
    0
}
$outputPointsPerSecond = if ($runtimeTotals -gt 0) {
    Round-Or-Zero -Value ($outputTotals / $runtimeTotals) -Digits 3
}
else {
    0
}

$filteredSummaryPath = Join-Path $TargetRoot ("summary_{0}.csv" -f $OutputSuffix)
$filteredMetricsPath = Join-Path $TargetRoot ("metrics_summary_{0}.csv" -f $OutputSuffix)
$filteredRunInfoPath = Join-Path $TargetRoot ("run_info_{0}.txt" -f $OutputSuffix)
$excludedListPath = Join-Path $TargetRoot ("excluded_files_{0}.txt" -f $OutputSuffix)

$filteredRows | Export-Csv -Path $filteredSummaryPath -NoTypeInformation -Encoding UTF8

$dataset = if ($filteredRows.Count -gt 0) { $filteredRows[0].dataset } else { "" }
$epsilon = if ($filteredRows.Count -gt 0) { $filteredRows[0].epsilon } else { "" }
$algorithm = if ($filteredRows.Count -gt 0) { $filteredRows[0].algorithm } else { "OPW-TR" }

[pscustomobject]@{
    algorithm = $algorithm
    dataset = $dataset
    epsilon = $epsilon
    processed_files = $filteredRows.Count
    total_input_points = [int64]$inputTotals
    total_output_points = [int64]$outputTotals
    total_runtime_seconds = Round-Or-Zero -Value $runtimeTotals
    overall_compression_ratio = $overallCompressionRatio
    saved_ratio = $savedRatio
    avg_file_compression_ratio = Round-Or-Zero -Value $fileCompressionAverage
    avg_file_runtime_seconds = Round-Or-Zero -Value $fileRuntimeAverage
    input_points_per_second = $pointsPerSecond
    output_points_per_second = $outputPointsPerSecond
    excluded_files = $excludedSet.Count
    filter_note = "Filtered 1e-3 summary by excluding trajectories that timed out in selected source thresholds"
} | Export-Csv -Path $filteredMetricsPath -NoTypeInformation -Encoding UTF8

@(
    ("algorithm={0}" -f $algorithm)
    ("dataset={0}" -f $dataset)
    ("epsilon={0}" -f $epsilon)
    ("processed_files={0}" -f $filteredRows.Count)
    ("total_input_points={0}" -f [int64]$inputTotals)
    ("total_output_points={0}" -f [int64]$outputTotals)
    ("overall_compression_ratio={0}" -f $overallCompressionRatio)
    ("saved_ratio={0}" -f $savedRatio)
    ("total_runtime_seconds={0}" -f (Round-Or-Zero -Value $runtimeTotals))
    ("avg_file_compression_ratio={0}" -f (Round-Or-Zero -Value $fileCompressionAverage))
    ("avg_file_runtime_seconds={0}" -f (Round-Or-Zero -Value $fileRuntimeAverage))
    ("input_points_per_second={0}" -f $pointsPerSecond)
    ("output_points_per_second={0}" -f $outputPointsPerSecond)
    ("excluded_files={0}" -f $excludedSet.Count)
    ("summary_csv={0}" -f $filteredSummaryPath)
    ("metrics_csv={0}" -f $filteredMetricsPath)
    ("excluded_list={0}" -f $excludedListPath)
) | Set-Content -Path $filteredRunInfoPath

$excludedSet | Sort-Object | Set-Content -Path $excludedListPath

[pscustomobject]@{
    target_root = $TargetRoot
    excluded_files = $excludedSet.Count
    filtered_summary = $filteredSummaryPath
    filtered_metrics = $filteredMetricsPath
    filtered_run_info = $filteredRunInfoPath
} | Format-List
