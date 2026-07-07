param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\skwcl\TR-TR_result\geolife\converted",
    [string]$DatasetName = "geolife_converted",
    [double]$Epsilon = 1e-5,
    [string]$EpsilonTag = "1e-5",
    [string]$OutputRoot = "",
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
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

function Ensure-OpwTrExecutable {
    param(
        [string]$SourcePath,
        [string]$ExePath
    )

    $needsCompile = -not (Test-Path $ExePath)
    if (-not $needsCompile) {
        $needsCompile = (Get-Item $SourcePath).LastWriteTimeUtc -gt (Get-Item $ExePath).LastWriteTimeUtc
    }

    if ($needsCompile) {
        & g++ $SourcePath -o $ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile OPW_TR.cpp"
        }
    }
}

function Read-InputTrajectory {
    param([string]$Path)

    $validLines = New-Object System.Collections.Generic.List[string]
    foreach ($line in Get-Content $Path) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line.Trim() -split "\s+"
        if ($parts.Length -lt 3) {
            continue
        }

        try {
            $lat = [double]$parts[0]
            $lon = [double]$parts[1]
            $ts = [double]$parts[2]
            $validLines.Add(("{0} {1} {2}" -f $lat, $lon, $ts))
        }
        catch {
            continue
        }
    }

    return $validLines
}

function Read-ResultMetadata {
    param(
        [string]$ResultPath,
        [int]$InputCount
    )

    if (-not (Test-Path $ResultPath)) {
        return $null
    }

    $outputLines = Get-Content $ResultPath -ErrorAction SilentlyContinue
    if ($null -eq $outputLines -or $outputLines.Count -eq 0) {
        return $null
    }

    try {
        $runtimeSeconds = [double]$outputLines[-1]
    }
    catch {
        return $null
    }

    $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
    $compressionRatio = if ($InputCount -gt 0) {
        [Math]::Round($outputPointCount / $InputCount, 6)
    }
    else {
        0
    }

    return [pscustomobject]@{
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        output_points = $outputPointCount
        compression_ratio = $compressionRatio
    }
}

function Invoke-OpwTrWithTimeout {
    param(
        [string]$ExePath,
        [string]$InputPath,
        [double]$Epsilon,
        [string]$ResultPath,
        [int]$TimeoutSeconds
    )

    $process = Start-Process -FilePath $ExePath -ArgumentList @($InputPath, [string]$Epsilon, $ResultPath) -PassThru -WindowStyle Hidden
    $finished = $true
    try {
        Wait-Process -Id $process.Id -Timeout $TimeoutSeconds -ErrorAction Stop
    }
    catch {
        $finished = $false
    }

    if (-not $finished) {
        try {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        catch {
        }
        if (Test-Path $ResultPath) {
            Remove-Item -LiteralPath $ResultPath -Force -ErrorAction SilentlyContinue
        }
        return [pscustomobject]@{
            status = "timeout"
            exit_code = $null
        }
    }

    return [pscustomobject]@{
        status = "completed"
        exit_code = $process.ExitCode
    }
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$safeDatasetName = ($DatasetName -replace "[^A-Za-z0-9_-]", "_")
$targetRoot = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    Join-Path $WorkspaceRoot ("test\OPW-TR_test_{0}_{1}" -f $safeDatasetName, $EpsilonTag)
}
else {
    $OutputRoot
}
$resultsDir = Join-Path $targetRoot "results"
$binDir = Join-Path $targetRoot "bin"
$summaryPath = Join-Path $targetRoot "summary.csv"
$metricsPath = Join-Path $targetRoot "metrics_summary.csv"
$masterInfoPath = Join-Path $targetRoot "run_info.txt"

Ensure-Directory $targetRoot
Ensure-Directory $resultsDir
Ensure-Directory $binDir

$sourcePath = Join-Path $WorkspaceRoot "online\OPW-TR\OPW_TR.cpp"
$exePath = Join-Path $binDir "OPW-TR.exe"
Ensure-OpwTrExecutable -SourcePath $sourcePath -ExePath $exePath

$files = Get-ChildItem $DatasetRoot -Recurse -File -Filter *.plt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]
$skippedTimeout = New-Object System.Collections.Generic.List[string]

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $resultsSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $resultsSubdir

    $inputLines = Read-InputTrajectory -Path $file.FullName
    if ($inputLines.Count -eq 0) {
        continue
    }

    $resultPath = Join-Path $resultsSubdir ($baseName + ".opwtr.csv")

    $resultMeta = Read-ResultMetadata -ResultPath $resultPath -InputCount $inputLines.Count
    if ($null -eq $resultMeta) {
        $invokeResult = Invoke-OpwTrWithTimeout -ExePath $exePath -InputPath $file.FullName -Epsilon $Epsilon -ResultPath $resultPath -TimeoutSeconds $TimeoutSeconds
        if ($invokeResult.status -eq "timeout") {
            $skippedTimeout.Add($relativePath) | Out-Null
            continue
        }
        if ($invokeResult.exit_code -ne 0) {
            throw "OPW-TR failed on $($file.FullName)"
        }

        $resultMeta = Read-ResultMetadata -ResultPath $resultPath -InputCount $inputLines.Count
    }

    if ($null -eq $resultMeta) {
        continue
    }

    $rows.Add([pscustomobject]@{
        algorithm = "OPW-TR"
        dataset = $DatasetName
        epsilon = $EpsilonTag
        relative_file = $relativePath
        input_points = $inputLines.Count
        output_points = $resultMeta.output_points
        compression_ratio = $resultMeta.compression_ratio
        runtime_seconds = $resultMeta.runtime_seconds
        input_path = $file.FullName
        result_path = $resultPath
    }) | Out-Null
}

