param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetName = "geolife",
    [string]$DatasetRoot = "D:\all_traj_method\skwcl\CISED-W_result\geolife",
    [string]$PreparedDir = "",
    [double]$MaxError = 1.113195,
    [string]$MaxErrorTag = "1e-5deg_as_1.113195m",
    [int]$PolygonNum = 16,
    [double]$Accuracy = 0.2,
    [int]$Utf = 3,
    [string]$InputDir = "D:\all_traj_method\skwcl\TR-TR_result\geolife\converted",
    [string]$PythonExe = "py"
)

$ErrorActionPreference = "Stop"

function Ensure-Directory { param([string]$Path) if (-not (Test-Path $Path)) { New-Item -ItemType Directory -Path $Path | Out-Null } }
function Ensure-Exe {
    param([string]$SourcePath, [string]$ExePath)
    $needsCompile = -not (Test-Path $ExePath)
    if (-not $needsCompile) { $needsCompile = (Get-Item $SourcePath).LastWriteTimeUtc -gt (Get-Item $ExePath).LastWriteTimeUtc }
    if ($needsCompile) {
        & g++ -std=c++17 -O2 -Wall -Wextra -pedantic $SourcePath -o $ExePath
        if ($LASTEXITCODE -ne 0) { throw "Failed to compile cisedw_reference.cpp" }
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
function Parse-Metrics {
    param([string[]]$Lines)
    $metrics = @{}
    foreach ($line in $Lines) { if ($line -match "=") { $parts = $line -split "=", 2; $metrics[$parts[0]] = $parts[1] } }
    return $metrics
}

$outputDir = Join-Path $DatasetRoot $MaxErrorTag
$defaultPreparedDir = Join-Path $DatasetRoot "prepared_xy"
if ([string]::IsNullOrWhiteSpace($PreparedDir)) { $PreparedDir = $defaultPreparedDir }
$resultsDir = Join-Path $outputDir "results"
$exeDir = Join-Path $outputDir "bin"
$summaryPath = Join-Path $outputDir "summary.csv"
$metricsPath = Join-Path $outputDir "metrics_summary.csv"
$skippedPath = Join-Path $outputDir "skipped.csv"
$runInfoPath = Join-Path $outputDir "run_info.txt"

Ensure-Directory $DatasetRoot
Ensure-Directory $PreparedDir
Ensure-Directory $outputDir
Ensure-Directory $resultsDir
Ensure-Directory $exeDir

$prepareScript = Join-Path $WorkspaceRoot "batch\prepare_pilotc_converted_xy.py"
& $PythonExe $prepareScript --input-dir $InputDir --output-dir $PreparedDir
if ($LASTEXITCODE -ne 0) { throw "Failed to prepare PILOT-C converted xy inputs." }

$sourcePath = Join-Path $WorkspaceRoot "PILOT-C\cpp_port\src\cisedw_reference.cpp"
$exePath = Join-Path $exeDir "cisedw_reference.exe"
Ensure-Exe -SourcePath $sourcePath -ExePath $exePath

$files = Get-ChildItem $PreparedDir -Recurse -File -Filter *.txt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]
$skipped = New-Object System.Collections.Generic.List[object]
$wallClock = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $PreparedDir -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $resultsSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $resultsSubdir
    $resultPath = Join-Path $resultsSubdir ($baseName + ".csv")

    try {
        $output = & $exePath $file.FullName $MaxError $PolygonNum $resultPath $Accuracy $Utf 2>&1
        if ($LASTEXITCODE -ne 0) { throw ($output | Out-String) }
        $metrics = Parse-Metrics -Lines $output
        if (-not $metrics.ContainsKey("compress_ratio")) { throw "Missing compression metrics in executable output." }
        $inputPoints = [int64]$metrics["points"]
        $outputPoints = [int64]$metrics["kept_points"]
        $compressionRatio = if ($inputPoints -gt 0) { $outputPoints / $inputPoints } else { 0.0 }
        $compressedFraction = $compressionRatio
        $savedRatio = if ($inputPoints -gt 0) { 1 - ($outputPoints / $inputPoints) } else { 0.0 }
        $rows.Add([pscustomobject]@{
            algorithm = "CISED-W"; dataset = $DatasetName; max_error_tag = $MaxErrorTag; max_error_meters = $MaxError; polygon_num = $PolygonNum; accuracy = $Accuracy; utf = $Utf
            relative_file = $relativePath; input_points = $inputPoints; output_points = $outputPoints; compress_bits = [int64]$metrics["compress_bits"]
            compression_ratio = [Math]::Round($compressionRatio, 6); compressed_fraction = [Math]::Round($compressedFraction, 6); saved_ratio = [Math]::Round($savedRatio, 6)
            compression_time_us = [Math]::Round([double]$metrics["compression_time_us"], 3); max_error = [double]$metrics["max_error"]; mean_error = [double]$metrics["mean_error"]; var_error = [double]$metrics["var_error"]
            prepared_path = $file.FullName; result_path = $resultPath
        }) | Out-Null
    } catch {
        $skipped.Add([pscustomobject]@{ algorithm = "CISED-W"; dataset = $DatasetName; max_error_tag = $MaxErrorTag; relative_file = $relativePath; reason = "runtime_error"; detail = $_.Exception.Message }) | Out-Null
    }
}

