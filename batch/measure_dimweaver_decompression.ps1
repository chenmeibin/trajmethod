param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$ResultsRoot = "D:\all_traj_method\skwcl\DimWeaver_result",
    [string]$DatasetName = "",
    [double]$DefaultC = 0.85
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

$cppRoot = Join-Path $WorkspaceRoot "DimWeaver\cpp"
$buildDir = Join-Path $cppRoot "build"
$exePath = Join-Path $buildDir "dimweaver_cli.exe"

Ensure-Directory $buildDir
g++ -std=c++17 -I(Join-Path $cppRoot "include") (Join-Path $cppRoot "src\dimweaver.cpp") (Join-Path $cppRoot "src\main.cpp") -o $exePath
if ($LASTEXITCODE -ne 0) {
    throw "Failed to compile DimWeaver."
}

$datasetDirs = Get-ChildItem -Path $ResultsRoot -Directory | Where-Object {
    $_.Name -ne "converted" -and ($DatasetName -eq "" -or $_.Name -eq $DatasetName)
} | Sort-Object Name

foreach ($datasetDir in $datasetDirs) {
    $convertedDir = Join-Path $datasetDir.FullName "converted"
    if (-not (Test-Path $convertedDir)) {
        Write-Warning "Skipping $($datasetDir.Name): converted directory missing."
        continue
    }

    $files = Get-ChildItem -Path $convertedDir -File -Filter *.csv | Sort-Object Name
    if ($files.Count -eq 0) {
        Write-Warning "Skipping $($datasetDir.Name): no converted csv files found."
        continue
    }

    $epsilonDirs = Get-ChildItem -Path $datasetDir.FullName -Directory | Where-Object {
        $_.Name -ne "converted" -and (Test-Path (Join-Path $_.FullName "run_info.txt"))
    } | Sort-Object Name

    foreach ($epsilonDir in $epsilonDirs) {
        $epsilon = $epsilonDir.Name
        $fileResultsPath = Join-Path $epsilonDir.FullName "decompression_file_results.csv"
        $summaryPath = Join-Path $epsilonDir.FullName "decompression_summary.csv"
        $runInfoPath = Join-Path $epsilonDir.FullName "decompression_run_info.txt"
        '"file","points","compressed_bytes","decompression_time_us","reconstruction_time_us","compression_time_us","max_error"' | Set-Content -Path $fileResultsPath

        $processed = 0
        $skipped = 0
        $totalPoints = 0L
        $sumCompressionTimeUs = 0.0
        $sumDecompressionTimeUs = 0.0
        $sumReconstructionTimeUs = 0.0
        $totalCompressedBytes = 0L
        $globalMaxError = 0.0
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

        foreach ($file in $files) {
            try {
                $output = & $exePath $file.FullName $epsilon $DefaultC
                if ($LASTEXITCODE -ne 0) {
                    throw "CLI failed"
                }

                $metrics = @{}
                foreach ($line in $output) {
                    if ($line -match "=") {
                        $parts = $line -split "=", 2
                        $metrics[$parts[0]] = $parts[1]
                    }
                }

                foreach ($required in @("points", "compressed_bytes", "compression_time_us", "decompression_time_us", "reconstruction_time_us", "max_error")) {
                    if (-not $metrics.ContainsKey($required)) {
                        throw "Missing metric: $required"
                    }
                }

                $points = [int64]$metrics["points"]
                $compressedBytes = [int64]$metrics["compressed_bytes"]
                $compressionTimeUs = [double]$metrics["compression_time_us"]
                $decompressionTimeUs = [double]$metrics["decompression_time_us"]
                $reconstructionTimeUs = [double]$metrics["reconstruction_time_us"]
                $maxError = [double]$metrics["max_error"]

                $processed++
                $totalPoints += $points
                $totalCompressedBytes += $compressedBytes
                $sumCompressionTimeUs += $compressionTimeUs
                $sumDecompressionTimeUs += $decompressionTimeUs
                $sumReconstructionTimeUs += $reconstructionTimeUs
                if ($maxError -gt $globalMaxError) {
                    $globalMaxError = $maxError
                }

                ('"{0}","{1}","{2}","{3:F0}","{4:F0}","{5:F0}","{6:F6}"' -f $file.Name, $points, $compressedBytes, $decompressionTimeUs, $reconstructionTimeUs, $compressionTimeUs, $maxError) |
                    Add-Content -Path $fileResultsPath
            }
            catch {
                $skipped++
            }
        }

        $stopwatch.Stop()

        $avgDecompressionMs = if ($processed -gt 0) { ($sumDecompressionTimeUs / $processed) / 1000.0 } else { 0.0 }
        $totalDecompressionS = $sumDecompressionTimeUs / 1000000.0
        $avgReconstructionMs = if ($processed -gt 0) { ($sumReconstructionTimeUs / $processed) / 1000.0 } else { 0.0 }
        $totalReconstructionS = $sumReconstructionTimeUs / 1000000.0
        $avgCompressionMs = if ($processed -gt 0) { ($sumCompressionTimeUs / $processed) / 1000.0 } else { 0.0 }
        $totalCompressionS = $sumCompressionTimeUs / 1000000.0
        $throughput = if ($totalDecompressionS -gt 0) { $totalPoints / $totalDecompressionS } else { 0.0 }

        $csvLines = @(
            'algorithm,dataset,epsilon,c,processed_files,skipped_files,total_input_points,total_compressed_bytes,average_decompress_time_ms,total_decompress_time_s,average_reconstruct_time_ms,total_reconstruct_time_s,average_compress_time_ms,total_compress_time_s,decompress_points_per_second,global_max_error,total_runtime_s',
            ("DimWeaver,{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14},{15}" -f $datasetDir.Name, $epsilon, $DefaultC, $processed, $skipped, $totalPoints, $totalCompressedBytes, $avgDecompressionMs, $totalDecompressionS, $avgReconstructionMs, $totalReconstructionS, $avgCompressionMs, $totalCompressionS, $throughput, $globalMaxError, $stopwatch.Elapsed.TotalSeconds)
        )
        Set-Content -Path $summaryPath -Value $csvLines

        $runInfo = @(
            "algorithm=DimWeaver",
            "dataset=$($datasetDir.Name)",
            "epsilon=$epsilon",
            "c=$DefaultC",
            "processed_files=$processed",
            "skipped_files=$skipped",
            "total_input_points=$totalPoints",
            "total_compressed_bytes=$totalCompressedBytes",
            ("average_decompress_time_ms={0:F6}" -f $avgDecompressionMs),
            ("total_decompress_time_s={0:F6}" -f $totalDecompressionS),
            ("average_reconstruct_time_ms={0:F6}" -f $avgReconstructionMs),
            ("total_reconstruct_time_s={0:F6}" -f $totalReconstructionS),
            ("average_compress_time_ms={0:F6}" -f $avgCompressionMs),
            ("total_compress_time_s={0:F6}" -f $totalCompressionS),
            ("decompress_points_per_second={0:F3}" -f $throughput),
            ("global_max_error={0:F6}" -f $globalMaxError),
            "file_results_csv=$fileResultsPath",
            "summary_csv=$summaryPath"
        )
        Set-Content -Path $runInfoPath -Value $runInfo

        [pscustomobject]@{
            algorithm = "DimWeaver"
            dataset = $datasetDir.Name
            epsilon = $epsilon
            processed_files = $processed
            skipped_files = $skipped
            total_decompress_time_s = [Math]::Round($totalDecompressionS, 6)
            total_reconstruct_time_s = [Math]::Round($totalReconstructionS, 6)
            total_compress_time_s = [Math]::Round($totalCompressionS, 6)
            summary_csv = $summaryPath
        } | Format-List
    }
}
