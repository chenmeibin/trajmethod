param(
    [string]$InputDir,
    [string]$ResultsDir,
    [double]$Epsilon,
    [string]$EpsilonLabel,
    [string]$DatasetLabel,
    [int]$MaxNewResults = 0
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceFile = Join-Path $scriptDir "VOLTComTest.cpp"
$exeFile = Join-Path $scriptDir "VOLTComTest.exe"
$summaryFile = Join-Path $ResultsDir "summary.csv"

if (-not (Test-Path $InputDir)) {
    throw "Input directory not found: $InputDir"
}

New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

$needsCompile = -not (Test-Path $exeFile)
if (-not $needsCompile) {
    $needsCompile = (Get-Item $sourceFile).LastWriteTimeUtc -gt (Get-Item $exeFile).LastWriteTimeUtc
}

if ($needsCompile) {
    & g++ $sourceFile -o $exeFile
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile VOLTComTest.cpp"
    }
}

function Parse-StatValue {
    param(
        [string[]]$Lines,
        [string]$Prefix
    )

    $line = $Lines | Where-Object { $_ -like "$Prefix*" } | Select-Object -First 1
    if (-not $line) {
        return $null
    }

    return ($line.Substring($Prefix.Length)).Trim()
}

$files = Get-ChildItem $InputDir -File -Filter *_for_VOLTCom.csv | Sort-Object Name
$newResults = 0
$rows = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $resultPath = Join-Path $ResultsDir ($baseName + "_VOLTCom_" + $DatasetLabel + "_" + $EpsilonLabel + ".txt")

    if (-not (Test-Path $resultPath)) {
        if ($MaxNewResults -gt 0 -and $newResults -ge $MaxNewResults) {
            break
        }

        & $exeFile $file.FullName $Epsilon $resultPath | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "VOLTCom failed on $($file.Name)"
        }
        $newResults += 1
    }

    $lines = Get-Content $resultPath
    if ($lines.Count -eq 0) {
        continue
    }

    $originalPoints = [int](Parse-StatValue -Lines $lines -Prefix "Original Points:")
    $compressedSegments = [int](Parse-StatValue -Lines $lines -Prefix "Compressed Segments:")
    $compressionRatioText = Parse-StatValue -Lines $lines -Prefix "Compression Ratio:"
    $compressionRatio = [double](($compressionRatioText -replace '%', '')) / 100.0
    $totalCompressionTime = [double]((Parse-StatValue -Lines $lines -Prefix "Total Compression Time:") -replace ' us', '')
    $totalDecompressionTime = [double]((Parse-StatValue -Lines $lines -Prefix "Total Decompression Time:") -replace ' us', '')
    $avgError = [double]((Parse-StatValue -Lines $lines -Prefix "Average Error:") -replace ' m', '')
    $maxError = [double]((Parse-StatValue -Lines $lines -Prefix "Maximum Error:") -replace ' m', '')
    $trajectorySegments = [int](Parse-StatValue -Lines $lines -Prefix "Number of Segments:")

    $rows.Add([pscustomobject]@{
        file_name = $file.Name
        original_points = $originalPoints
        compressed_segments = $compressedSegments
        compression_ratio = [Math]::Round($compressionRatio, 6)
        compression_time_us = [Math]::Round($totalCompressionTime, 6)
        decompression_time_us = [Math]::Round($totalDecompressionTime, 6)
        average_error_m = [Math]::Round($avgError, 6)
        maximum_error_m = [Math]::Round($maxError, 6)
        trajectory_segments = $trajectorySegments
        input_path = $file.FullName
        result_path = $resultPath
    }) | Out-Null
}

$rows | Export-Csv -Path $summaryFile -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property original_points -Sum
$outputTotals = $rows | Measure-Object -Property compressed_segments -Sum
$timeTotals = $rows | Measure-Object -Property compression_time_us -Sum
$decompressionTimeTotals = $rows | Measure-Object -Property decompression_time_us -Sum

[pscustomobject]@{
    processed_files = $rows.Count
    newly_computed_results = $newResults
    total_input_points = [int64]$inputTotals.Sum
    total_compressed_segments = [int64]$outputTotals.Sum
    overall_compression_ratio = if ($inputTotals.Sum -gt 0) {
        [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6)
    }
    else {
        0
    }
    total_compression_time_us = [Math]::Round($timeTotals.Sum, 6)
    total_decompression_time_us = [Math]::Round($decompressionTimeTotals.Sum, 6)
    summary_file = $summaryFile
} | Format-List
