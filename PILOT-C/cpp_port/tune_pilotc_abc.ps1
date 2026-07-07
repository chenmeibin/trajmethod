param(
    [string]$ExePath = "",
    [string]$InputDir,
    [string]$DataSource = "geolife",
    [string]$InputFormat = "simple",
    [string]$ThresholdMode = "meter",
    [string]$Thresholds = "1,5,10",
    [string]$AValues = "0.5,1,2",
    [string]$BValues = "25,50,100",
    [string]$CValues = "0.6,0.8,1.1",
    [double]$Accuracy = 0.5,
    [double]$AllowedOverflowRatio = 0.0,
    [string]$ResultDir = "",
    [string]$OutputCsv = "",
    [string]$BestCsv = "",
    [int]$MaxFiles = 0
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultBuildDir = Join-Path $scriptRoot "build"

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $defaultBuildDir "pilotc_reference.exe"
}
if ([string]::IsNullOrWhiteSpace($ResultDir)) {
    $ResultDir = Join-Path $defaultBuildDir ($DataSource + "_abc_search")
}
if ([string]::IsNullOrWhiteSpace($OutputCsv)) {
    $OutputCsv = Join-Path $ResultDir "tune_results.csv"
}
if ([string]::IsNullOrWhiteSpace($BestCsv)) {
    $BestCsv = Join-Path $ResultDir "tune_best.csv"
}

New-Item -ItemType Directory -Force -Path $ResultDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputCsv) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $BestCsv) | Out-Null

