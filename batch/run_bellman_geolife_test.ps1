param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\data\Geolife",
    [double]$CompressionRatio = 0.5,
    [string]$RatioTag = "0.5",
    [int]$MaxPoints = 500
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Convert-ToUnixSeconds {
    param([string]$DateText)

    $dt = [datetime]::ParseExact($DateText, "yyyy-MM-dd HH:mm:ss", [System.Globalization.CultureInfo]::InvariantCulture)
    return [int64][DateTimeOffset]::new($dt).ToUnixTimeSeconds()
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

function Ensure-BellmanExecutable {
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
            throw "Failed to compile Bellman C++ port"
        }
    }
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$testRoot = Join-Path $WorkspaceRoot ("test\Bellman_test_Geolife_r{0}_le{1}" -f $RatioTag, $MaxPoints)
$convertedDir = Join-Path $testRoot "converted"
$resultsDir = Join-Path $testRoot "results"
$binDir = Join-Path $testRoot "bin"
$summaryPath = Join-Path $testRoot "bellman_geolife_summary.csv"
$metricsPath = Join-Path $testRoot "bellman_geolife_metrics_summary.csv"
$infoPath = Join-Path $testRoot "run_info.txt"

Ensure-Directory $testRoot
Ensure-Directory $convertedDir
Ensure-Directory $resultsDir
Ensure-Directory $binDir

$sourcePath = Join-Path $WorkspaceRoot "batch\Bellman_cpp_port\main.cpp"
$exePath = Join-Path $binDir "bellman.exe"
$metricsScriptPath = Join-Path $WorkspaceRoot "online\evaluate_simplification_metrics.py"
Ensure-BellmanExecutable -SourcePath $sourcePath -ExePath $exePath

$files = Get-ChildItem $DatasetRoot -Recurse -File -Filter *.plt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $convertedSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $convertedDir } else { Join-Path $convertedDir $relativeDir }
    $resultsSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $convertedSubdir
    Ensure-Directory $resultsSubdir

    $convertedPath = Join-Path $convertedSubdir ($baseName + ".bellman_input.plt")
    $resultPath = Join-Path $resultsSubdir ($baseName + ".bellman_output.csv")

    $convertedLines = New-Object System.Collections.Generic.List[string]
    $lineIndex = 0
    foreach ($line in Get-Content $file.FullName) {
        $lineIndex += 1
        if ($lineIndex -le 6 -or [string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line.Split(",")
        if ($parts.Length -lt 7) {
            continue
        }

        try {
            $lat = [double]$parts[0]
            $lon = [double]$parts[1]
            $epoch = Convert-ToUnixSeconds (($parts[5].Trim() + " " + $parts[6].Trim()))
            $convertedLines.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
        }
        catch {
            continue
        }
    }

    if ($convertedLines.Count -lt 2 -or $convertedLines.Count -gt $MaxPoints) {
        continue
    }

    Set-Content -Path $convertedPath -Value $convertedLines

    $keptPoints = [Math]::Max(2, [Math]::Min($convertedLines.Count, [int][Math]::Round($convertedLines.Count * $CompressionRatio)))

    & $exePath $convertedPath $keptPoints $resultPath
    if ($LASTEXITCODE -ne 0) {
        throw "Bellman failed on $($file.FullName)"
    }

    $outputLines = Get-Content $resultPath
    if ($outputLines.Count -lt 2) {
        continue
    }

    $runtimeLine = $outputLines | Where-Object { $_ -like '# runtime_seconds=*' } | Select-Object -Last 1
    $rmseLine = $outputLines | Where-Object { $_ -like '# rmse=*' } | Select-Object -Last 1
    $pointLines = $outputLines | Where-Object { $_ -notlike '# *' }

    $runtimeSeconds = if ($runtimeLine) { [double]($runtimeLine -replace '^# runtime_seconds=', '') } else { 0.0 }
    $rmse = if ($rmseLine) { [double]($rmseLine -replace '^# rmse=', '') } else { 0.0 }
    $outputPointCount = $pointLines.Count
    $compressionRatio = if ($convertedLines.Count -gt 0) {
        [Math]::Round($outputPointCount / $convertedLines.Count, 6)
    } else {
        0
    }
    $errorMetricsJson = & py -3 $metricsScriptPath $convertedPath $resultPath
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to evaluate Bellman PED/SED errors on $($file.FullName)"
    }
    $errorMetrics = $errorMetricsJson | ConvertFrom-Json

    $rows.Add([pscustomobject]@{
        algorithm = "Bellman"
        dataset = "Geolife_subset"
        compression_ratio_param = $RatioTag
        num_kept_points = $keptPoints
        max_points = $MaxPoints
        relative_file = $relativePath
        input_points = $convertedLines.Count
        output_points = $outputPointCount
        compression_ratio = $compressionRatio
        rmse = [Math]::Round($rmse, 6)
        max_sed_m = [Math]::Round([double]$errorMetrics.max_sed_m, 6)
        mean_sed_m = [Math]::Round([double]$errorMetrics.mean_sed_m, 6)
        max_ped_m = [Math]::Round([double]$errorMetrics.max_ped_m, 6)
        mean_ped_m = [Math]::Round([double]$errorMetrics.mean_ped_m, 6)
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        converted_path = $convertedPath
        result_path = $resultPath
    }) | Out-Null
}

