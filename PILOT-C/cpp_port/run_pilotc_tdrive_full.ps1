param(
    [string]$ExePath = "",
    [string]$InputDir = "D:\all_traj_method\data\T-drive",
    [string]$RecordsRoot = "",
    [string]$Thresholds = "1e-5,5e-5,1e-4,3e-4,5e-4,1e-3",
    [double]$A = 0.75,
    [double]$B = 10,
    [double]$C = 0.6,
    [double]$Accuracy = 0.5,
    [int]$MaxFiles = 0
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultBuildDir = Join-Path $scriptRoot "build"

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $defaultBuildDir "pilotc_reference.exe"
}
if ([string]::IsNullOrWhiteSpace($RecordsRoot)) {
    $RecordsRoot = Join-Path $defaultBuildDir "records"
}

if (-not (Test-Path $ExePath)) {
    throw "Executable not found: $ExePath"
}
if (-not (Test-Path $InputDir)) {
    throw "InputDir not found: $InputDir"
}

function Parse-DoubleList {
    param([string]$Text)
    return $Text.Split(',') | ForEach-Object { [double]($_.Trim()) }
}

function Format-ThresholdLabel {
    param([double]$Value)
    return $Value.ToString("0.###############e+0", [System.Globalization.CultureInfo]::InvariantCulture).Replace("e+0", "e").Replace("e-0", "e-")
}

function Parse-Metrics {
    param([string[]]$Lines)
    $metrics = @{}
    foreach ($line in $Lines) {
        if ($line -match '"max_error":\s*([0-9eE\+\-\.]+)') { $metrics.max_error = [double]$matches[1] }
        elseif ($line -match '"mean_error":\s*([0-9eE\+\-\.]+)') { $metrics.mean_error = [double]$matches[1] }
        elseif ($line -match '"compress_ratio":\s*([0-9eE\+\-\.]+)') { $metrics.compress_ratio = [double]$matches[1] }
        elseif ($line -match '"length":\s*([0-9eE\+\-\.]+)') { $metrics.length = [int]$matches[1] }
        elseif ($line -match '"error_cnt":\s*([0-9eE\+\-\.]+)') { $metrics.error_cnt = [int]$matches[1] }
        elseif ($line -match '"var_error":\s*([0-9eE\+\-\.]+)') { $metrics.var_error = [double]$matches[1] }
        elseif ($line -match '"compress_time_ms":\s*([0-9eE\+\-\.]+)') { $metrics.compress_time_ms = [double]$matches[1] }
        elseif ($line -match '"decompress_time_ms":\s*([0-9eE\+\-\.]+)') { $metrics.decompress_time_ms = [double]$matches[1] }
        elseif ($line -match '"pipeline_time_ms":\s*([0-9eE\+\-\.]+)') { $metrics.pipeline_time_ms = [double]$matches[1] }
        elseif ($line -match '^meter_threshold=([0-9eE\+\-\.]+)$') { $metrics.meter_threshold = [double]$matches[1] }
        elseif ($line -match '^meters_per_degree_latitude=([0-9eE\+\-\.]+)$') { $metrics.meters_per_degree_latitude = [double]$matches[1] }
        elseif ($line -match '^meters_per_degree_longitude=([0-9eE\+\-\.]+)$') { $metrics.meters_per_degree_longitude = [double]$matches[1] }
        elseif ($line -match '^reference_latitude_degrees=([0-9eE\+\-\.]+)$') { $metrics.reference_latitude_degrees = [double]$matches[1] }
    }
    return $metrics
}

function Invert-CompressRatio {
    param([double]$Value)
    if ($Value -eq 0) {
        return 0.0
    }
    return 1.0 / $Value
}

$thresholdList = Parse-DoubleList $Thresholds
$files = Get-ChildItem -Path $InputDir -File | Sort-Object Name
if ($MaxFiles -gt 0) {
    $files = $files | Select-Object -First $MaxFiles
}
if ($files.Count -eq 0) {
    throw "No input files found in $InputDir"
}

