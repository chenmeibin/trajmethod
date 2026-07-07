param(
    [string]$ExePath = "",
    [string]$InputDir,
    [string]$DataSource = "geolife",
    [string]$InputFormat = "simple",
    [string]$ThresholdMode = "meter",
    [double]$Threshold = 1.0,
    [double]$TargetCompressRatio,
    [string]$AValues = "0.5,1,2",
    [string]$BValues = "25,50,100",
    [string]$CValues = "0.6,0.8,1.1",
    [double]$Accuracy = 0.5,
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
    $ResultDir = Join-Path $defaultBuildDir ($DataSource + "_target_cr_search")
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
            $totalLength = 0
            $totalErrorCnt = 0
            $weightedCompress = 0.0
            $weightedMeanError = 0.0
            $globalMaxError = 0.0
            $meterThreshold = $null

            foreach ($file in $files) {
                $tempOutput = Join-Path $env:TEMP ("pilotc_target_cr_" + [guid]::NewGuid().ToString("N") + ".bin")
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
                    $args += @("--max-error-degree", $Threshold.ToString([System.Globalization.CultureInfo]::InvariantCulture))
                } else {
                    $args += @("--max-error", $Threshold.ToString([System.Globalization.CultureInfo]::InvariantCulture))
                }

                try {
                    $output = & $ExePath @args 2>&1
                    if ($LASTEXITCODE -ne 0) {
                        throw "Run failed for $($file.Name) with a=$a b=$b c=$c threshold=$Threshold`n$output"
                    }

                    $metrics = Parse-Metrics $output
                    if (-not $metrics.ContainsKey("length")) {
                        throw "Could not parse metrics for $($file.Name) with a=$a b=$b c=$c threshold=$Threshold"
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
                        $meterThreshold = $Threshold
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
            $meetsTarget = ($avgCompress -ge $TargetCompressRatio)
            $compressGap = $avgCompress - $TargetCompressRatio

            $rows.Add([pscustomobject]@{
                data_source = $DataSource
                input_format = $InputFormat
                threshold_mode = $ThresholdMode
                threshold_input = $Threshold
                threshold_meter = $meterThreshold
                target_compress_ratio = $TargetCompressRatio
                a = $a
                b = $b
                c = $c
                files = $files.Count
                total_points = $totalLength
                error_points = $totalErrorCnt
                overflow_ratio = [Math]::Round($overflowRatio, 8)
                average_compress_ratio_code = [Math]::Round($avgCompress, 8)
                average_compress_ratio_paper = [Math]::Round((Invert-CompressRatio $avgCompress), 8)
                compress_gap = [Math]::Round($compressGap, 8)
                average_mean_error = [Math]::Round($avgMeanError, 8)
                global_max_error = [Math]::Round($globalMaxError, 8)
                meets_target = $meetsTarget
            }) | Out-Null
        }
    }
}

$rows | Export-Csv -NoTypeInformation -Encoding UTF8 $OutputCsv

$rows |
    Sort-Object @{ Expression = "meets_target"; Descending = $true }, @{ Expression = "average_mean_error"; Descending = $false }, @{ Expression = "average_compress_ratio_code"; Descending = $false }, @{ Expression = "global_max_error"; Descending = $false } |
    Export-Csv -NoTypeInformation -Encoding UTF8 $BestCsv

$metaPath = Join-Path $ResultDir "search_config.txt"
$metaLines = @(
    "data_source=$DataSource",
    "input_format=$InputFormat",
    "input_dir=$InputDir",
    "threshold_mode=$ThresholdMode",
    "threshold=$Threshold",
    "target_compress_ratio=$TargetCompressRatio",
    "a_values=$AValues",
    "b_values=$BValues",
    "c_values=$CValues",
    "accuracy=$Accuracy",
    "max_files=$MaxFiles",
    "files_evaluated=$($files.Count)"
)
$metaLines | Set-Content -Encoding UTF8 $metaPath

Write-Output "Saved target-CR results to: $OutputCsv"
Write-Output "Saved target-CR ranking to: $BestCsv"
Write-Output "Saved search config to: $metaPath"
