param(
    [string]$InputDir = "D:\all_traj_method\data\TD-TR_wuxi_converted",
    [string]$ResultsDir = "D:\all_traj_method\data\TD-TR_wuxi_1e-5_results",
    [double]$Epsilon = 1e-5,
    [string]$EpsilonLabel = "1e-5",
    [int]$MaxNewResults = 0
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceFile = Join-Path $scriptDir "TD-TR.cpp"
$exeFile = Join-Path $scriptDir "TD-TR.exe"
$summaryFile = Join-Path $ResultsDir "summary.csv"

if (-not (Test-Path $InputDir)) {
    throw "Converted Wuxi input directory not found: $InputDir"
}

New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

$needsCompile = -not (Test-Path $exeFile)
if (-not $needsCompile) {
    $needsCompile = (Get-Item $sourceFile).LastWriteTimeUtc -gt (Get-Item $exeFile).LastWriteTimeUtc
}

if ($needsCompile) {
    & g++ $sourceFile -o $exeFile
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile TD-TR.cpp"
    }
}

$files = Get-ChildItem $InputDir -File -Filter *.plt | Sort-Object Name
$newResults = 0
$rows = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $resultPath = Join-Path $ResultsDir ($baseName + "_TD-TR_wuxi_" + $EpsilonLabel + ".csv")

    if (-not (Test-Path $resultPath)) {
        if ($MaxNewResults -gt 0 -and $newResults -ge $MaxNewResults) {
            break
        }

        & $exeFile $file.FullName $Epsilon $resultPath
        if ($LASTEXITCODE -ne 0) {
            throw "TD-TR failed on $($file.Name)"
        }
        $newResults += 1
    }

    $inputPoints = (Get-Content $file.FullName).Count
    $outputLines = Get-Content $resultPath
    if ($outputLines.Count -eq 0) {
        continue
    }

    $runtimeSeconds = [double]$outputLines[-1]
    $outputPoints = [Math]::Max(0, $outputLines.Count - 1)
    $ratio = if ($inputPoints -gt 0) {
        [Math]::Round($outputPoints / $inputPoints, 6)
    }
    else {
        0
    }

    $rows.Add([pscustomobject]@{
        file_name = $file.Name
        input_points = $inputPoints
        output_points = $outputPoints
        compression_ratio = $ratio
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        converted_path = $file.FullName
        result_path = $resultPath
    }) | Out-Null
}

$rows | Export-Csv -Path $summaryFile -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property input_points -Sum
$outputTotals = $rows | Measure-Object -Property output_points -Sum
$runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum

[pscustomobject]@{
    processed_files = $rows.Count
    newly_computed_results = $newResults
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    overall_compression_ratio = if ($inputTotals.Sum -gt 0) {
        [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6)
    }
    else {
        0
    }
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    summary_file = $summaryFile
} | Format-List
