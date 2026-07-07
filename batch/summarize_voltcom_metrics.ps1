param(
    [string]$WorkspaceRoot = "D:\all_traj_method"
)

$ErrorActionPreference = "Stop"

function Save-SingleRowCsv {
    param(
        [string]$Path,
        [psobject]$Row
    )

    @($Row) | Export-Csv -Path $Path -NoTypeInformation -Encoding UTF8
}

$voltcomRoot = Join-Path $WorkspaceRoot "skwcl\voltcom_result"
if (-not (Test-Path $voltcomRoot)) {
    throw "voltcom_result not found: $voltcomRoot"
}

$masterRows = New-Object System.Collections.Generic.List[object]

Get-ChildItem -Path $voltcomRoot -Directory | ForEach-Object {
    $datasetDir = $_
    $datasetName = $datasetDir.Name

    Get-ChildItem -Path $datasetDir.FullName -File -Filter "summary_*.csv" | Sort-Object Name | ForEach-Object {
        $summaryPath = $_.FullName
        $thresholdTag = $_.BaseName.Substring("summary_".Length)
        $rows = Import-Csv $summaryPath
        if (-not $rows) {
            return
        }

        $processedFiles = @($rows).Count
        $totalInputPoints = [int64](($rows | Measure-Object -Property original_points -Sum).Sum)
        $totalOutputPoints = [int64](($rows | Measure-Object -Property compressed_segments -Sum).Sum)
        $totalCompressUs = [double](($rows | Measure-Object -Property compression_time_us -Sum).Sum)
        $totalDecompressUs = [double](($rows | Measure-Object -Property decompression_time_us -Sum).Sum)
        $overallCompressionRatio = if ($totalInputPoints -gt 0) {
            [Math]::Round(($totalOutputPoints / [double]$totalInputPoints), 12)
        }
        else {
            $null
        }
        $avgFileCompressionRatio = [Math]::Round([double](($rows | Measure-Object -Property compression_ratio -Average).Average), 12)
        $avgAverageErrorM = [Math]::Round([double](($rows | Measure-Object -Property average_error_m -Average).Average), 12)
        $globalMaxErrorM = [Math]::Round([double](($rows | Measure-Object -Property maximum_error_m -Maximum).Maximum), 12)
        $totalTrajectorySegments = [int64](($rows | Measure-Object -Property trajectory_segments -Sum).Sum)
        $compressPps = if ($totalCompressUs -gt 0) {
            [Math]::Round(($totalInputPoints / ($totalCompressUs / 1000000.0)), 6)
        }
        else {
            $null
        }
        $decompressPps = if ($totalDecompressUs -gt 0) {
            [Math]::Round(($totalInputPoints / ($totalDecompressUs / 1000000.0)), 6)
        }
        else {
            $null
        }

        $metricRow = [pscustomobject]@{
            algorithm = "VOLTCom"
            dataset = $datasetName
            threshold = $thresholdTag
            processed_files = $processedFiles
            total_input_points = $totalInputPoints
            total_output_points = $totalOutputPoints
            total_trajectory_segments = $totalTrajectorySegments
            total_compress_time_us = [int64]$totalCompressUs
            total_decompress_time_us = [int64]$totalDecompressUs
            overall_compression_ratio = $overallCompressionRatio
            saved_ratio = if ($overallCompressionRatio -ne $null) { [Math]::Round((1.0 - $overallCompressionRatio), 12) } else { $null }
            avg_file_compression_ratio = $avgFileCompressionRatio
            avg_average_error_m = $avgAverageErrorM
            global_max_error_m = $globalMaxErrorM
            compress_points_per_second = $compressPps
            decompress_points_per_second = $decompressPps
            source_summary_csv = $summaryPath
        }

        $metricsPath = Join-Path $datasetDir.FullName ("metrics_summary_" + $thresholdTag + ".csv")
        Save-SingleRowCsv -Path $metricsPath -Row $metricRow
        $masterRows.Add($metricRow) | Out-Null
    }
}

$masterPath = Join-Path $voltcomRoot "metrics_master_summary.csv"
$masterRows | Export-Csv -Path $masterPath -NoTypeInformation -Encoding UTF8
Write-Output "Saved VOLTCom metrics master summary to: $masterPath"
