param(
    [string]$DatasetName = "T-drive",
    [string]$ThresholdLabel = "1e-5",
    [double]$Epsilon = 1e-5,
    [double]$C = 0.85,
    [string]$InputFormat = "tdrive",
    [string]$InputDir = "D:\all_traj_method\Data\T-drive"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$cppRoot = Join-Path $repoRoot "cpp"
$buildDir = Join-Path $cppRoot "build"
$exePath = Join-Path $buildDir "dimweaver_cli.exe"
$resultRoot = Join-Path $repoRoot "result"
$runFolderName = "{0}_{1}" -f $DatasetName, $ThresholdLabel
$runDir = Join-Path $resultRoot $runFolderName

New-Item -ItemType Directory -Force $buildDir | Out-Null
New-Item -ItemType Directory -Force $resultRoot | Out-Null
New-Item -ItemType Directory -Force $runDir | Out-Null

g++ -std=c++17 -I(Join-Path $cppRoot "include") (Join-Path $cppRoot "src\\dimweaver.cpp") (Join-Path $cppRoot "src\\main.cpp") -o $exePath

$files = Get-ChildItem -Path $InputDir -File | Sort-Object Name
$fileResultsPath = Join-Path $runDir "file_results.csv"
$summaryPath = Join-Path $runDir "summary.csv"
$fullSummaryPath = Join-Path $resultRoot (($DatasetName.ToLower() -replace "-", "_") + "_full_summary.csv")
$skippedPath = Join-Path $runDir "skipped.csv"

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
$maxError = 0.0
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($file in $files) {
    try {
        $output = & $exePath --input-format $InputFormat $file.FullName $Epsilon $C
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
        $errorValue = [double]$metrics["max_error"]

        $processed++
        $totalPoints += $points
        $totalCompressedBytes += $compressedBytes
        $totalOriginalBytes += $originalBytes
        $sumCompressionTimeUs += $compressionTimeUs
        $sumCompressionRatio += $compressionRatio
        $sumCompressedFraction += $compressedFraction
        if ($errorValue -gt $maxError) {
            $maxError = $errorValue
        }

        ('"{0}","{1}","{2}","{3}","{4:F6}","{5:F6}","{6:F0}","{7:F6}"' -f $file.Name, $points, $compressedBytes, $originalBytes, $compressionRatio, $compressedFraction, $compressionTimeUs, $errorValue) |
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

'"dataset","threshold","files_total","files_processed","files_skipped","total_points","average_compress_time_ms","total_compress_time_s","average_compress_ratio","average_compressed_fraction","weighted_compress_ratio","global_max_error","total_runtime_s"' |
    Set-Content -Path $summaryPath
('"'+$DatasetName+'","'+$ThresholdLabel+'","{0}","{1}","{2}","{3}","{4:F6}","{5:F6}","{6:F6}","{7:F6}","{8:F6}","{9:F6}","{10:F6}"' -f $files.Count, $processed, $skipped, $totalPoints, $averageCompressionTimeMs, $totalCompressionTimeS, $averageCompressionRatio, $averageCompressedFraction, $weightedCompressionRatio, $maxError, $stopwatch.Elapsed.TotalSeconds) |
    Add-Content -Path $summaryPath

'"dataset","threshold","files_total","files_processed","files_skipped","total_points","average_compress_time_ms","total_compress_time_s","average_compress_ratio","average_compressed_fraction","weighted_compress_ratio","global_max_error","total_runtime_s","result_folder"' |
    Set-Content -Path $fullSummaryPath
('"'+$DatasetName+'","'+$ThresholdLabel+'","{0}","{1}","{2}","{3}","{4:F6}","{5:F6}","{6:F6}","{7:F6}","{8:F6}","{9:F6}","{10:F6}","{11}"' -f $files.Count, $processed, $skipped, $totalPoints, $averageCompressionTimeMs, $totalCompressionTimeS, $averageCompressionRatio, $averageCompressedFraction, $weightedCompressionRatio, $maxError, $stopwatch.Elapsed.TotalSeconds, $runFolderName) |
    Add-Content -Path $fullSummaryPath

Write-Host "Saved results to $runDir"