function Parse-DoubleList {
    param([string]$Text)
    return $Text.Split(',') | ForEach-Object { [double]($_.Trim()) }
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
        elseif ($line -match '^meter_threshold=([0-9eE\+\-\.]+)$') { $metrics.meter_threshold = [double]$matches[1] }
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

$aList = Parse-DoubleList $AValues
$bList = Parse-DoubleList $BValues
$cList = Parse-DoubleList $CValues
$thresholdList = Parse-DoubleList $Thresholds

$files = Get-ChildItem -Path $InputDir -File | Sort-Object Name
if ($MaxFiles -gt 0) {
    $files = $files | Select-Object -First $MaxFiles
}
if ($files.Count -eq 0) {
    throw "No input files found in $InputDir"
}

$rows = New-Object System.Collections.Generic.List[object]

foreach ($a in $aList) {
    foreach ($b in $bList) {
        foreach ($c in $cList) {
            foreach ($threshold in $thresholdList) {
                $totalLength = 0
                $totalErrorCnt = 0
                $weightedCompress = 0.0
                $weightedMeanError = 0.0
                $globalMaxError = 0.0
                $meterThreshold = $null

                foreach ($file in $files) {
                    $tempOutput = Join-Path $env:TEMP ("pilotc_tune_" + [guid]::NewGuid().ToString("N") + ".bin")
                    $args = @(
                        "--input", $file.FullName,
                        "--input-format", $InputFormat,
                        "--data-source", $DataSource,
                        "--a", $a.ToString([System.Globalization.CultureInfo]::InvariantCulture),
                        "--b", $b.ToString([System.Globalization.CultureInfo]::InvariantCulture),
                        "--c", $c.ToString([System.Globalization.CultureInfo]::InvariantCulture),
                        "--accuracy", $Accuracy.ToString([System.Globalization.CultureInfo]::InvariantCulture),
                        "--output", $tempOutput
                    )

                    if ($ThresholdMode -eq "degree") {
                        $args += @("--max-error-degree", $threshold.ToString([System.Globalization.CultureInfo]::InvariantCulture))
                    } else {
                        $args += @("--max-error", $threshold.ToString([System.Globalization.CultureInfo]::InvariantCulture))
                    }

                    try {
                        $output = & $ExePath @args 2>&1
                        if ($LASTEXITCODE -ne 0) {
                            throw "Run failed for $($file.Name) with a=$a b=$b c=$c threshold=$threshold`n$output"
                        }

                        $metrics = Parse-Metrics $output
                        if (-not $metrics.ContainsKey("length")) {
                            throw "Could not parse metrics for $($file.Name) with a=$a b=$b c=$c threshold=$threshold"
                        }

                        $length = [int]$metrics.length
                        $totalLength += $length
                        $totalErrorCnt += [int]$metrics.error_cnt
                        $weightedCompress += [double]$metrics.compress_ratio * $length
                        $weightedMeanError += [double]$metrics.mean_error * $length
                        $globalMaxError = [Math]::Max($globalMaxError, [double]$metrics.max_error)

                        if ($metrics.ContainsKey("meter_threshold")) {
                            $meterThreshold = [double]$metrics.meter_threshold
                        } elseif ($ThresholdMode -eq "meter") {
                            $meterThreshold = $threshold
                        }
                    }
                    finally {
                        if (Test-Path $tempOutput) {
                            Remove-Item -Force -LiteralPath $tempOutput
                        }
                    }
                }

                $avgCompress = if ($totalLength -gt 0) { $weightedCompress / $totalLength } else { 0.0 }
                $avgMeanError = if ($totalLength -gt 0) { $weightedMeanError / $totalLength } else { 0.0 }
                $overflowRatio = if ($totalLength -gt 0) { $totalErrorCnt / $totalLength } else { 1.0 }
                $isValid = ($overflowRatio -le $AllowedOverflowRatio)

                $rows.Add([pscustomobject]@{
                    data_source = $DataSource
                    input_format = $InputFormat
                    threshold_mode = $ThresholdMode
                    threshold_input = $threshold
                    threshold_meter = $meterThreshold
                    a = $a
                    b = $b
                    c = $c
                    files = $files.Count
                    total_points = $totalLength
                    error_points = $totalErrorCnt
                    overflow_ratio = [Math]::Round($overflowRatio, 8)
                    average_compress_ratio_code = [Math]::Round($avgCompress, 8)
                    average_compress_ratio_paper = [Math]::Round((Invert-CompressRatio $avgCompress), 8)
                    average_mean_error = [Math]::Round($avgMeanError, 8)
                    global_max_error = [Math]::Round($globalMaxError, 8)
                    valid = $isValid
                }) | Out-Null
            }
        }
    }
}

$rows | Export-Csv -NoTypeInformation -Encoding UTF8 $OutputCsv

$grouped = $rows | Group-Object a, b, c
$bestRows = New-Object System.Collections.Generic.List[object]

foreach ($group in $grouped) {
    $items = $group.Group
    $allValid = ($items | Where-Object { -not $_.valid }).Count -eq 0
    $avgCompress = ($items | Measure-Object average_compress_ratio_code -Average).Average
    $avgMeanError = ($items | Measure-Object average_mean_error -Average).Average
    $worstOverflow = ($items | Measure-Object overflow_ratio -Maximum).Maximum
    $worstMaxError = ($items | Measure-Object global_max_error -Maximum).Maximum
    $bestRows.Add([pscustomobject]@{
        data_source = $DataSource
        a = [double]$items[0].a
        b = [double]$items[0].b
        c = [double]$items[0].c
        thresholds_tested = $items.Count
        all_thresholds_valid = $allValid
        average_compress_ratio_code = [Math]::Round($avgCompress, 8)
        average_compress_ratio_paper = [Math]::Round((Invert-CompressRatio $avgCompress), 8)
        average_mean_error = [Math]::Round($avgMeanError, 8)
        worst_overflow_ratio = [Math]::Round($worstOverflow, 8)
        worst_max_error = [Math]::Round($worstMaxError, 8)
    }) | Out-Null
}

$bestRows |
    Sort-Object @{ Expression = "all_thresholds_valid"; Descending = $true }, @{ Expression = "average_compress_ratio_code"; Descending = $true }, @{ Expression = "average_mean_error"; Descending = $false } |
    Export-Csv -NoTypeInformation -Encoding UTF8 $BestCsv

$metaPath = Join-Path $ResultDir "search_config.txt"
$metaLines = @(
    "data_source=$DataSource",
    "input_format=$InputFormat",
    "input_dir=$InputDir",
    "threshold_mode=$ThresholdMode",
    "thresholds=$Thresholds",
    "a_values=$AValues",
    "b_values=$BValues",
    "c_values=$CValues",
    "accuracy=$Accuracy",
    "allowed_overflow_ratio=$AllowedOverflowRatio",
    "max_files=$MaxFiles",
    "files_evaluated=$($files.Count)"
)
$metaLines | Set-Content -Encoding UTF8 $metaPath

Write-Output "Saved threshold-level results to: $OutputCsv"
Write-Output "Saved grouped ranking to: $BestCsv"
Write-Output "Saved search config to: $metaPath"