$wallClock.Stop()
$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8
$skipped | Export-Csv -Path $skippedPath -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property input_points -Sum
$outputTotals = $rows | Measure-Object -Property output_points -Sum
$timeTotalsUs = $rows | Measure-Object -Property compression_time_us -Sum
$avgFileCompressionRatio = ($rows | Measure-Object -Property compression_ratio -Average).Average
$avgCompressedFraction = ($rows | Measure-Object -Property compressed_fraction -Average).Average
$avgFileCompressionTimeUs = ($rows | Measure-Object -Property compression_time_us -Average).Average
$avgFileMeanError = ($rows | Measure-Object -Property mean_error -Average).Average
$avgFileVarError = ($rows | Measure-Object -Property var_error -Average).Average
$globalMaxError = ($rows | Measure-Object -Property max_error -Maximum).Maximum

$overallCompressionRatio = if ($inputTotals.Sum -gt 0) { [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6) } else { 0 }
$weightedCompressedFraction = $overallCompressionRatio
$savedRatio = if ($inputTotals.Sum -gt 0) { [Math]::Round(1 - ($outputTotals.Sum / $inputTotals.Sum), 6) } else { 0 }
$totalRuntimeSeconds = if ($timeTotalsUs.Sum) { [Math]::Round($timeTotalsUs.Sum / 1000000.0, 6) } else { 0 }
$inputPointsPerSecond = if ($timeTotalsUs.Sum -gt 0) { [Math]::Round(($inputTotals.Sum * 1000000.0) / $timeTotalsUs.Sum, 3) } else { 0 }

[pscustomobject]@{
    algorithm = "CISED-W"; dataset = $DatasetName; max_error_tag = $MaxErrorTag; max_error_meters = $MaxError; polygon_num = $PolygonNum; accuracy = $Accuracy; utf = $Utf
    processed_files = $rows.Count; skipped_files = $skipped.Count; total_input_points = [int64]$inputTotals.Sum; total_output_points = [int64]$outputTotals.Sum; total_compress_bits = [int64](($rows | Measure-Object -Property compress_bits -Sum).Sum)
    total_runtime_seconds = $totalRuntimeSeconds; wall_clock_seconds = [Math]::Round($wallClock.Elapsed.TotalSeconds, 6); overall_compression_ratio = $overallCompressionRatio
    average_compressed_fraction = [Math]::Round($avgCompressedFraction, 6); weighted_compressed_fraction = $weightedCompressedFraction; saved_ratio = $savedRatio
    avg_file_compression_ratio = [Math]::Round($avgFileCompressionRatio, 6); avg_file_compression_time_us = [Math]::Round($avgFileCompressionTimeUs, 3)
    avg_file_mean_error = [Math]::Round($avgFileMeanError, 8); avg_file_var_error = [Math]::Round($avgFileVarError, 8); global_max_error = [Math]::Round($globalMaxError, 8)
    input_points_per_second = $inputPointsPerSecond
    metrics_note = "compression_ratio is output_points divided by input_points"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=CISED-W"; "dataset=$DatasetName"; "max_error=$MaxErrorTag"; "max_error_meters=$MaxError"; "polygon_num=$PolygonNum"; "accuracy=$Accuracy"; "utf=$Utf"
    "processed_files=$($rows.Count)"; "skipped_files=$($skipped.Count)"; "total_input_points=$([int64]$inputTotals.Sum)"; "total_output_points=$([int64]$outputTotals.Sum)"
    "total_compress_bits=$([int64]$bitTotals.Sum)"; "overall_compression_ratio=$overallCompressionRatio"; "average_compressed_fraction=$([Math]::Round($avgCompressedFraction, 6))"
    "weighted_compressed_fraction=$weightedCompressedFraction"; "saved_ratio=$savedRatio"; "total_runtime_seconds=$totalRuntimeSeconds"; "wall_clock_seconds=$([Math]::Round($wallClock.Elapsed.TotalSeconds, 6))"
    "avg_file_compression_ratio=$([Math]::Round($avgFileCompressionRatio, 6))"; "avg_file_compression_time_us=$([Math]::Round($avgFileCompressionTimeUs, 3))"; "avg_file_mean_error=$([Math]::Round($avgFileMeanError, 8))"
    "avg_file_var_error=$([Math]::Round($avgFileVarError, 8))"; "global_max_error=$([Math]::Round($globalMaxError, 8))"; "input_points_per_second=$inputPointsPerSecond"
    "coordinate_unit=projected_xy_meters_from_simple_lat_lon_time_inputs"; "implementation_note=cplusplus_port_of_pilotc_python_cisedw_core_logic"
    "summary_csv=$summaryPath"; "metrics_csv=$metricsPath"; "skipped_csv=$skippedPath"
) | Set-Content -Path $runInfoPath

[pscustomobject]@{
    algorithm = "CISED-W"; dataset = $DatasetName; max_error_tag = $MaxErrorTag; processed_files = $rows.Count; skipped_files = $skipped.Count
    total_input_points = [int64]$inputTotals.Sum; total_output_points = [int64]$outputTotals.Sum; overall_compression_ratio = $overallCompressionRatio
    global_max_error = [Math]::Round($globalMaxError, 8); input_points_per_second = $inputPointsPerSecond; metrics_csv = $metricsPath; run_info = $runInfoPath
} | Format-List
