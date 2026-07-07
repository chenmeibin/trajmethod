param(
    [string]$DatasetName = "Geolife",
    [string]$ThresholdLabel = "1e-5",
    [double]$Epsilon = 1e-5,
    [double]$C = 0.85,
    [string]$InputDir = "D:\all_traj_method\data\Geolife",
    [string]$OutputDir = "D:\all_traj_method\test\DimWeaver_test_Geolife_1e-5"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $repoRoot
$cppRoot = Join-Path $projectRoot "DimWeaver\cpp"
$buildDir = Join-Path $cppRoot "build"
$exePath = Join-Path $buildDir "dimweaver_cli.exe"
$convertedDir = Join-Path $OutputDir "converted"
$pythonExe = "C:\Users\Chen\AppData\Local\Programs\Python\Python314\python.exe"
$prepareScript = Join-Path $projectRoot "batch\prepare_dimweaver_geolife.py"

New-Item -ItemType Directory -Force $buildDir | Out-Null
New-Item -ItemType Directory -Force $OutputDir | Out-Null
New-Item -ItemType Directory -Force $convertedDir | Out-Null

& $pythonExe $prepareScript --input-dir $InputDir --output-dir $convertedDir

g++ -std=c++17 -I(Join-Path $cppRoot "include") (Join-Path $cppRoot "src\dimweaver.cpp") (Join-Path $cppRoot "src\main.cpp") -o $exePath

$files = Get-ChildItem -Path $convertedDir -File -Filter *.csv | Sort-Object Name
$fileResultsPath = Join-Path $OutputDir "file_results.csv"
$summaryPath = Join-Path $OutputDir "dimweaver_geolife_metrics_summary.csv"
$skippedPath = Join-Path $OutputDir "skipped.csv"
$runInfoPath = Join-Path $OutputDir "run_info.txt"

'"file","points","compressed_bytes","original_bytes","compression_ratio","compressed_fraction","compression_time_us","max_error"' | Set-Content -Path $fileResultsPath
'"file","reason"' | Set-Content -Path $skippedPath

$processed = 0
$skipped = 0
$totalPoints = 0L
$totalCompressedBytes = 0L
$totalOriginalBytes = 0L
$sumCompressionTimeUs = 0.0
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
        $maxError = [double]$metrics["max_error"]

        $processed++
        $totalPoints += $points
        $totalCompressedBytes += $compressedBytes
        $totalOriginalBytes += $originalBytes
        $sumCompressionTimeUs += $compressionTimeUs
        $sumCompressionRatio += $compressionRatio
        $sumCompressedFraction += $compressedFraction
        if ($maxError -gt $globalMaxError) {
            $globalMaxError = $maxError
        }

        ('"{0}","{1}","{2}","{3}","{4:F6}","{5:F6}","{6:F0}","{7:F6}"' -f $file.Name, $points, $compressedBytes, $originalBytes, $compressionRatio, $compressedFraction, $compressionTimeUs, $maxError) |
            Add-Content -Path $fileResultsPath
    } catch {
        $skipped++
        ('"{0}","{1}"' -f $file.Name, (($_.Exception.Message) -replace '"', '""')) | Add-Content -Path $skippedPath
    }
}

$stopwatch.Stop()

$averageCompressionTimeMs = if ($processed -gt 0) { ($sumCompressionTimeUs / $processed) / 1000.0 } else { 0.0 }
$totalCompressionTimeS = $sumCompressionTimeUs / 1000000.0
$averageCompressionRatio = if ($processed -gt 0) { $sumCompressionRatio / $processed } else { 0.0 }
$averageCompressedFraction = if ($processed -gt 0) { $sumCompressedFraction / $processed } else { 0.0 }
$weightedCompressionRatio = if ($totalCompressedBytes -gt 0) { $totalOriginalBytes / $totalCompressedBytes } else { 0.0 }
$spaceSaving = 1.0 - $averageCompressedFraction
$inputPointsPerSecond = if ($totalCompressionTimeS -gt 0) { $totalPoints / $totalCompressionTimeS } else { 0.0 }

$csvLines = @(
    'algorithm,dataset,epsilon,c,files_total,files_processed,files_skipped,total_points,average_compress_time_ms,total_compress_time_s,average_compression_ratio,average_compressed_fraction,weighted_compression_ratio,space_saving_ratio,global_max_error,total_runtime_s,input_points_per_second',
    ("DimWeaver,{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14},{15}" -f $DatasetName, $ThresholdLabel, $C, $files.Count, $processed, $skipped, $totalPoints, $averageCompressionTimeMs, $totalCompressionTimeS, $averageCompressionRatio, $averageCompressedFraction, $weightedCompressionRatio, $spaceSaving, $globalMaxError, $stopwatch.Elapsed.TotalSeconds, $inputPointsPerSecond)
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
    ("average_compression_ratio={0:F6}" -f $averageCompressionRatio),
    ("average_compressed_fraction={0:F6}" -f $averageCompressedFraction),
    ("weighted_compression_ratio={0:F6}" -f $weightedCompressionRatio),
    ("space_saving_ratio={0:F6}" -f $spaceSaving),
    ("global_max_error={0:F6}" -f $globalMaxError),
    ("input_points_per_second={0:F3}" -f $inputPointsPerSecond),
    "input_format=t,x,y_csv",
    "input_note=converted_from_geolife_plt_to_csv_with_unix_timestamp_lon_lat",
    "implementation_note=repository_dimweaver_cpp_port",
    "summary_csv=$summaryPath",
    "file_results_csv=$fileResultsPath",
    "skipped_csv=$skippedPath"
)
Set-Content -Path $runInfoPath -Value $runInfo

Write-Host "Saved results to $OutputDir"
