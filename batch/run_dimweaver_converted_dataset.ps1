param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetName = "geolife",
    [string]$ThresholdLabel = "1e-5",
    [double]$Epsilon = 1e-5,
    [double]$C = 0.85,
    [string]$InputDir = "D:\all_traj_method\skwcl\TR-TR_result\geolife\converted",
    [string]$DatasetRoot = "D:\all_traj_method\skwcl\DimWeaver_result\geolife",
    [string]$PythonExe = "py"
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $repoRoot
$cppRoot = Join-Path $projectRoot "DimWeaver\cpp"
$buildDir = Join-Path $cppRoot "build"
$exePath = Join-Path $buildDir "dimweaver_cli.exe"
$sharedConvertedDir = Join-Path $DatasetRoot "converted"
$outputDir = Join-Path $DatasetRoot $ThresholdLabel
$fileResultsPath = Join-Path $outputDir "file_results.csv"
$summaryPath = Join-Path $outputDir "metrics_summary.csv"
$skippedPath = Join-Path $outputDir "skipped.csv"
$runInfoPath = Join-Path $outputDir "run_info.txt"
$prepareScript = Join-Path $projectRoot "batch\prepare_dimweaver_converted.py"

Ensure-Directory $buildDir
Ensure-Directory $DatasetRoot
Ensure-Directory $sharedConvertedDir
Ensure-Directory $outputDir

& $PythonExe $prepareScript --input-dir $InputDir --output-dir $sharedConvertedDir
if ($LASTEXITCODE -ne 0) {
    throw "Failed to prepare DimWeaver converted inputs."
}

g++ -std=c++17 -I(Join-Path $cppRoot "include") (Join-Path $cppRoot "src\dimweaver.cpp") (Join-Path $cppRoot "src\main.cpp") -o $exePath
if ($LASTEXITCODE -ne 0) {
    throw "Failed to compile DimWeaver."
}

$files = Get-ChildItem -Path $sharedConvertedDir -File -Filter *.csv | Sort-Object Name
'"file","points","compressed_bytes","original_bytes","compression_ratio","compressed_fraction","compression_time_us","decompression_time_us","reconstruction_time_us","max_error"' | Set-Content -Path $fileResultsPath
'"file","reason"' | Set-Content -Path $skippedPath

$processed = 0
$skipped = 0
$totalPoints = 0L
$totalCompressedBytes = 0L
$totalOriginalBytes = 0L
$sumCompressionTimeUs = 0.0
$sumDecompressionTimeUs = 0.0
$sumReconstructionTimeUs = 0.0
$sumCompressionRatio = 0.0
$sumCompressedFraction = 0.0
$globalMaxError = 0.0
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($file in $files) {
    try {
        $output = & $exePath $file.FullName $Epsilon $C
        $metrics = @{}
        foreach ($line in $output) {
            if ($line -match "=") {
                $parts = $line -split "=", 2
                $metrics[$parts[0]] = $parts[1]
            }
        }

        if (-not $metrics.ContainsKey("compression_ratio")) {
            throw "Missing compression output"
        }

        $points = [int64]$metrics["points"]
        $compressedBytes = [int64]$metrics["compressed_bytes"]
        $originalBytes = [int64]$metrics["original_bytes"]
        $compressionRatio = [double]$metrics["compression_ratio"]
        $compressedFraction = [double]$metrics["compressed_fraction"]
        $compressionTimeUs = [double]$metrics["compression_time_us"]
        $decompressionTimeUs = [double]$metrics["decompression_time_us"]
        $reconstructionTimeUs = [double]$metrics["reconstruction_time_us"]
        $maxError = [double]$metrics["max_error"]

        $processed++
        $totalPoints += $points
        $totalCompressedBytes += $compressedBytes
        $totalOriginalBytes += $originalBytes
        $sumCompressionTimeUs += $compressionTimeUs
        $sumDecompressionTimeUs += $decompressionTimeUs
        $sumReconstructionTimeUs += $reconstructionTimeUs
        $sumCompressionRatio += $compressionRatio
        $sumCompressedFraction += $compressedFraction
        if ($maxError -gt $globalMaxError) {
            $globalMaxError = $maxError
        }

        ('"{0}","{1}","{2}","{3}","{4:F6}","{5:F6}","{6:F0}","{7:F0}","{8:F0}","{9:F6}"' -f $file.Name, $points, $compressedBytes, $originalBytes, $compressionRatio, $compressedFraction, $compressionTimeUs, $decompressionTimeUs, $reconstructionTimeUs, $maxError) |
            Add-Content -Path $fileResultsPath
    }
    catch {
        $skipped++
        ('"{0}","{1}"' -f $file.Name, (($_.Exception.Message) -replace '"', '""')) | Add-Content -Path $skippedPath
    }
}

$stopwatch.Stop()

