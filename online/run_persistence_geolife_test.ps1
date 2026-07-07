param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\data\Geolife",
    [double]$MinimumDistance = 100,
    [string]$DistanceTag = "100"
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

function Ensure-PersistenceExecutable {
    param(
        [string]$SourcePath,
        [string]$ExePath
    )

    $needsCompile = -not (Test-Path $ExePath)
    if (-not $needsCompile) {
        $needsCompile = (Get-Item $SourcePath).LastWriteTimeUtc -gt (Get-Item $ExePath).LastWriteTimeUtc
    }

    if ($needsCompile) {
        & g++ $SourcePath -O2 -std=c++17 -o $ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile Persistence C++ port"
        }
    }
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$testRoot = Join-Path $WorkspaceRoot ("test\Persistence_test_Geolife_{0}" -f $DistanceTag)
$resultsDir = Join-Path $testRoot "results"
$binDir = Join-Path $testRoot "bin"
$summaryPath = Join-Path $testRoot "persistence_geolife_summary.csv"
$metricsPath = Join-Path $testRoot "persistence_geolife_metrics_summary.csv"
$infoPath = Join-Path $testRoot "run_info.txt"
$failuresPath = Join-Path $testRoot "failed_files.csv"

Ensure-Directory $testRoot
Ensure-Directory $resultsDir
Ensure-Directory $binDir

$sourcePath = Join-Path $WorkspaceRoot "online\Persistence_cpp_port\main.cpp"
$exePath = Join-Path $binDir "Persistence_cpp.exe"
Ensure-PersistenceExecutable -SourcePath $sourcePath -ExePath $exePath

$files = Get-ChildItem $DatasetRoot -Recurse -File -Filter *.plt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $resultsSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $resultsSubdir

    $resultPath = Join-Path $resultsSubdir ($baseName + ".persistence_output.csv")

    $inputPointCount = 0
    $lineIndex = 0
    foreach ($line in Get-Content $file.FullName) {
        $lineIndex += 1
        if ($lineIndex -le 6 -or [string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $inputPointCount += 1
    }

    if ($inputPointCount -eq 0) {
        continue
    }

    & $exePath $file.FullName $MinimumDistance $resultPath
    if ($LASTEXITCODE -ne 0) {
        $failures.Add([pscustomobject]@{
            algorithm = "Persistence"
            dataset = "Geolife"
            minimum_distance = $DistanceTag
            relative_file = $relativePath
            input_points = $inputPointCount
            failure_reason = "persistence_cpp_port_nonzero_exit"
        }) | Out-Null
        continue
    }

    $outputLines = Get-Content $resultPath
    if ($outputLines.Count -eq 0) {
        continue
    }

    $runtimeSeconds = [double]$outputLines[-1]
    $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
    $compressionRatio = if ($inputPointCount -gt 0) {
        [Math]::Round($outputPointCount / $inputPointCount, 6)
    }
    else {
        0
    }
    $inputPointsPerSecondFile = if ($runtimeSeconds -gt 0) {
        [Math]::Round($inputPointCount / $runtimeSeconds, 3)
    }
    else {
        0
    }
    $outputPointsPerSecondFile = if ($runtimeSeconds -gt 0) {
        [Math]::Round($outputPointCount / $runtimeSeconds, 3)
    }
    else {
        0
    }

    $rows.Add([pscustomobject]@{
        algorithm = "Persistence"
        dataset = "Geolife"
        minimum_distance = $DistanceTag
        relative_file = $relativePath
        input_points = $inputPointCount
        output_points = $outputPointCount
        compression_ratio = $compressionRatio
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        input_points_per_second = $inputPointsPerSecondFile
        output_points_per_second = $outputPointsPerSecondFile
        result_path = $resultPath
    }) | Out-Null
}

$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8
$failures | Export-Csv -Path $failuresPath -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property input_points -Sum
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
$inputPointsPerSecond = if ($runtimeTotals.Sum -gt 0) {
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
    algorithm = "Persistence"
    dataset = "Geolife"
    minimum_distance = $DistanceTag
    processed_files = $rows.Count
    failed_files = $failures.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    overall_compression_ratio = $overallCompressionRatio
    saved_ratio = $savedRatio
    avg_file_compression_ratio = [Math]::Round($fileCompressionAverage, 6)
    avg_file_runtime_seconds = [Math]::Round($fileRuntimeAverage, 6)
    input_points_per_second = $inputPointsPerSecond
    output_points_per_second = $outputPointsPerSecond
    metrics_note = "Persistence uses minimum-distance threshold after Mercator projection and internal normalization"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=Persistence"
    "dataset=Geolife"
    ("minimum_distance={0}" -f $DistanceTag)
    ("processed_files={0}" -f $rows.Count)
    ("failed_files={0}" -f $failures.Count)
    ("total_input_points={0}" -f [int64]$inputTotals.Sum)
    ("total_output_points={0}" -f [int64]$outputTotals.Sum)
    ("overall_compression_ratio={0}" -f $overallCompressionRatio)
    ("saved_ratio={0}" -f $savedRatio)
    ("total_runtime_seconds={0}" -f [Math]::Round($runtimeTotals.Sum, 6))
    ("avg_file_compression_ratio={0}" -f [Math]::Round($fileCompressionAverage, 6))
    ("avg_file_runtime_seconds={0}" -f [Math]::Round($fileRuntimeAverage, 6))
    ("input_points_per_second={0}" -f $inputPointsPerSecond)
    ("output_points_per_second={0}" -f $outputPointsPerSecond)
    "parameter_type=minimum_distance"
    "distance_unit_note=Persistence local C++ port follows repository GeoLife parsing, Mercator projection, and normalized distance-threshold semantics"
    "implementation_note=local_cpp_port_of_persistence_batch_core"
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
    ("failed_files_csv={0}" -f $failuresPath)
) | Set-Content -Path $infoPath

[pscustomobject]@{
    algorithm = "Persistence"
    dataset = "Geolife"
    minimum_distance = $DistanceTag
    processed_files = $rows.Count
    failed_files = $failures.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    overall_compression_ratio = $overallCompressionRatio
    input_points_per_second = $inputPointsPerSecond
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    summary_csv = $summaryPath
    metrics_csv = $metricsPath
    info_file = $infoPath
} | Format-List
