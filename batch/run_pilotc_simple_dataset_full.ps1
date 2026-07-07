param(
    [string]$ExePath = "",
    [Parameter(Mandatory = $true)]
    [string]$DatasetName,
    [Parameter(Mandatory = $true)]
    [string]$InputDir,
    [Parameter(Mandatory = $true)]
    [string]$DatasetRoot,
    [string]$DataSource = "geolife",
    [string]$Thresholds = "1e-5,5e-5,1e-4,5e-4,1e-3",
    [double]$MetersPerDegree = 111194.93,
    [double]$A,
    [double]$B,
    [double]$C,
    [double]$Accuracy = 0.5,
    [int]$MaxFiles = 0
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultBuildDir = Join-Path $scriptRoot "..\PILOT-C\cpp_port\build"

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $defaultBuildDir "pilotc_reference.exe"
}

function Parse-ThresholdList {
    param([string]$Text)

    $items = New-Object System.Collections.Generic.List[object]
    foreach ($token in $Text.Split(',')) {
        $tag = $token.Trim()
        if ([string]::IsNullOrWhiteSpace($tag)) {
            continue
        }
        $degreeValue = [double]::Parse($tag, [System.Globalization.CultureInfo]::InvariantCulture)
        $items.Add([pscustomobject]@{
            tag = $tag
            degree_value = $degreeValue
            meter_value = $degreeValue * $MetersPerDegree
        }) | Out-Null
    }
    return $items
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

if (-not (Test-Path $ExePath)) {
    throw "Executable not found: $ExePath"
}
if (-not (Test-Path $InputDir)) {
    throw "InputDir not found: $InputDir"
}

$thresholdList = Parse-ThresholdList $Thresholds
$files = Get-ChildItem -Path $InputDir -File | Sort-Object Name
if ($MaxFiles -gt 0) {
    $files = $files | Select-Object -First $MaxFiles
}
if (($files | Measure-Object).Count -eq 0) {
    throw "No input files found in $InputDir"
}

New-Item -ItemType Directory -Force -Path $DatasetRoot | Out-Null

foreach ($threshold in $thresholdList) {
    $thresholdStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $resultDir = Join-Path $DatasetRoot $threshold.tag
    New-Item -ItemType Directory -Force -Path $resultDir | Out-Null
    $summaryPath = Join-Path $resultDir "summary.csv"

    if (Test-Path $summaryPath) {
        Write-Output ("Skip completed threshold " + $threshold.tag + " because summary.csv already exists.")
        continue
    }

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

    foreach ($file in $files) {
        $lineCount = (Get-Content -LiteralPath $file.FullName | Measure-Object -Line).Lines
        if ($lineCount -le 1) {
            $skippedRows.Add([pscustomobject]@{
                file_name = $file.Name
                line_count = $lineCount
                reason = "empty_or_too_short"
            }) | Out-Null
            continue
        }

        $tempOutput = Join-Path $env:TEMP ("pilotc_skwcl_" + [guid]::NewGuid().ToString("N") + ".bin")
        $args = @(
            "--input", $file.FullName,
            "--input-format", "simple",
            "--data-source", $DataSource,
            "--max-error", $threshold.meter_value.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--a", $A.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--b", $B.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--c", $C.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--accuracy", $Accuracy.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "--output", $tempOutput
        )

        try {
            $output = & $ExePath @args 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw ($output | Out-String)
            }

            $metrics = Parse-Metrics $output
            if (-not $metrics.ContainsKey("length")) {
                throw "Could not parse PILOT-C metrics output."
            }

            $length = [int]$metrics.length
            $rows.Add([pscustomobject]@{
                file_name = $file.Name
                threshold_degree = $threshold.degree_value
                threshold_label = $threshold.tag
                threshold_meter = [Math]::Round($threshold.meter_value, 6)
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
            }) | Out-Null

            $totalLength += $length
            $totalErrorCnt += [int]$metrics.error_cnt
            $weightedCompress += [double]$metrics.compress_ratio * $length
            $weightedMeanError += [double]$metrics.mean_error * $length
            $weightedVarError += [double]$metrics.var_error * $length
            $totalCompressTimeMs += [double]$metrics.compress_time_ms
            $totalDecompressTimeMs += [double]$metrics.decompress_time_ms
            $totalPipelineTimeMs += [double]$metrics.pipeline_time_ms
            $globalMaxError = [Math]::Max($globalMaxError, [double]$metrics.max_error)
        }
        catch {
            $skippedRows.Add([pscustomobject]@{
                file_name = $file.Name
                line_count = $lineCount
                reason = "runtime_error"
                detail = $_.Exception.Message.Trim()
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
        dataset = $DatasetName
        threshold_degree = $threshold.degree_value
        threshold_label = $threshold.tag
        threshold_meter = [Math]::Round($threshold.meter_value, 6)
        a = $A
        b = $B
        c = $C
        accuracy = $Accuracy
        files_total = $files.Count
        files_processed = $rows.Count
        files_skipped = $skippedRows.Count
        total_points = $totalLength
        error_points = $totalErrorCnt
        overflow_ratio = if ($totalLength -gt 0) { [Math]::Round(($totalErrorCnt / $totalLength), 8) } else { 0.0 }
        average_compress_time_ms = if ($rows.Count -gt 0) { [Math]::Round(($totalCompressTimeMs / $rows.Count), 4) } else { 0.0 }
        average_decompress_time_ms = if ($rows.Count -gt 0) { [Math]::Round(($totalDecompressTimeMs / $rows.Count), 4) } else { 0.0 }
        average_pipeline_time_ms = if ($rows.Count -gt 0) { [Math]::Round(($totalPipelineTimeMs / $rows.Count), 4) } else { 0.0 }
        total_runtime_s = [Math]::Round(($totalPipelineTimeMs / 1000.0), 4)
        outer_wall_time_s = [Math]::Round($thresholdStopwatch.Elapsed.TotalSeconds, 4)
        average_compress_ratio_code = if ($totalLength -gt 0) { [Math]::Round(($weightedCompress / $totalLength), 8) } else { 0.0 }
        average_compress_ratio_paper = if ($totalLength -gt 0) { [Math]::Round((Invert-CompressRatio ($weightedCompress / $totalLength)), 8) } else { 0.0 }
        average_mean_error = if ($totalLength -gt 0) { [Math]::Round(($weightedMeanError / $totalLength), 8) } else { 0.0 }
        average_var_error = if ($totalLength -gt 0) { [Math]::Round(($weightedVarError / $totalLength), 8) } else { 0.0 }
        global_max_error = [Math]::Round($globalMaxError, 8)
    }
    @($summary) | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath

    @(
        "dataset=$DatasetName",
        "input_dir=$InputDir",
        "input_format=simple",
        "data_source=$DataSource",
        "threshold_degree=$($threshold.degree_value)",
        "threshold_label=$($threshold.tag)",
        "threshold_meter=$([Math]::Round($threshold.meter_value, 6))",
        "a=$A",
        "b=$B",
        "c=$C",
        "accuracy=$Accuracy",
        "files_total=$($files.Count)",
        "files_processed=$($rows.Count)",
        "files_skipped=$($skippedRows.Count)"
    ) | Set-Content -Path (Join-Path $resultDir "config.txt") -Encoding UTF8

    @(
        "dataset=$DatasetName",
        "threshold=$($threshold.tag)",
        "exe_path=$ExePath",
        "input_dir=$InputDir"
    ) | Set-Content -Path (Join-Path $resultDir "run_info.txt") -Encoding UTF8

    Write-Output ("Finished threshold " + $threshold.tag + " -> " + $resultDir)
}
