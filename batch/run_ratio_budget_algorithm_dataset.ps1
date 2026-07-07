param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [ValidateSet("Bellman", "SQUISH", "STTrace")]
    [string]$Algorithm,
    [string]$DatasetName,
    [string]$DatasetRoot,
    [string]$AlgorithmRoot,
    [double]$CompressionRatio,
    [string]$RatioTag,
    [string]$PythonExe = "py",
    [string]$SharedConvertedRoot = "",
    [switch]$Resume,
    [int]$CompressionTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Convert-ToUnixSeconds {
    param([string]$DateText)

    $dt = [datetime]::Parse($DateText, [System.Globalization.CultureInfo]::InvariantCulture)
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

function Get-LineCount {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return 0
    }

    return (Get-Content $Path | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count
}

function Append-CheckpointRow {
    param(
        [string]$Path,
        [psobject]$Row
    )

    if (Test-Path $Path) {
        $Row | Export-Csv -Path $Path -NoTypeInformation -Encoding UTF8 -Append
    }
    else {
        $Row | Export-Csv -Path $Path -NoTypeInformation -Encoding UTF8
    }
}

function Ensure-AlgorithmExecutable {
    param(
        [string]$Algorithm,
        [string]$WorkspaceRoot,
        [string]$BinDir
    )

    Ensure-Directory $BinDir

    switch ($Algorithm) {
        "Bellman" {
            $sourcePath = Join-Path $WorkspaceRoot "batch\Bellman_cpp_port\main.cpp"
            $exePath = Join-Path $BinDir "bellman.exe"
            $compileArgs = @($sourcePath, "-O2", "-std=c++17", "-o", $exePath)
        }
        "SQUISH" {
            $sourcePath = Join-Path $WorkspaceRoot "online\SQUISH\SQUISH.cpp"
            $exePath = Join-Path $BinDir "SQUISH.exe"
            $compileArgs = @($sourcePath, "-O2", "-std=c++11", "-o", $exePath)
        }
        "STTrace" {
            $sourcePath = Join-Path $WorkspaceRoot "online\STTrace\STTrace.cpp"
            $exePath = Join-Path $BinDir "STTrace.exe"
            $compileArgs = @($sourcePath, "-O2", "-std=c++11", "-o", $exePath)
        }
        default {
            throw "Unsupported algorithm: $Algorithm"
        }
    }

    $needsCompile = -not (Test-Path $exePath)
    if (-not $needsCompile) {
        $needsCompile = (Get-Item $sourcePath).LastWriteTimeUtc -gt (Get-Item $exePath).LastWriteTimeUtc
    }

    if ($needsCompile) {
        & g++ @compileArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile $Algorithm"
        }
    }

    return $exePath
}

