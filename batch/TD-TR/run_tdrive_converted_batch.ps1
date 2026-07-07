param(
    [string]$ConvertedDir = "D:\all_traj_method\skwcl\TR-TR_result\converted",
    [string]$ThresholdDir = "D:\all_traj_method\skwcl\TR-TR_result\1e-5",
    [string]$ResultsDir = "",
    [double]$Epsilon = 1e-5
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceFile = Join-Path $scriptDir "TD-TR.cpp"
$exeFile = Join-Path $scriptDir "TD-TR.exe"

if (-not (Test-Path $ConvertedDir)) {
    throw "Converted dataset directory not found: $ConvertedDir"
}

if ([string]::IsNullOrWhiteSpace($ResultsDir)) {
    New-Item -ItemType Directory -Force -Path $ThresholdDir | Out-Null

    $runDirs = Get-ChildItem $ThresholdDir -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^RUN-(\d+)$' }
    $nextRunNumber = if ($runDirs) {
        (($runDirs | ForEach-Object {
            [void]($_.Name -match '^RUN-(\d+)$')
            [int]$Matches[1]
        } | Measure-Object -Maximum).Maximum + 1)
    }
    else {
        1
    }

    $ResultsDir = Join-Path $ThresholdDir ("RUN-{0}" -f $nextRunNumber)
}

New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null
$summaryFile = Join-Path $ResultsDir "summary.csv"

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

$rows = New-Object System.Collections.Generic.List[object]
$files = Get-ChildItem $ConvertedDir -File -Filter *.plt | Sort-Object Name

foreach ($file in $files) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $resultPath = Join-Path $ResultsDir ($baseName + "_tdtr.csv")

    $inputPoints = (Get-Content $file.FullName | Measure-Object).Count
    if ($inputPoints -eq 0) {
        continue
    }

    & $exeFile $file.FullName $Epsilon $resultPath
    if ($LASTEXITCODE -ne 0) {
        throw "TD-TR failed on $($file.Name)"
    }

    $outputLines = Get-Content $resultPath
    if ($outputLines.Count -eq 0) {
        continue
    }

    $runtimeSeconds = [double]$outputLines[-1]
    $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
    $compressionRatio = if ($inputPoints -gt 0) {
        [Math]::Round($outputPointCount / $inputPoints, 6)
    }
    else {
        0
    }

    $rows.Add([pscustomobject]@{
        file_name = $file.Name
        input_points = $inputPoints
        output_points = $outputPointCount
        compression_ratio = $compressionRatio
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        converted_path = $file.FullName
        result_path = $resultPath
    }) | Out-Null
}

$rows | Export-Csv -Path $summaryFile -NoTypeInformation -Encoding UTF8

$totals = $rows | Measure-Object -Property input_points -Sum
$compressedTotals = $rows | Measure-Object -Property output_points -Sum
$runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum

[pscustomobject]@{
    processed_files = $rows.Count
    total_input_points = [int64]$totals.Sum
    total_output_points = [int64]$compressedTotals.Sum
    overall_compression_ratio = if ($totals.Sum -gt 0) {
        [Math]::Round($compressedTotals.Sum / $totals.Sum, 6)
    }
    else {
        0
    }
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    summary_file = $summaryFile
} | Format-List
