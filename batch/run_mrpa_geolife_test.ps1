param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\data\Geolife",
    [double]$LISSEDth = 1000,
    [string]$ThresholdTag = "1000"
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Get-RelativePathSafe {
    param(
        [string]$BasePath,
        [string]$TargetPath
    )

    $baseFull = [System.IO.Path]::GetFullPath($BasePath)
    if (-not $baseFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $baseFull += [System.IO.Path]::DirectorySeparatorChar
    }
    $targetFull = [System.IO.Path]::GetFullPath($TargetPath)

    $baseUri = [System.Uri]::new($baseFull)
    $targetUri = [System.Uri]::new($targetFull)
    $relativeUri = $baseUri.MakeRelativeUri($targetUri)
    return [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', '\')
}

function Get-GeolifeEffectivePointCount {
    param([string]$FilePath)

    $count = 0
    $lineIndex = 0
    $prevTime = $null

    foreach ($line in Get-Content $FilePath) {
        $lineIndex += 1
        if ($lineIndex -le 6 -or [string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line.Split(",")
        if ($parts.Length -lt 7) {
            continue
        }

        $timeText = ($parts[5].Trim() + " " + $parts[6].Trim())
        if ($null -ne $prevTime -and $prevTime -eq $timeText) {
            $prevTime = $timeText
            continue
        }

        $prevTime = $timeText
        $count += 1
    }

    return $count
}

function Ensure-MrpaExecutable {
    param(
        [string]$WorkspaceRoot,
        [string]$ExePath
    )

    $sourcePath = Join-Path $WorkspaceRoot "batch\MRPA_cpp_port\main.cpp"
    $needsCompile = -not (Test-Path $ExePath)
    if (-not $needsCompile) {
        $needsCompile = (Get-Item $sourcePath).LastWriteTimeUtc -gt (Get-Item $ExePath).LastWriteTimeUtc
    }

    if ($needsCompile) {
        & g++ -O2 -std=c++17 $sourcePath -o $ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile MRPA C++ port"
        }
    }
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$thresholdTag = $ThresholdTag
$testRoot = Join-Path $WorkspaceRoot ("test\MRPA_test_Geolife_{0}" -f $thresholdTag)
$binDir = Join-Path $testRoot "bin"
$resultsDir = Join-Path $testRoot "results"
$summaryPath = Join-Path $testRoot "mrpa_geolife_summary.csv"
$metricsPath = Join-Path $testRoot "mrpa_geolife_metrics_summary.csv"
$masterInfoPath = Join-Path $testRoot "run_info.txt"

Ensure-Directory $testRoot
Ensure-Directory $binDir
Ensure-Directory $resultsDir

$exePath = Join-Path $binDir "mrpa_cpp.exe"
Ensure-MrpaExecutable -WorkspaceRoot $WorkspaceRoot -ExePath $exePath

$files = Get-ChildItem $DatasetRoot -Recurse -File -Filter *.plt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $resultSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $resultSubdir

    $resultPath = Join-Path $resultSubdir ($baseName + ".mrpa_output.txt")
    $effectiveInputPoints = Get-GeolifeEffectivePointCount -FilePath $file.FullName
    if ($effectiveInputPoints -le 1) {
        continue
    }

    & $exePath $file.FullName $LISSEDth $resultPath
    if ($LASTEXITCODE -ne 0) {
        throw "MRPA failed on $($file.FullName)"
    }

    $outputLines = Get-Content $resultPath
    if ($outputLines.Count -eq 0) {
        continue
    }

    $runtimeSeconds = [double]$outputLines[-1]
    $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
    $compressionRatio = if ($effectiveInputPoints -gt 0) {
        [Math]::Round($outputPointCount / $effectiveInputPoints, 6)
    }
    else {
        0
    }

    $rows.Add([pscustomobject]@{
        algorithm = "MRPA"
        dataset = "Geolife"
        lissedth = $thresholdTag
        relative_file = $relativePath
        effective_input_points = $effectiveInputPoints
        output_points = $outputPointCount
        compression_ratio = $compressionRatio
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        result_path = $resultPath
        implementation_note = "C++ port in batch/MRPA_cpp_port"
    }) | Out-Null
}

$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property effective_input_points -Sum
$outputTotals = $rows | Measure-Object -Property output_points -Sum
$runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum
$fileCompressionAverage = ($rows | Measure-Object -Property compression_ratio -Average).Average
$fileRuntimeAverage = ($rows | Measure-Object -Property runtime_seconds -Average).Average
$overallCompressionRatio = if ($inputTotals.Sum -gt 0) {
    [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6)
}
else {
    0
}
$savedRatio = if ($inputTotals.Sum -gt 0) {
    [Math]::Round(1 - ($outputTotals.Sum / $inputTotals.Sum), 6)
}
else {
    0
}
$pointsPerSecond = if ($runtimeTotals.Sum -gt 0) {
    [Math]::Round($inputTotals.Sum / $runtimeTotals.Sum, 3)
}
else {
    0
}
$outputPointsPerSecond = if ($runtimeTotals.Sum -gt 0) {
    [Math]::Round($outputTotals.Sum / $runtimeTotals.Sum, 3)
}
else {
    0
}

[pscustomobject]@{
    algorithm = "MRPA"
    dataset = "Geolife"
    lissedth = $thresholdTag
    processed_files = $rows.Count
    total_effective_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    overall_compression_ratio = $overallCompressionRatio
    saved_ratio = $savedRatio
    avg_file_compression_ratio = [Math]::Round($fileCompressionAverage, 6)
    avg_file_runtime_seconds = [Math]::Round($fileRuntimeAverage, 6)
    input_points_per_second = $pointsPerSecond
    output_points_per_second = $outputPointsPerSecond
    metrics_note = "MRPA uses LISSEDth in projected cumulative SED^2 space; current implementation is the local C++ port"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=MRPA"
    "dataset=Geolife"
    ("lissedth={0}" -f $thresholdTag)
    ("processed_files={0}" -f $rows.Count)
    ("total_effective_input_points={0}" -f [int64]$inputTotals.Sum)
    ("total_output_points={0}" -f [int64]$outputTotals.Sum)
    ("overall_compression_ratio={0}" -f $overallCompressionRatio)
    ("saved_ratio={0}" -f $savedRatio)
    ("total_runtime_seconds={0}" -f [Math]::Round($runtimeTotals.Sum, 6))
    ("avg_file_compression_ratio={0}" -f [Math]::Round($fileCompressionAverage, 6))
    ("avg_file_runtime_seconds={0}" -f [Math]::Round($fileRuntimeAverage, 6))
    ("input_points_per_second={0}" -f $pointsPerSecond)
    ("output_points_per_second={0}" -f $outputPointsPerSecond)
    "threshold_note=LISSEDth is not equivalent to raw-coordinate epsilon; it is a projected cumulative SED^2 threshold"
    "implementation_note=results come from the local C++ port in batch/MRPA_cpp_port"
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
) | Set-Content -Path $masterInfoPath

[pscustomobject]@{
    algorithm = "MRPA"
    dataset = "Geolife"
    lissedth = $thresholdTag
    processed_files = $rows.Count
    total_effective_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    overall_compression_ratio = $overallCompressionRatio
    input_points_per_second = $pointsPerSecond
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    summary_csv = $summaryPath
    metrics_csv = $metricsPath
    info_file = $masterInfoPath
} | Format-List
