param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetName = "geolife",
    [string]$DatasetRoot = "D:\all_traj_method\skwcl\DOTS_result\geolife",
    [string]$InputDir = "D:\all_traj_method\skwcl\TR-TR_result\geolife\converted",
    [string]$PreparedDir = "",
    [double]$LssdThreshold = 1000,
    [string]$ThresholdTag = "1000",
    [string]$PythonExe = "py"
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Ensure-DotsExecutable {
    param([string]$SourcePath, [string]$ExePath)
    $needsCompile = -not (Test-Path $ExePath)
    if (-not $needsCompile) {
        $needsCompile = (Get-Item $SourcePath).LastWriteTimeUtc -gt (Get-Item $ExePath).LastWriteTimeUtc
    }
    if ($needsCompile) {
        & g++ $SourcePath -O2 -std=c++17 -o $ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile DOTS C++ port"
        }
    }
}

function Get-RelativePathSafe {
    param([string]$BasePath, [string]$TargetPath)
    $baseFull = [System.IO.Path]::GetFullPath($BasePath)
    if (-not $baseFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) { $baseFull += [System.IO.Path]::DirectorySeparatorChar }
    $targetFull = [System.IO.Path]::GetFullPath($TargetPath)
    $baseUri = [System.Uri]::new($baseFull)
    $targetUri = [System.Uri]::new($targetFull)
    $relativeUri = $baseUri.MakeRelativeUri($targetUri)
    return [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', '\')
}

function Get-PreparedPointCount {
    param([string]$FilePath)
    $count = 0
    $lineIndex = 0
    foreach ($line in Get-Content $FilePath) {
        $lineIndex += 1
        if ($lineIndex -le 6 -or [string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $count += 1
    }
    return $count
}

$outputDir = Join-Path $DatasetRoot $ThresholdTag
$resultsDir = Join-Path $outputDir "results"
$binDir = Join-Path $outputDir "bin"
$summaryPath = Join-Path $outputDir "summary.csv"
$metricsPath = Join-Path $outputDir "metrics_summary.csv"
$skippedPath = Join-Path $outputDir "skipped.csv"
$runInfoPath = Join-Path $outputDir "run_info.txt"
$defaultPreparedDir = Join-Path $DatasetRoot "prepared_geolife_plt"
if ([string]::IsNullOrWhiteSpace($PreparedDir)) { $PreparedDir = $defaultPreparedDir }

Ensure-Directory $DatasetRoot
Ensure-Directory $PreparedDir
Ensure-Directory $outputDir
Ensure-Directory $resultsDir
Ensure-Directory $binDir

$prepareScript = Join-Path $WorkspaceRoot "batch\prepare_pseudo_geolife_from_converted.py"
& $PythonExe $prepareScript --input-dir $InputDir --output-dir $PreparedDir
if ($LASTEXITCODE -ne 0) { throw "Failed to prepare pseudo Geolife inputs for DOTS." }

$sourcePath = Join-Path $WorkspaceRoot "online\DOTS_cpp_port\main.cpp"
$exePath = Join-Path $binDir "DOTS_cpp.exe"
Ensure-DotsExecutable -SourcePath $SourcePath -ExePath $exePath

$files = Get-ChildItem $PreparedDir -Recurse -File -Filter *.plt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]
$skipped = New-Object System.Collections.Generic.List[object]

$totalInputPoints = [int64]0
$totalOutputPoints = [int64]0
$totalRuntimeSeconds = 0.0
$wallClock = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $PreparedDir -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $resultSubdir = if ([string]::IsNullOrWhiteSpace($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $resultSubdir
    $resultPath = Join-Path $resultSubdir ($baseName + ".dots_output.csv")

    try {
        $inputPointCount = Get-PreparedPointCount -FilePath $file.FullName
        if ($inputPointCount -le 0) {
            throw "No valid points after preparation."
        }

        & $exePath $file.FullName $LssdThreshold $resultPath | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "DOTS failed on $($file.FullName)"
        }

        $outputLines = Get-Content $resultPath
        if ($outputLines.Count -eq 0) {
            throw "DOTS produced empty output."
        }

        $runtimeSeconds = [double]$outputLines[-1]
        $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
        $compressionRatio = if ($inputPointCount -gt 0) { [Math]::Round($outputPointCount / $inputPointCount, 6) } else { 0.0 }
        $compressedFraction = $compressionRatio
        $savedRatio = if ($inputPointCount -gt 0) { [Math]::Round(1 - ($outputPointCount / $inputPointCount), 6) } else { 0.0 }

        $totalInputPoints += $inputPointCount
        $totalOutputPoints += $outputPointCount
        $totalRuntimeSeconds += $runtimeSeconds

        $rows.Add([pscustomobject]@{
            algorithm = "DOTS"
            dataset = $DatasetName
            lssd_threshold = $ThresholdTag
            relative_file = $relativePath
            input_points = $inputPointCount
            output_points = $outputPointCount
            compression_ratio = $compressionRatio
            compressed_fraction = $compressedFraction
            saved_ratio = $savedRatio
            runtime_seconds = [Math]::Round($runtimeSeconds, 6)
            prepared_path = $file.FullName
            result_path = $resultPath
        }) | Out-Null
    } catch {
        $skipped.Add([pscustomobject]@{
            algorithm = "DOTS"
            dataset = $DatasetName
            lssd_threshold = $ThresholdTag
            relative_file = $relativePath
            reason = $_.Exception.Message
        }) | Out-Null
    }
}

$wallClock.Stop()
$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8
$skipped | Export-Csv -Path $skippedPath -NoTypeInformation -Encoding UTF8

$avgFileCompressionRatio = if ($rows.Count -gt 0) { ($rows | Measure-Object -Property compression_ratio -Average).Average } else { 0 }
$avgCompressedFraction = if ($rows.Count -gt 0) { ($rows | Measure-Object -Property compressed_fraction -Average).Average } else { 0 }
$avgRuntimeSeconds = if ($rows.Count -gt 0) { ($rows | Measure-Object -Property runtime_seconds -Average).Average } else { 0 }
$overallCompressionRatio = if ($totalInputPoints -gt 0) { [Math]::Round($totalOutputPoints / $totalInputPoints, 6) } else { 0 }
$savedRatio = if ($totalInputPoints -gt 0) { [Math]::Round(1 - ($totalOutputPoints / $totalInputPoints), 6) } else { 0 }
$inputPointsPerSecond = if ($totalRuntimeSeconds -gt 0) { [Math]::Round($totalInputPoints / $totalRuntimeSeconds, 3) } else { 0 }
$outputPointsPerSecond = if ($totalRuntimeSeconds -gt 0) { [Math]::Round($totalOutputPoints / $totalRuntimeSeconds, 3) } else { 0 }

[pscustomobject]@{
    algorithm = "DOTS"
    dataset = $DatasetName
    lssd_threshold = $ThresholdTag
    processed_files = $rows.Count
    skipped_files = $skipped.Count
    total_input_points = [int64]$totalInputPoints
    total_output_points = [int64]$totalOutputPoints
    total_runtime_seconds = [Math]::Round($totalRuntimeSeconds, 6)
    wall_clock_seconds = [Math]::Round($wallClock.Elapsed.TotalSeconds, 6)
    overall_compression_ratio = $overallCompressionRatio
    saved_ratio = $savedRatio
    avg_file_compression_ratio = [Math]::Round($avgFileCompressionRatio, 6)
    avg_file_compressed_fraction = [Math]::Round($avgCompressedFraction, 6)
    avg_file_runtime_seconds = [Math]::Round($avgRuntimeSeconds, 6)
    input_points_per_second = $inputPointsPerSecond
    output_points_per_second = $outputPointsPerSecond
    metrics_note = "DOTS uses LSSD threshold after Mercator projection and internal normalization"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=DOTS"
    "dataset=$DatasetName"
    "lssd_threshold=$ThresholdTag"
    "processed_files=$($rows.Count)"
    "skipped_files=$($skipped.Count)"
    "total_input_points=$totalInputPoints"
    "total_output_points=$totalOutputPoints"
    "overall_compression_ratio=$overallCompressionRatio"
    "saved_ratio=$savedRatio"
    ("total_runtime_seconds={0}" -f [Math]::Round($totalRuntimeSeconds, 6))
    ("wall_clock_seconds={0}" -f [Math]::Round($wallClock.Elapsed.TotalSeconds, 6))
    ("avg_file_compression_ratio={0}" -f [Math]::Round($avgFileCompressionRatio, 6))
    ("avg_file_compressed_fraction={0}" -f [Math]::Round($avgCompressedFraction, 6))
    ("avg_file_runtime_seconds={0}" -f [Math]::Round($avgRuntimeSeconds, 6))
    ("input_points_per_second={0}" -f $inputPointsPerSecond)
    ("output_points_per_second={0}" -f $outputPointsPerSecond)
    "parameter_type=lssd_threshold"
    "input_format=pseudo_geolife_plt_converted_from_simple_lat_lon_epoch"
    "implementation_note=local_cpp_port_of_online_DOTS_batch_core"
    "summary_csv=$summaryPath"
    "metrics_csv=$metricsPath"
    "skipped_csv=$skippedPath"
) | Set-Content -Path $runInfoPath

[pscustomobject]@{
    algorithm = "DOTS"
    dataset = $DatasetName
    lssd_threshold = $ThresholdTag
    processed_files = $rows.Count
    skipped_files = $skipped.Count
    total_input_points = [int64]$totalInputPoints
    total_output_points = [int64]$totalOutputPoints
    overall_compression_ratio = $overallCompressionRatio
    input_points_per_second = $inputPointsPerSecond
    metrics_csv = $metricsPath
    run_info = $runInfoPath
} | Format-List