$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property input_points -Sum
$outputTotals = $rows | Measure-Object -Property output_points -Sum
$runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum
$fileCompressionAverage = ($rows | Measure-Object -Property compression_ratio -Average).Average
$fileRuntimeAverage = ($rows | Measure-Object -Property runtime_seconds -Average).Average
$overallCompressionRatio = if ($inputTotals.Sum -gt 0) {
    [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6)
}
else {
    0
}
$savedRatio = if ($inputTotals.Sum -gt 0) {
    [Math]::Round(1 - ($outputTotals.Sum / $inputTotals.Sum), 6)
}
else {
    0
}
$pointsPerSecond = if ($runtimeTotals.Sum -gt 0) {
    [Math]::Round($inputTotals.Sum / $runtimeTotals.Sum, 3)
}
else {
    0
}
$outputPointsPerSecond = if ($runtimeTotals.Sum -gt 0) {
    [Math]::Round($outputTotals.Sum / $runtimeTotals.Sum, 3)
}
else {
    0
}

[pscustomobject]@{
    algorithm = "OPW-TR"
    dataset = $DatasetName
    epsilon = $EpsilonTag
    processed_files = $rows.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    overall_compression_ratio = $overallCompressionRatio
    saved_ratio = $savedRatio
    avg_file_compression_ratio = [Math]::Round($fileCompressionAverage, 6)
    avg_file_runtime_seconds = [Math]::Round($fileRuntimeAverage, 6)
    input_points_per_second = $pointsPerSecond
    output_points_per_second = $outputPointsPerSecond
    skipped_timeout_files = $skippedTimeout.Count
    metrics_note = "OPW-TR uses SED on raw latitude/longitude coordinates with timestamps"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=OPW-TR"
    ("dataset={0}" -f $DatasetName)
    ("epsilon={0}" -f $EpsilonTag)
    ("processed_files={0}" -f $rows.Count)
    ("total_input_points={0}" -f [int64]$inputTotals.Sum)
    ("total_output_points={0}" -f [int64]$outputTotals.Sum)
    ("overall_compression_ratio={0}" -f $overallCompressionRatio)
    ("saved_ratio={0}" -f $savedRatio)
    ("total_runtime_seconds={0}" -f [Math]::Round($runtimeTotals.Sum, 6))
    ("avg_file_compression_ratio={0}" -f [Math]::Round($fileCompressionAverage, 6))
    ("avg_file_runtime_seconds={0}" -f [Math]::Round($fileRuntimeAverage, 6))
    ("input_points_per_second={0}" -f $pointsPerSecond)
    ("output_points_per_second={0}" -f $outputPointsPerSecond)
    ("skipped_timeout_files={0}" -f $skippedTimeout.Count)
    ("timeout_seconds={0}" -f $TimeoutSeconds)
    "epsilon_unit=raw_coordinate_degrees"
    "distance_unit_note=OPW-TR uses time-synchronized Euclidean distance on latitude/longitude coordinates, not meters"
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
    ("skipped_timeout_list={0}" -f ([string]::Join(';', $skippedTimeout)))
) | Set-Content -Path $masterInfoPath

[pscustomobject]@{
    algorithm = "OPW-TR"
    dataset = $DatasetName
    epsilon = $EpsilonTag
    processed_files = $rows.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    overall_compression_ratio = $overallCompressionRatio
    input_points_per_second = $pointsPerSecond
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    summary_csv = $summaryPath
    metrics_csv = $metricsPath
    info_file = $masterInfoPath
} | Format-List
