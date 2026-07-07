param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [Parameter(Mandatory = $true)]
    [string]$DatasetName,
    [Parameter(Mandatory = $true)]
    [string]$InputDir,
    [Parameter(Mandatory = $true)]
    [string]$DatasetRoot,
    [Parameter(Mandatory = $true)]
    [string]$SpatialError,
    [Parameter(Mandatory = $true)]
    [string]$ThresholdTag,
    [string]$TemporalError = "0",
    [string]$StatsExe = "",
    [int]$MaxFiles = 0,
    [int]$CompressionTimeoutSeconds = 30,
    [switch]$Resume
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Parse-TrajicResult {
    param([string]$Path)

    $lines = Get-Content -LiteralPath $Path
    if ($lines.Count -lt 2) {
        throw "Result file too short: $Path"
    }

    $timeParts = $lines[-2].Trim().Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
    $sizeParts = $lines[-1].Trim().Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
    if ($timeParts.Count -lt 2 -or $sizeParts.Count -lt 2) {
        throw "Failed to parse Trajic metrics: $Path"
    }

    [pscustomobject]@{
        point_count = [Math]::Max(0, $lines.Count - 2)
        compress_time_us = [int64]$timeParts[0]
        decompress_time_us = [int64]$timeParts[1]
        raw_size_bytes = [int64]$sizeParts[0]
        compressed_size_bytes = [int64]$sizeParts[1]
    }
}