New-Item -ItemType Directory -Force -Path $RecordsRoot | Out-Null
$overallSummary = New-Object System.Collections.Generic.List[object]
$fileInfos = foreach ($file in $files) {
    $lineCount = (Get-Content $file.FullName | Measure-Object -Line).Lines
    [pscustomobject]@{
        File = $file
        LineCount = $lineCount
    }
}
$processableInfos = $fileInfos | Where-Object { $_.LineCount -gt 0 }
$skippedInfos = $fileInfos | Where-Object { $_.LineCount -le 0 }

foreach ($threshold in $thresholdList) {
    $thresholdStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $label = Format-ThresholdLabel $threshold
    $resultDir = Join-Path $RecordsRoot ("t_drive_" + $label)
    New-Item -ItemType Directory -Force -Path $resultDir | Out-Null

    $rows = New-Object System.Collections.Generic.List[object]
    $skippedRows = New-Object System.Collections.Generic.List[object]
    $totalLength = 0
    $totalErrorCnt = 0
    $weightedCompress = 0.0
    $weightedMeanError = 0.0
    $weightedVarError = 0.0
    $totalCompressTimeMs = 0.0
    $totalDecompressTimeMs = 0.0
    $totalPipelineTimeMs = 0.0
    $globalMaxError = 0.0
    $meterThreshold = $null

    foreach ($info in $skippedInfos) {
        $skippedRows.Add([pscustomobject]@{
            file_name = $info.File.Name
            line_count = $info.LineCount
            reason = "empty_trajectory"
        }) | Out-Null
    }

    foreach ($info in $processableInfos) {
        $file = $info.File
        $tempOutput = Join-Path $env:TEMP ("pilotc_full_" + [guid]::NewGuid().ToString("N") + ".bin")
        $args = @(
            "--input", $file.FullName,
            "--input-format", "tdrive",
            "--data-source", "geolife",
            "--max-error-degree", $threshold.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--a", $A.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--b", $B.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--c", $C.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--accuracy", $Accuracy.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--output", $tempOutput
        )

        try {
            $fileStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
            $output = & $ExePath @args 2>&1
            $fileStopwatch.Stop()
            if ($LASTEXITCODE -ne 0) {
                throw ($output | Out-String)
            }

            $metrics = Parse-Metrics $output
            if (-not $metrics.ContainsKey("length")) {
                throw "Could not parse metrics output."
            }

            $length = [int]$metrics.length
            $row = [pscustomobject]@{
                file_name = $file.Name
                threshold_degree = $threshold
                threshold_meter = $metrics.meter_threshold
                a = $A
                b = $B
                c = $C
                accuracy = $Accuracy
                length = $length
                error_cnt = [int]$metrics.error_cnt
                overflow_ratio = if ($length -gt 0) { [Math]::Round(([int]$metrics.error_cnt / $length), 8) } else { 0.0 }
                compress_time_ms = [Math]::Round([double]$metrics.compress_time_ms, 4)
                decompress_time_ms = [Math]::Round([double]$metrics.decompress_time_ms, 4)
                pipeline_time_ms = [Math]::Round([double]$metrics.pipeline_time_ms, 4)
                compress_ratio_code = [Math]::Round([double]$metrics.compress_ratio, 8)
                compress_ratio_paper = [Math]::Round((Invert-CompressRatio ([double]$metrics.compress_ratio)), 8)
                mean_error = [Math]::Round([double]$metrics.mean_error, 8)
                max_error = [Math]::Round([double]$metrics.max_error, 8)
                var_error = [Math]::Round([double]$metrics.var_error, 8)
            }
            $rows.Add($row) | Out-Null

            $totalLength += $length
            $totalErrorCnt += [int]$metrics.error_cnt
            $weightedCompress += [double]$metrics.compress_ratio * $length
            $weightedMeanError += [double]$metrics.mean_error * $length
            $weightedVarError += [double]$metrics.var_error * $length
            $totalCompressTimeMs += [double]$metrics.compress_time_ms
            $totalDecompressTimeMs += [double]$metrics.decompress_time_ms
            $totalPipelineTimeMs += [double]$metrics.pipeline_time_ms
            $globalMaxError = [Math]::Max($globalMaxError, [double]$metrics.max_error)
            $meterThreshold = [double]$metrics.meter_threshold
        }
        catch {
            $message = $_.Exception.Message.Trim()
            $skippedRows.Add([pscustomobject]@{
                file_name = $file.Name
                line_count = $info.LineCount
                reason = "runtime_error"
                detail = $message
            }) | Out-Null
        }
        finally {
            if (Test-Path $tempOutput) {
                Remove-Item -Force -LiteralPath $tempOutput
            }
        }
    }

    $detailCsv = Join-Path $resultDir "detail.csv"
    $rows | Export-Csv -NoTypeInformation -Encoding UTF8 $detailCsv
    $skippedCsv = Join-Path $resultDir "skipped.csv"
    $skippedRows | Export-Csv -NoTypeInformation -Encoding UTF8 $skippedCsv

    $thresholdStopwatch.Stop()
    $summary = [pscustomobject]@{
        dataset = "T-drive"
        threshold_degree = $threshold
        threshold_label = $label
        threshold_meter = $meterThreshold
        a = $A
        b = $B
        c = $C
        accuracy = $Accuracy
        files_total = $files.Count
        files_processed = $processableInfos.Count
        files_skipped = $skippedInfos.Count
        total_points = $totalLength
        error_points = $totalErrorCnt
        overflow_ratio = if ($totalLength -gt 0) { [Math]::Round(($totalErrorCnt / $totalLength), 8) } else { 0.0 }
        average_compress_time_ms = if ($processableInfos.Count -gt 0) { [Math]::Round(($totalCompressTimeMs / $processableInfos.Count), 4) } else { 0.0 }
        average_decompress_time_ms = if ($processableInfos.Count -gt 0) { [Math]::Round(($totalDecompressTimeMs / $processableInfos.Count), 4) } else { 0.0 }
        average_pipeline_time_ms = if ($processableInfos.Count -gt 0) { [Math]::Round(($totalPipelineTimeMs / $processableInfos.Count), 4) } else { 0.0 }
        total_runtime_s = [Math]::Round(($totalPipelineTimeMs / 1000.0), 4)
        outer_wall_time_s = [Math]::Round($thresholdStopwatch.Elapsed.TotalSeconds, 4)
        average_compress_ratio_code = if ($totalLength -gt 0) { [Math]::Round(($weightedCompress / $totalLength), 8) } else { 0.0 }
        average_compress_ratio_paper = if ($totalLength -gt 0) { [Math]::Round((Invert-CompressRatio ($weightedCompress / $totalLength)), 8) } else { 0.0 }
        average_mean_error = if ($totalLength -gt 0) { [Math]::Round(($weightedMeanError / $totalLength), 8) } else { 0.0 }
        average_var_error = if ($totalLength -gt 0) { [Math]::Round(($weightedVarError / $totalLength), 8) } else { 0.0 }
        global_max_error = [Math]::Round($globalMaxError, 8)
    }
    $summaryPath = Join-Path $resultDir "summary.csv"
    @($summary) | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath

    $metaPath = Join-Path $resultDir "config.txt"
    @(
        "dataset=T-drive",
        "input_dir=$InputDir",
        "input_format=tdrive",
        "data_source=geolife",
        "threshold_degree=$threshold",
        "threshold_meter=$meterThreshold",
        "a=$A",
        "b=$B",
        "c=$C",
        "accuracy=$Accuracy",
        "files_total=$($files.Count)",
        "files_processed=$($processableInfos.Count)",
        "files_skipped=$($skippedInfos.Count)"
    ) | Set-Content -Encoding UTF8 $metaPath

    $overallSummary.Add($summary) | Out-Null
    Write-Output ("Finished threshold " + $label + " -> " + $resultDir)
}

$overallPath = Join-Path $RecordsRoot "t_drive_full_summary.csv"
$overallSummary | Export-Csv -NoTypeInformation -Encoding UTF8 $overallPath
Write-Output ("Saved overall summary to: " + $overallPath)
