param(
    [string]$RawDir = "D:\all_traj_method\data\T-drive",
    [string]$ConvertedDir = "D:\all_traj_method\data\T-drive-tdtr",
    [string]$ResultsDir = "D:\all_traj_method\data\T-drive-tdtr-results",
    [double]$Epsilon = 0.0065
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceFile = Join-Path $scriptDir "TD-TR.cpp"
$exeFile = Join-Path $scriptDir "TD-TR.exe"
$summaryFile = Join-Path $ResultsDir "summary.csv"

if (-not (Test-Path $RawDir)) {
    throw "Raw dataset directory not found: $RawDir"
}

New-Item -ItemType Directory -Force -Path $ConvertedDir | Out-Null
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

$rows = New-Object System.Collections.Generic.List[object]
$files = Get-ChildItem $RawDir -File -Filter *.txt | Sort-Object Name

foreach ($file in $files) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $convertedPath = Join-Path $ConvertedDir ($baseName + ".plt")
    $resultPath = Join-Path $ResultsDir ($baseName + "_tdtr.csv")

    $convertedLines = New-Object System.Collections.Generic.List[string]

    foreach ($line in Get-Content $file.FullName) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line.Split(",")
        if ($parts.Length -lt 4) {
            continue
        }

        try {
            $timestamp = [datetime]::ParseExact($parts[1].Trim(), "yyyy-MM-dd HH:mm:ss", $null)
            $epoch = [int64][DateTimeOffset]::new($timestamp).ToUnixTimeSeconds()
            $lon = [double]$parts[2]
            $lat = [double]$parts[3]
            $convertedLines.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
        }
        catch {
            continue
        }
    }

    if ($convertedLines.Count -eq 0) {
        continue
    }

    Set-Content -Path $convertedPath -Value $convertedLines

    & $exeFile $convertedPath $Epsilon $resultPath
    if ($LASTEXITCODE -ne 0) {
        throw "TD-TR failed on $($file.Name)"
    }

    $outputLines = Get-Content $resultPath
    if ($outputLines.Count -eq 0) {
        continue
    }

    $runtimeSeconds = [double]$outputLines[-1]
    $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
    $compressionRatio = if ($convertedLines.Count -gt 0) {
        [Math]::Round($outputPointCount / $convertedLines.Count, 6)
    }
    else {
        0
    }

    $rows.Add([pscustomobject]@{
        file_name = $file.Name
        input_points = $convertedLines.Count
        output_points = $outputPointCount
        compression_ratio = $compressionRatio
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        converted_path = $convertedPath
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