function Invoke-TrajicStats {
    param(
        [string]$StatsExePath,
        [string]$InputPath,
        [string]$ResultPath,
        [string]$TemporalErrorValue,
        [string]$SpatialErrorValue,
        [int]$TimeoutSeconds
    )

    $proc = Start-Process -FilePath $StatsExePath `
        -ArgumentList @("trajic", $InputPath, $ResultPath, $TemporalErrorValue, $SpatialErrorValue) `
        -PassThru `
        -WindowStyle Hidden

    try {
        Wait-Process -Id $proc.Id -Timeout $TimeoutSeconds -ErrorAction Stop
    }
    catch {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        throw "timeout_after_${TimeoutSeconds}s"
    }

    $proc.Refresh()
    return $proc.ExitCode
}

if ([string]::IsNullOrWhiteSpace($StatsExe)) {
    $StatsExe = Join-Path $WorkspaceRoot "lossless\trajic\bin\stats.exe"
}

if (-not (Test-Path $StatsExe)) {
    throw "Trajic stats executable not found: $StatsExe"
}
if (-not (Test-Path $InputDir)) {
    throw "InputDir not found: $InputDir"
}

$resultDir = Join-Path $DatasetRoot $ThresholdTag
$resultsDir = Join-Path $resultDir "results"
Ensure-Directory $resultsDir
$summaryPath = Join-Path $resultDir "summary.csv"

if ($Resume -and (Test-Path $summaryPath)) {
    Write-Output "Skip completed Trajic $DatasetName $ThresholdTag because summary.csv already exists."
    return
}

$inputFiles = Get-ChildItem -Path $InputDir -File | Sort-Object Name
if ($MaxFiles -gt 0) {
    $inputFiles = $inputFiles | Select-Object -First $MaxFiles
}
if (($inputFiles | Measure-Object).Count -eq 0) {
    throw "No input files found in $InputDir"
}

$runInfoPath = Join-Path $resultDir "run_info.txt"
$fileRows = New-Object System.Collections.Generic.List[object]
$skippedRows = New-Object System.Collections.Generic.List[object]
$totalRawSize = [int64]0
$totalCompressedSize = [int64]0
$totalCompressTimeUs = [int64]0
$totalDecompressTimeUs = [int64]0
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($file in $inputFiles) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $resultPath = Join-Path $resultsDir ($baseName + ".trajic.txt")

    try {
        if (-not ($Resume -and (Test-Path $resultPath))) {
            $exitCode = Invoke-TrajicStats `
                -StatsExePath $StatsExe `
                -InputPath $file.FullName `
                -ResultPath $resultPath `
                -TemporalErrorValue $TemporalError `
                -SpatialErrorValue $SpatialError `
                -TimeoutSeconds $CompressionTimeoutSeconds
            if ($exitCode -ne 0) {
                throw "stats.exe exit code $exitCode"
            }
        }

        $metrics = Parse-TrajicResult -Path $resultPath
        $ratio = if ($metrics.raw_size_bytes -gt 0) {
            [double]$metrics.compressed_size_bytes / [double]$metrics.raw_size_bytes
        }
        else {
            0.0
        }

        $fileRows.Add([pscustomobject]@{
            trajectory_file = $file.Name
            result_file = [System.IO.Path]::GetFileName($resultPath)
            point_count = $metrics.point_count
            raw_size_bytes = $metrics.raw_size_bytes
            compressed_size_bytes = $metrics.compressed_size_bytes
            compression_ratio = [Math]::Round($ratio, 12)
            compress_time_us = $metrics.compress_time_us
            decompress_time_us = $metrics.decompress_time_us
        }) | Out-Null

        $totalRawSize += $metrics.raw_size_bytes
        $totalCompressedSize += $metrics.compressed_size_bytes
        $totalCompressTimeUs += $metrics.compress_time_us
        $totalDecompressTimeUs += $metrics.decompress_time_us
    }
    catch {
        if (Test-Path $resultPath) {
            Remove-Item -LiteralPath $resultPath -Force -ErrorAction SilentlyContinue
        }
        $skippedRows.Add([pscustomobject]@{
            trajectory_file = $file.Name
            reason = $_.Exception.Message.Trim()
        }) | Out-Null
    }
}

$stopwatch.Stop()

$fileResultsPath = Join-Path $resultDir "file_results.csv"
$fileRows | Export-Csv -Path $fileResultsPath -NoTypeInformation -Encoding UTF8

$skippedPath = Join-Path $resultDir "skipped.csv"
$skippedRows | Export-Csv -Path $skippedPath -NoTypeInformation -Encoding UTF8

$trajectoryCount = $fileRows.Count
$compressionRatio = if ($totalRawSize -gt 0) {
    [double]$totalCompressedSize / [double]$totalRawSize
}
else {
    0.0
}
$avgCompressUs = if ($trajectoryCount -gt 0) {
    [double]$totalCompressTimeUs / [double]$trajectoryCount
}
else {
    0.0
}
$avgDecompressUs = if ($trajectoryCount -gt 0) {
    [double]$totalDecompressTimeUs / [double]$trajectoryCount
}
else {
    0.0
}

$summary = [pscustomobject]@{
    dataset = $DatasetName
    spatial_error_threshold = $ThresholdTag
    temporal_error_threshold = $TemporalError
    trajectory_count = $trajectoryCount
    raw_size_bytes = $totalRawSize
    compressed_size_bytes = $totalCompressedSize
    compression_ratio = [Math]::Round($compressionRatio, 12)
    average_trajectory_compress_time_us = [Math]::Round($avgCompressUs, 6)
    average_trajectory_compress_time_ms = [Math]::Round(($avgCompressUs / 1000.0), 6)
    average_trajectory_decompress_time_us = [Math]::Round($avgDecompressUs, 6)
    average_trajectory_decompress_time_ms = [Math]::Round(($avgDecompressUs / 1000.0), 6)
    total_compress_time_us = $totalCompressTimeUs
    total_compress_time_ms = [Math]::Round(($totalCompressTimeUs / 1000.0), 6)
    total_decompress_time_us = $totalDecompressTimeUs
    total_decompress_time_ms = [Math]::Round(($totalDecompressTimeUs / 1000.0), 6)
    total_run_time_s = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
    skipped_count = $skippedRows.Count
}

@($summary) | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

@(
    "dataset=$DatasetName",
    "input_dir=$InputDir",
    "stats_exe=$StatsExe",
    "spatial_error=$SpatialError",
    "threshold_tag=$ThresholdTag",
    "temporal_error=$TemporalError",
    "compression_timeout_seconds=$CompressionTimeoutSeconds",
    "files_total=$($inputFiles.Count)",
    "files_succeeded=$trajectoryCount",
    "files_skipped=$($skippedRows.Count)",
    "resume=$Resume",
    "max_files=$MaxFiles"
) | Set-Content -Path $runInfoPath -Encoding UTF8

Write-Output "Finished Trajic $DatasetName $ThresholdTag -> $resultDir"