$averageCompressionTimeMs = if ($processed -gt 0) { ($sumCompressionTimeUs / $processed) / 1000.0 } else { 0.0 }
$totalCompressionTimeS = $sumCompressionTimeUs / 1000000.0
$averageDecompressionTimeMs = if ($processed -gt 0) { ($sumDecompressionTimeUs / $processed) / 1000.0 } else { 0.0 }
$totalDecompressionTimeS = $sumDecompressionTimeUs / 1000000.0
$averageReconstructionTimeMs = if ($processed -gt 0) { ($sumReconstructionTimeUs / $processed) / 1000.0 } else { 0.0 }
$totalReconstructionTimeS = $sumReconstructionTimeUs / 1000000.0
$averageCompressionRatio = if ($processed -gt 0) { $sumCompressionRatio / $processed } else { 0.0 }
$averageCompressedFraction = if ($processed -gt 0) { $sumCompressedFraction / $processed } else { 0.0 }
$overallCompressionRatio = if ($totalCompressedBytes -gt 0) { $totalOriginalBytes / $totalCompressedBytes } else { 0.0 }
$spaceSaving = 1.0 - $averageCompressedFraction
$inputPointsPerSecond = if ($totalCompressionTimeS -gt 0) { $totalPoints / $totalCompressionTimeS } else { 0.0 }
$decompressPointsPerSecond = if ($totalDecompressionTimeS -gt 0) { $totalPoints / $totalDecompressionTimeS } else { 0.0 }
$reconstructPointsPerSecond = if ($totalReconstructionTimeS -gt 0) { $totalPoints / $totalReconstructionTimeS } else { 0.0 }

$csvLines = @(
    'algorithm,dataset,epsilon,c,files_total,files_processed,files_skipped,total_points,average_compress_time_ms,total_compress_time_s,average_decompress_time_ms,total_decompress_time_s,average_reconstruct_time_ms,total_reconstruct_time_s,average_compression_ratio,average_compressed_fraction,overall_compression_ratio,space_saving_ratio,global_max_error,total_runtime_s,input_points_per_second,decompress_points_per_second,reconstruct_points_per_second',
    ("DimWeaver,{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14},{15},{16},{17},{18},{19},{20},{21}" -f $DatasetName, $ThresholdLabel, $C, $files.Count, $processed, $skipped, $totalPoints, $averageCompressionTimeMs, $totalCompressionTimeS, $averageDecompressionTimeMs, $totalDecompressionTimeS, $averageReconstructionTimeMs, $totalReconstructionTimeS, $averageCompressionRatio, $averageCompressedFraction, $overallCompressionRatio, $spaceSaving, $globalMaxError, $stopwatch.Elapsed.TotalSeconds, $inputPointsPerSecond, $decompressPointsPerSecond, $reconstructPointsPerSecond)
)
Set-Content -Path $summaryPath -Value $csvLines

$runInfo = @(
    "algorithm=DimWeaver",
    "dataset=$DatasetName",
    "epsilon=$ThresholdLabel",
    "c=$C",
    "processed_files=$processed",
    "skipped_files=$skipped",
    "total_input_points=$totalPoints",
    ("average_compress_time_ms={0:F6}" -f $averageCompressionTimeMs),
    ("total_compress_time_s={0:F6}" -f $totalCompressionTimeS),
    ("average_decompress_time_ms={0:F6}" -f $averageDecompressionTimeMs),
    ("total_decompress_time_s={0:F6}" -f $totalDecompressionTimeS),
    ("average_reconstruct_time_ms={0:F6}" -f $averageReconstructionTimeMs),
    ("total_reconstruct_time_s={0:F6}" -f $totalReconstructionTimeS),
    ("average_compression_ratio={0:F6}" -f $averageCompressionRatio),
    ("average_compressed_fraction={0:F6}" -f $averageCompressedFraction),
    ("overall_compression_ratio={0:F6}" -f $overallCompressionRatio),
    ("space_saving_ratio={0:F6}" -f $spaceSaving),
    ("global_max_error={0:F6}" -f $globalMaxError),
    ("input_points_per_second={0:F3}" -f $inputPointsPerSecond),
    ("decompress_points_per_second={0:F3}" -f $decompressPointsPerSecond),
    ("reconstruct_points_per_second={0:F3}" -f $reconstructPointsPerSecond),
    "input_format=t,x,y_csv",
    "input_note=converted_from_simple_lat_lon_time_to_csv_with_epoch_lon_lat",
    "implementation_note=repository_dimweaver_cpp_port",
    "metrics_csv=$summaryPath",
    "file_results_csv=$fileResultsPath",
    "skipped_csv=$skippedPath"
)
Set-Content -Path $runInfoPath -Value $runInfo

[pscustomobject]@{
    algorithm = "DimWeaver"
    dataset = $DatasetName
    epsilon = $ThresholdLabel
    processed_files = $processed
    skipped_files = $skipped
    total_input_points = $totalPoints
    total_compress_time_s = [Math]::Round($totalCompressionTimeS, 6)
    total_decompress_time_s = [Math]::Round($totalDecompressionTimeS, 6)
    total_reconstruct_time_s = [Math]::Round($totalReconstructionTimeS, 6)
    overall_compression_ratio = [Math]::Round($overallCompressionRatio, 6)
    metrics_csv = $summaryPath
    run_info = $runInfoPath
} | Format-List