$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property input_points -Sum
$outputTotals = $rows | Measure-Object -Property output_points -Sum
$runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum
$rmseAverage = ($rows | Measure-Object -Property rmse -Average).Average
$fileCompressionAverage = ($rows | Measure-Object -Property compression_ratio -Average).Average
$fileRuntimeAverage = ($rows | Measure-Object -Property runtime_seconds -Average).Average
$avgFileMeanPed = ($rows | Measure-Object -Property mean_ped_m -Average).Average
$avgFileMeanSed = ($rows | Measure-Object -Property mean_sed_m -Average).Average
$overallCompressionRatio = if ($inputTotals.Sum -gt 0) {
    [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6)
} else {
    0
}
$savedRatio = if ($inputTotals.Sum -gt 0) {
    [Math]::Round(1 - ($outputTotals.Sum / $inputTotals.Sum), 6)
} else {
    0
}
$inputPointsPerSecond = if ($runtimeTotals.Sum -gt 0) {
    [Math]::Round($inputTotals.Sum / $runtimeTotals.Sum, 3)
} else {
    0
}
$outputPointsPerSecond = if ($runtimeTotals.Sum -gt 0) {
    [Math]::Round($outputTotals.Sum / $runtimeTotals.Sum, 3)
} else {
    0
}

[pscustomobject]@{
    algorithm = "Bellman"
    dataset = "Geolife_subset"
    compression_ratio_param = $RatioTag
    max_points = $MaxPoints
    processed_files = $rows.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    overall_compression_ratio = $overallCompressionRatio
    saved_ratio = $savedRatio
    avg_file_compression_ratio = [Math]::Round($fileCompressionAverage, 6)
    avg_file_rmse = [Math]::Round($rmseAverage, 6)
    avg_trajectory_ped_m = [Math]::Round($avgFileMeanPed, 6)
    avg_trajectory_sed_m = [Math]::Round($avgFileMeanSed, 6)
    avg_file_runtime_seconds = [Math]::Round($fileRuntimeAverage, 6)
    input_points_per_second = $inputPointsPerSecond
    output_points_per_second = $outputPointsPerSecond
    metrics_note = "Bellman DP reproduction keeps a fixed number of points and optimizes RMS error; subset run only"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=Bellman"
    "dataset=Geolife_subset"
    ("compression_ratio_param={0}" -f $RatioTag)
    ("max_points={0}" -f $MaxPoints)
    ("processed_files={0}" -f $rows.Count)
    ("total_input_points={0}" -f [int64]$inputTotals.Sum)
    ("total_output_points={0}" -f [int64]$outputTotals.Sum)
    ("overall_compression_ratio={0}" -f $overallCompressionRatio)
    ("saved_ratio={0}" -f $savedRatio)
    ("avg_file_rmse={0}" -f [Math]::Round($rmseAverage, 6))
    ("avg_trajectory_ped_m={0}" -f [Math]::Round($avgFileMeanPed, 6))
    ("avg_trajectory_sed_m={0}" -f [Math]::Round($avgFileMeanSed, 6))
    ("total_runtime_seconds={0}" -f [Math]::Round($runtimeTotals.Sum, 6))
    ("avg_file_compression_ratio={0}" -f [Math]::Round($fileCompressionAverage, 6))
    ("avg_file_runtime_seconds={0}" -f [Math]::Round($fileRuntimeAverage, 6))
    ("input_points_per_second={0}" -f $inputPointsPerSecond)
    ("output_points_per_second={0}" -f $outputPointsPerSecond)
    "subset_note=exact Bellman-style dynamic programming is expensive, so the batch script limits trajectories by max_points"
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
) | Set-Content -Path $infoPath

[pscustomobject]@{
    algorithm = "Bellman"
    dataset = "Geolife_subset"
    compression_ratio_param = $RatioTag
    max_points = $MaxPoints
    processed_files = $rows.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    overall_compression_ratio = $overallCompressionRatio
    avg_file_rmse = [Math]::Round($rmseAverage, 6)
    avg_trajectory_ped_m = [Math]::Round($avgFileMeanPed, 6)
    input_points_per_second = $inputPointsPerSecond
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    summary_csv = $summaryPath
    metrics_csv = $metricsPath
    info_file = $infoPath
} | Format-List
