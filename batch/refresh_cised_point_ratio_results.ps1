param(
    [string]$WorkspaceRoot = "D:\all_traj_method"
)

$ErrorActionPreference = "Stop"

function Update-CisedResultSet {
    param(
        [string]$Algorithm,
        [string]$RootPath
    )

    $metricNote = "compression_ratio is output_points divided by input_points"
    $datasetDirs = Get-ChildItem $RootPath -Directory -ErrorAction Stop

    foreach ($datasetDir in $datasetDirs) {
        $thresholdDirs = Get-ChildItem $datasetDir.FullName -Directory | Where-Object { $_.Name -ne "prepared_xy" }
        foreach ($thresholdDir in $thresholdDirs) {
            $summaryPath = Join-Path $thresholdDir.FullName "summary.csv"
            $metricsPath = Join-Path $thresholdDir.FullName "metrics_summary.csv"
            $runInfoPath = Join-Path $thresholdDir.FullName "run_info.txt"
            $skippedPath = Join-Path $thresholdDir.FullName "skipped.csv"

            if (-not (Test-Path $summaryPath) -or -not (Test-Path $metricsPath) -or -not (Test-Path $runInfoPath)) {
                continue
            }

            $rows = Import-Csv $summaryPath
            if (-not $rows) {
                continue
            }

            foreach ($row in $rows) {
                $inputPoints = [double]$row.input_points
                $outputPoints = [double]$row.output_points
                $ratio = if ($inputPoints -gt 0) { $outputPoints / $inputPoints } else { 0.0 }
                $row.compression_ratio = [Math]::Round($ratio, 6)
                $row.compressed_fraction = [Math]::Round($ratio, 6)
                $row.saved_ratio = [Math]::Round(1 - $ratio, 6)
            }
            $rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

            $metricsRows = Import-Csv $metricsPath
            $metricsRow = $metricsRows | Select-Object -First 1
            if (-not $metricsRow) {
                continue
            }

            $inputTotal = ($rows | Measure-Object -Property input_points -Sum).Sum
            $outputTotal = ($rows | Measure-Object -Property output_points -Sum).Sum
            $avgFileCompressionRatio = ($rows | Measure-Object -Property compression_ratio -Average).Average
            $avgCompressedFraction = ($rows | Measure-Object -Property compressed_fraction -Average).Average
            $avgCompressionTimeUs = ($rows | Measure-Object -Property compression_time_us -Average).Average
            $avgMeanError = ($rows | Measure-Object -Property mean_error -Average).Average
            $avgVarError = ($rows | Measure-Object -Property var_error -Average).Average
            $globalMaxError = ($rows | Measure-Object -Property max_error -Maximum).Maximum
            $totalRuntimeSeconds = [double]$metricsRow.total_runtime_seconds
            $overallCompressionRatio = if ($inputTotal -gt 0) { [Math]::Round($outputTotal / $inputTotal, 6) } else { 0.0 }
            $savedRatio = if ($inputTotal -gt 0) { [Math]::Round(1 - ($outputTotal / $inputTotal), 6) } else { 0.0 }
            $inputPointsPerSecond = if ($totalRuntimeSeconds -gt 0) { [Math]::Round($inputTotal / $totalRuntimeSeconds, 3) } else { 0.0 }
            $skippedCount = 0
            if (Test-Path $skippedPath) {
                $skippedImported = Import-Csv $skippedPath
                if ($skippedImported) { $skippedCount = @($skippedImported).Count }
            }

            $updatedMetricsRow = [pscustomobject]@{}
            foreach ($property in $metricsRow.PSObject.Properties) {
                $updatedMetricsRow | Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value
            }
            $updatedMetricsRow.processed_files = @($rows).Count
            $updatedMetricsRow.skipped_files = $skippedCount
            $updatedMetricsRow.total_input_points = [int64]$inputTotal
            $updatedMetricsRow.total_output_points = [int64]$outputTotal
            $updatedMetricsRow.overall_compression_ratio = $overallCompressionRatio
            $updatedMetricsRow.average_compressed_fraction = [Math]::Round($avgCompressedFraction, 6)
            $updatedMetricsRow.weighted_compressed_fraction = $overallCompressionRatio
            $updatedMetricsRow.saved_ratio = $savedRatio
            $updatedMetricsRow.avg_file_compression_ratio = [Math]::Round($avgFileCompressionRatio, 6)
            $updatedMetricsRow.avg_file_compression_time_us = [Math]::Round([double]$avgCompressionTimeUs, 3)
            $updatedMetricsRow.avg_file_mean_error = [Math]::Round([double]$avgMeanError, 8)
            $updatedMetricsRow.avg_file_var_error = [Math]::Round([double]$avgVarError, 8)
            $updatedMetricsRow.global_max_error = [Math]::Round([double]$globalMaxError, 8)
            $updatedMetricsRow.input_points_per_second = $inputPointsPerSecond
            $updatedMetricsRow.metrics_note = $metricNote
            $updatedMetricsRow | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

            $runInfo = Get-Content $runInfoPath
            $replacements = @{
                "overall_compression_ratio" = $overallCompressionRatio
                "average_compressed_fraction" = [Math]::Round($avgCompressedFraction, 6)
                "weighted_compressed_fraction" = $overallCompressionRatio
                "saved_ratio" = $savedRatio
                "avg_file_compression_ratio" = [Math]::Round($avgFileCompressionRatio, 6)
                "avg_file_compression_time_us" = [Math]::Round([double]$avgCompressionTimeUs, 3)
                "avg_file_mean_error" = [Math]::Round([double]$avgMeanError, 8)
                "avg_file_var_error" = [Math]::Round([double]$avgVarError, 8)
                "global_max_error" = [Math]::Round([double]$globalMaxError, 8)
                "input_points_per_second" = $inputPointsPerSecond
            }

            $updatedLines = foreach ($line in $runInfo) {
                $matched = $false
                foreach ($key in $replacements.Keys) {
                    if ($line -like "$key=*") {
                        "$key=$($replacements[$key])"
                        $matched = $true
                        break
                    }
                }
                if (-not $matched) { $line }
            }
            Set-Content -Path $runInfoPath -Value $updatedLines
        }
    }
}

Update-CisedResultSet -Algorithm "CISED-S" -RootPath (Join-Path $WorkspaceRoot "skwcl\CISED-S_result")
Update-CisedResultSet -Algorithm "CISED-W" -RootPath (Join-Path $WorkspaceRoot "skwcl\CISED-W_result")