function Convert-InputFile {
    param(
        [string]$DatasetName,
        [string]$SourcePath,
        [string]$DestinationPath
    )

    $convertedLines = New-Object System.Collections.Generic.List[string]
    $lineIndex = 0

    foreach ($line in Get-Content $SourcePath) {
        $lineIndex += 1
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        switch ($DatasetName) {
            "Geolife" {
                if ($lineIndex -le 6) {
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
            "T-drive" {
                $parts = $line.Split(",")
                if ($parts.Length -lt 4) {
                    continue
                }
                try {
                    $lon = [double]$parts[2]
                    $lat = [double]$parts[3]
                    $epoch = Convert-ToUnixSeconds $parts[1].Trim()
                    $convertedLines.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
                }
                catch {
                    continue
                }
            }
            default {
                if ($lineIndex -eq 1) {
                    continue
                }
                $parts = $line.Split(",")
                if ($parts.Length -lt 3) {
                    continue
                }
                try {
                    $lon = [double]$parts[1]
                    $lat = [double]$parts[2]
                    $epoch = Convert-ToUnixSeconds $parts[0].Trim()
                    $convertedLines.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
                }
                catch {
                    continue
                }
            }
        }
    }

    if ($convertedLines.Count -lt 2) {
        return 0
    }

    Set-Content -Path $DestinationPath -Value $convertedLines
    return $convertedLines.Count
}

function Get-LegacyConvertedCandidate {
    param(
        [string]$WorkspaceRoot,
        [string]$DatasetName,
        [string]$RelativePath
    )

    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($RelativePath)
    $relativeDir = [System.IO.Path]::GetDirectoryName($RelativePath)
    $ratios = @("0.5", "0.2", "0.1", "0.05", "0.02")
    $algorithmDirs = @("bellman_result", "SQUISH_result", "STTrace_result")

    foreach ($algorithmDir in $algorithmDirs) {
        foreach ($ratio in $ratios) {
            $candidateDir = Join-Path $WorkspaceRoot ("skwcl\{0}\{1}\{2}\converted" -f $algorithmDir, $DatasetName, $ratio)
            if (-not [string]::IsNullOrEmpty($relativeDir)) {
                $candidateDir = Join-Path $candidateDir $relativeDir
            }
            $candidatePath = Join-Path $candidateDir ($baseName + ".normalized.plt")
            if (Test-Path $candidatePath) {
                return $candidatePath
            }
        }
    }

    return $null
}

function Ensure-SharedConvertedFile {
    param(
        [string]$WorkspaceRoot,
        [string]$DatasetName,
        [string]$SourcePath,
        [string]$DatasetRoot,
        [string]$SharedConvertedRoot
    )

    $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $SourcePath
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($SourcePath)
    $sharedSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $SharedConvertedRoot } else { Join-Path $SharedConvertedRoot $relativeDir }
    Ensure-Directory $sharedSubdir

    $sharedPath = Join-Path $sharedSubdir ($baseName + ".normalized.plt")
    $existingCount = Get-LineCount -Path $sharedPath
    if ($existingCount -ge 2) {
        return @{
            Path = $sharedPath
            PointCount = $existingCount
            Source = "shared_cache"
        }
    }

    $legacyPath = Get-LegacyConvertedCandidate -WorkspaceRoot $WorkspaceRoot -DatasetName $DatasetName -RelativePath $relativePath
    if ($legacyPath) {
        Copy-Item -Path $legacyPath -Destination $sharedPath -Force
        $legacyCount = Get-LineCount -Path $sharedPath
        if ($legacyCount -ge 2) {
            return @{
                Path = $sharedPath
                PointCount = $legacyCount
                Source = "legacy_cache"
            }
        }
    }

    $tempPath = "{0}.{1}.tmp" -f $sharedPath, [System.Guid]::NewGuid().ToString("N")
    $pointCount = Convert-InputFile -DatasetName $DatasetName -SourcePath $SourcePath -DestinationPath $tempPath
    if ($pointCount -ge 2) {
        if (-not (Test-Path $sharedPath) -or (Get-LineCount -Path $sharedPath) -lt 2) {
            Move-Item -Path $tempPath -Destination $sharedPath -Force
        }
        else {
            Remove-Item -Path $tempPath -Force -ErrorAction SilentlyContinue
        }
        $pointCount = Get-LineCount -Path $sharedPath
    }
    else {
        Remove-Item -Path $tempPath -Force -ErrorAction SilentlyContinue
    }
    return @{
        Path = $sharedPath
        PointCount = $pointCount
        Source = "fresh_conversion"
    }
}

function Invoke-Algorithm {
    param(
        [string]$Algorithm,
        [string]$ExePath,
        [string]$InputPath,
        [int]$InputPointCount,
        [double]$CompressionRatio,
        [string]$OutputPath,
        [int]$CompressionTimeoutSeconds
    )

    if ($Algorithm -eq "Bellman" -and $CompressionTimeoutSeconds -gt 0) {
        $keptPoints = [Math]::Max(2, [Math]::Min($InputPointCount, [int][Math]::Round($InputPointCount * $CompressionRatio)))
        $argumentList = @($InputPath, [string]$keptPoints, $OutputPath)
        $process = Start-Process -FilePath $ExePath -ArgumentList $argumentList -PassThru -WindowStyle Hidden
        if (-not $process.WaitForExit($CompressionTimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            if (Test-Path $OutputPath) {
                Remove-Item -Path $OutputPath -Force -ErrorAction SilentlyContinue
            }
            throw ("Bellman compression exceeded {0}s and was skipped" -f $CompressionTimeoutSeconds)
        }
        if ($process.ExitCode -ne 0) {
            throw "$Algorithm failed on $InputPath"
        }
        return
    }

    switch ($Algorithm) {
        "Bellman" {
            $keptPoints = [Math]::Max(2, [Math]::Min($InputPointCount, [int][Math]::Round($InputPointCount * $CompressionRatio)))
            & $ExePath $InputPath $keptPoints $OutputPath
        }
        "SQUISH" {
            & $ExePath $InputPath $CompressionRatio $OutputPath
        }
        "STTrace" {
            & $ExePath $InputPath $CompressionRatio $OutputPath
        }
        default {
            throw "Unsupported algorithm: $Algorithm"
        }
    }

    if ($LASTEXITCODE -ne 0) {
        throw "$Algorithm failed on $InputPath"
    }
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

if ([string]::IsNullOrWhiteSpace($SharedConvertedRoot)) {
    $SharedConvertedRoot = Join-Path $WorkspaceRoot ("skwcl\_shared_converted\{0}" -f $DatasetName)
}

$thresholdRoot = Join-Path $AlgorithmRoot $RatioTag
$resultsDir = Join-Path $thresholdRoot "results"
$binDir = Join-Path $AlgorithmRoot "_tooling"
$summaryPath = Join-Path $thresholdRoot "summary.csv"
$metricsPath = Join-Path $thresholdRoot "metrics_summary.csv"
$infoPath = Join-Path $thresholdRoot "run_info.txt"
$failuresPath = Join-Path $thresholdRoot "failed_files.csv"
$checkpointPath = Join-Path $thresholdRoot "checkpoint_state.csv"
$metricsScriptPath = Join-Path $WorkspaceRoot "online\evaluate_simplification_metrics.py"

Ensure-Directory $AlgorithmRoot
Ensure-Directory $thresholdRoot
Ensure-Directory $resultsDir
Ensure-Directory $SharedConvertedRoot

$exePath = Ensure-AlgorithmExecutable -Algorithm $Algorithm -WorkspaceRoot $WorkspaceRoot -BinDir $binDir
$files = Get-ChildItem $DatasetRoot -Recurse -File | Where-Object { $_.Extension -in @(".plt", ".txt") } | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $resultsSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $resultsSubdir

    $resultPath = Join-Path $resultsSubdir ("{0}.{1}.csv" -f $baseName, $Algorithm.ToLowerInvariant())
    $conversion = $null
    $convertedPath = ""

    try {
        $conversion = Ensure-SharedConvertedFile -WorkspaceRoot $WorkspaceRoot -DatasetName $DatasetName -SourcePath $file.FullName -DatasetRoot $DatasetRoot -SharedConvertedRoot $SharedConvertedRoot
        $convertedPath = $conversion.Path
        $inputPointCount = $conversion.PointCount
        if ($inputPointCount -lt 2) {
            throw "Too few valid points after conversion"
        }

        $usedResumeHit = $false
        if (-not (Test-Path $resultPath) -or (Get-Item $resultPath).Length -eq 0) {
            Invoke-Algorithm -Algorithm $Algorithm -ExePath $exePath -InputPath $convertedPath -InputPointCount $inputPointCount -CompressionRatio $CompressionRatio -OutputPath $resultPath -CompressionTimeoutSeconds $CompressionTimeoutSeconds
        }
        else {
            $usedResumeHit = $Resume.IsPresent
        }

        $outputLines = Get-Content $resultPath
        if ($outputLines.Count -eq 0) {
            throw "Empty result file"
        }

        if ($Algorithm -eq "Bellman") {
            $runtimeLine = $outputLines | Where-Object { $_ -like '# runtime_seconds=*' } | Select-Object -Last 1
            $rmseLine = $outputLines | Where-Object { $_ -like '# rmse=*' } | Select-Object -Last 1
            $pointLines = $outputLines | Where-Object { $_ -notlike '# *' }
            $runtimeSeconds = if ($runtimeLine) { [double]($runtimeLine -replace '^# runtime_seconds=', '') } else { 0.0 }
            $rmse = if ($rmseLine) { [double]($rmseLine -replace '^# rmse=', '') } else { 0.0 }
            $outputPointCount = $pointLines.Count
        }
        else {
            $runtimeSeconds = [double]$outputLines[-1]
            $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
            $rmse = $null
        }

        $compressionRatioObserved = [Math]::Round($outputPointCount / $inputPointCount, 6)
        $errorMetricsJson = & $PythonExe -3 $metricsScriptPath $convertedPath $resultPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to evaluate simplification metrics"
        }
        $errorMetrics = $errorMetricsJson | ConvertFrom-Json

        $rows.Add([pscustomobject]@{
            algorithm = $Algorithm
            dataset = $DatasetName
            compression_ratio_param = $RatioTag
            relative_file = $relativePath
            input_points = $inputPointCount
            output_points = $outputPointCount
            compression_ratio = $compressionRatioObserved
            rmse = $(if ($null -ne $rmse) { [Math]::Round([double]$rmse, 6) } else { $null })
            max_sed_m = [Math]::Round([double]$errorMetrics.max_sed_m, 6)
            mean_sed_m = [Math]::Round([double]$errorMetrics.mean_sed_m, 6)
            max_ped_m = [Math]::Round([double]$errorMetrics.max_ped_m, 6)
            mean_ped_m = [Math]::Round([double]$errorMetrics.mean_ped_m, 6)
            runtime_seconds = [Math]::Round($runtimeSeconds, 6)
            converted_path = $convertedPath
            converted_source = $conversion.Source
            resumed_from_existing_result = $usedResumeHit
            result_path = $resultPath
        }) | Out-Null

        Append-CheckpointRow -Path $checkpointPath -Row ([pscustomobject]@{
            relative_file = $relativePath
            status = "completed"
            resumed_from_existing_result = $usedResumeHit
            converted_source = $conversion.Source
            result_path = $resultPath
        })
    }
    catch {
        $failures.Add([pscustomobject]@{
            algorithm = $Algorithm
            dataset = $DatasetName
            compression_ratio_param = $RatioTag
            relative_file = $relativePath
            input_path = $file.FullName
            converted_path = $convertedPath
            result_path = $resultPath
            error_message = $_.Exception.Message
        }) | Out-Null
        Append-CheckpointRow -Path $checkpointPath -Row ([pscustomobject]@{
            relative_file = $relativePath
            status = "failed"
            resumed_from_existing_result = $false
            converted_source = $(if ($conversion) { $conversion.Source } else { "" })
            result_path = $resultPath
        })
        continue
    }
}

$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8
$failures | Export-Csv -Path $failuresPath -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property input_points -Sum
$outputTotals = $rows | Measure-Object -Property output_points -Sum
$runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum
$fileCompressionAverage = ($rows | Measure-Object -Property compression_ratio -Average).Average
$fileRuntimeAverage = ($rows | Measure-Object -Property runtime_seconds -Average).Average
$avgFileMeanSed = ($rows | Measure-Object -Property mean_sed_m -Average).Average
$avgFileMeanPed = ($rows | Measure-Object -Property mean_ped_m -Average).Average
$avgFileRmse = ($rows | Where-Object { $null -ne $_.rmse } | Measure-Object -Property rmse -Average).Average
$globalMaxSed = ($rows | Measure-Object -Property max_sed_m -Maximum).Maximum
$globalMaxPed = ($rows | Measure-Object -Property max_ped_m -Maximum).Maximum
$weightedSedNumerator = 0.0
$weightedPedNumerator = 0.0
foreach ($row in $rows) {
    $weightedSedNumerator += [double]$row.mean_sed_m * [double]$row.input_points
    $weightedPedNumerator += [double]$row.mean_ped_m * [double]$row.input_points
}
$overallMeanSed = if ($inputTotals.Sum -gt 0) { [Math]::Round($weightedSedNumerator / $inputTotals.Sum, 6) } else { 0 }
$overallMeanPed = if ($inputTotals.Sum -gt 0) { [Math]::Round($weightedPedNumerator / $inputTotals.Sum, 6) } else { 0 }
$overallCompressionRatio = if ($inputTotals.Sum -gt 0) { [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6) } else { 0 }
$savedRatio = if ($inputTotals.Sum -gt 0) { [Math]::Round(1 - ($outputTotals.Sum / $inputTotals.Sum), 6) } else { 0 }
$inputPointsPerSecond = if ($runtimeTotals.Sum -gt 0) { [Math]::Round($inputTotals.Sum / $runtimeTotals.Sum, 3) } else { 0 }
$outputPointsPerSecond = if ($runtimeTotals.Sum -gt 0) { [Math]::Round($outputTotals.Sum / $runtimeTotals.Sum, 3) } else { 0 }
$resumeHits = ($rows | Where-Object { $_.resumed_from_existing_result } | Measure-Object).Count

[pscustomobject]@{
    algorithm = $Algorithm
    dataset = $DatasetName
    compression_ratio_param = $RatioTag
    processed_files = $rows.Count
    failed_files = $failures.Count
    resumed_existing_results = $resumeHits
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    overall_compression_ratio = $overallCompressionRatio
    saved_ratio = $savedRatio
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    avg_file_compression_ratio = [Math]::Round($fileCompressionAverage, 6)
    avg_file_runtime_seconds = [Math]::Round($fileRuntimeAverage, 6)
    overall_mean_sed_m = $overallMeanSed
    overall_mean_ped_m = $overallMeanPed
    avg_file_mean_sed_m = [Math]::Round($avgFileMeanSed, 6)
    avg_file_mean_ped_m = [Math]::Round($avgFileMeanPed, 6)
    avg_file_rmse = $(if ($null -ne $avgFileRmse) { [Math]::Round($avgFileRmse, 6) } else { $null })
    global_max_sed_m = [Math]::Round($globalMaxSed, 6)
    global_max_ped_m = [Math]::Round($globalMaxPed, 6)
    input_points_per_second = $inputPointsPerSecond
    output_points_per_second = $outputPointsPerSecond
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    ("algorithm={0}" -f $Algorithm)
    ("dataset={0}" -f $DatasetName)
    ("compression_ratio_param={0}" -f $RatioTag)
    ("processed_files={0}" -f $rows.Count)
    ("failed_files={0}" -f $failures.Count)
    ("resumed_existing_results={0}" -f $resumeHits)
    ("total_input_points={0}" -f [int64]$inputTotals.Sum)
    ("total_output_points={0}" -f [int64]$outputTotals.Sum)
    ("overall_compression_ratio={0}" -f $overallCompressionRatio)
    ("saved_ratio={0}" -f $savedRatio)
    ("total_runtime_seconds={0}" -f [Math]::Round($runtimeTotals.Sum, 6))
    ("avg_file_compression_ratio={0}" -f [Math]::Round($fileCompressionAverage, 6))
    ("avg_file_runtime_seconds={0}" -f [Math]::Round($fileRuntimeAverage, 6))
    ("overall_mean_sed_m={0}" -f $overallMeanSed)
    ("overall_mean_ped_m={0}" -f $overallMeanPed)
    ("avg_file_mean_sed_m={0}" -f [Math]::Round($avgFileMeanSed, 6))
    ("avg_file_mean_ped_m={0}" -f [Math]::Round($avgFileMeanPed, 6))
    ("avg_file_rmse={0}" -f $(if ($null -ne $avgFileRmse) { [Math]::Round($avgFileRmse, 6) } else { "n/a" }))
    ("global_max_sed_m={0}" -f [Math]::Round($globalMaxSed, 6))
    ("global_max_ped_m={0}" -f [Math]::Round($globalMaxPed, 6))
    ("input_points_per_second={0}" -f $inputPointsPerSecond)
    ("output_points_per_second={0}" -f $outputPointsPerSecond)
    ("shared_converted_root={0}" -f $SharedConvertedRoot)
    ("resume_mode={0}" -f $Resume.IsPresent)
    ("compression_timeout_seconds={0}" -f $CompressionTimeoutSeconds)
    ("input_root={0}" -f $DatasetRoot)
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
    ("failures_csv={0}" -f $failuresPath)
    ("checkpoint_csv={0}" -f $checkpointPath)
) | Set-Content -Path $infoPath

[pscustomobject]@{
    algorithm = $Algorithm
    dataset = $DatasetName
    compression_ratio_param = $RatioTag
    processed_files = $rows.Count
    failed_files = $failures.Count
    resumed_existing_results = $resumeHits
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    summary_csv = $summaryPath
    metrics_csv = $metricsPath
    info_file = $infoPath
} | Format-List
