param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PreparedInputRoot,
    [string]$DatasetName,
    [double]$Epsilon,
    [string]$ThresholdLabel,
    [int]$AlgOpt,
    [string]$AlgorithmLabel,
    [string]$OutputRoot
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
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

function Ensure-DptsExecutable {
    param(
        [string]$WorkspaceRoot,
        [string]$ExePath
    )

    $sourceDir = Join-Path $WorkspaceRoot "batch\DPTS\src"
    $sources = Get-ChildItem -Path $sourceDir -Filter *.cpp | Sort-Object FullName
    $needsCompile = -not (Test-Path $ExePath)
    if (-not $needsCompile) {
        $exeTime = (Get-Item $ExePath).LastWriteTimeUtc
        foreach ($source in $sources) {
            if ($source.LastWriteTimeUtc -gt $exeTime) {
                $needsCompile = $true
                break
            }
        }
    }

    if ($needsCompile) {
        Ensure-Directory -Path (Split-Path -Parent $ExePath)
        & g++ -g $sources.FullName -o $ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile DPTS"
        }
    }
}

function Get-PreparedLineCount {
    param([string]$FilePath)

    $count = 0
    foreach ($line in Get-Content -Path $FilePath) {
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            $count += 1
        }
    }
    return $count
}

function Read-StatLines {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return $null
    }

    $lines = Get-Content -Path $Path
    if ($lines.Count -lt 5) {
        return $null
    }
    return $lines
}

if (-not (Test-Path $PreparedInputRoot)) {
    throw "Prepared input root not found: $PreparedInputRoot"
}

$datasetInputRoot = Join-Path $PreparedInputRoot $DatasetName
if (-not (Test-Path $datasetInputRoot)) {
    throw "Prepared dataset input root not found: $datasetInputRoot"
}

$algoRoot = Join-Path $OutputRoot $DatasetName
$thresholdRoot = Join-Path $algoRoot $ThresholdLabel
$resultsRoot = Join-Path $thresholdRoot "results"
$summaryPath = Join-Path $thresholdRoot "summary.csv"
$metricsPath = Join-Path $thresholdRoot "metrics_summary.csv"
$runInfoPath = Join-Path $thresholdRoot "run_info.txt"
$skippedPath = Join-Path $thresholdRoot "skipped.csv"

Ensure-Directory -Path $thresholdRoot
Ensure-Directory -Path $resultsRoot

$exePath = Join-Path $WorkspaceRoot "batch\DPTS\bin\DPTS.exe"
Ensure-DptsExecutable -WorkspaceRoot $WorkspaceRoot -ExePath $exePath

$files = Get-ChildItem -Path $datasetInputRoot -Recurse -File -Filter *.txt | Sort-Object FullName
if (-not $files) {
    throw "No prepared input files found under $datasetInputRoot"
}

$summaryRows = New-Object System.Collections.Generic.List[object]
$skippedRows = New-Object System.Collections.Generic.List[object]

$totalInputPoints = [int64]0
$totalOutputPoints = [int64]0
$totalRuntimeSeconds = 0.0
$maxMemoryMb = 0.0
$resumedFiles = 0
$processedFiles = 0
$skippedFiles = 0
$startedAt = Get-Date

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $datasetInputRoot -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $resultSubdir = if ([string]::IsNullOrWhiteSpace($relativeDir)) { $resultsRoot } else { Join-Path $resultsRoot $relativeDir }
    Ensure-Directory -Path $resultSubdir
    $fileRunDir = Join-Path $resultSubdir $baseName
    Ensure-Directory -Path $fileRunDir

    $configPath = Join-Path $fileRunDir "config.txt"
    $statPath = Join-Path $fileRunDir "stat.txt"
    $lineCount = Get-PreparedLineCount -FilePath $file.FullName

    @(
        ("{0} {1}" -f ($file.FullName -replace "\\", "/"), $lineCount)
        ("{0} {1}" -f $Epsilon.ToString([System.Globalization.CultureInfo]::InvariantCulture), $AlgOpt)
        "2"
    ) | Set-Content -Path $configPath

    $statLines = Read-StatLines -Path $statPath
    $runtimeSeconds = $null
    $status = "completed"

    if ($null -eq $statLines) {
        Push-Location $fileRunDir
        try {
            $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
            & $exePath
            $stopwatch.Stop()
            if ($LASTEXITCODE -ne 0) {
                throw "DPTS failed for $($file.FullName)"
            }
            $statLines = Read-StatLines -Path $statPath
            if ($null -eq $statLines) {
                throw "Missing or invalid stat.txt for $($file.FullName)"
            }
            $runtimeSeconds = $stopwatch.Elapsed.TotalSeconds
            $processedFiles += 1
        }
        catch {
            $skippedFiles += 1
            $skippedRows.Add([pscustomobject]@{
                relative_file = $relativePath
                reason = $_.Exception.Message
            }) | Out-Null
            $status = "failed"
        }
        finally {
            Pop-Location
        }
    }
    else {
        $resumedFiles += 1
    }

    if ($status -ne "completed") {
        continue
    }

    $inputPoints = [int]$statLines[0]
    $outputPoints = [int]$statLines[1]
    $rawStatCompressionValue = [double]$statLines[2]
    $memoryMb = [double]$statLines[4]
    $compressedFraction = if ($inputPoints -gt 0) { $outputPoints / $inputPoints } else { 0.0 }
    $spaceSavingFraction = if ($inputPoints -gt 0) { 1.0 - ($outputPoints / $inputPoints) } else { 0.0 }

    if ($null -eq $runtimeSeconds) {
        $runtimeSeconds = 0.0
    }

    if ($memoryMb -gt $maxMemoryMb) {
        $maxMemoryMb = $memoryMb
    }

    $totalInputPoints += $inputPoints
    $totalOutputPoints += $outputPoints
    $totalRuntimeSeconds += $runtimeSeconds

    $summaryRows.Add([pscustomobject]@{
        algorithm = $AlgorithmLabel
        dataset = $DatasetName
        epsilon = $ThresholdLabel
        alg_opt = $AlgOpt
        relative_file = $relativePath
        input_points = $inputPoints
        output_points = $outputPoints
        compression_ratio = [Math]::Round($spaceSavingFraction, 6)
        compressed_fraction = [Math]::Round($compressedFraction, 6)
        raw_stat_compression_value = [Math]::Round($rawStatCompressionValue, 6)
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        memory_mb = [Math]::Round($memoryMb, 6)
        stat_path = $statPath
    }) | Out-Null
}

$summaryRows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8
if ($skippedRows.Count -gt 0) {
    $skippedRows | Export-Csv -Path $skippedPath -NoTypeInformation -Encoding UTF8
}

$completedFileCount = $summaryRows.Count
$overallCompressionRatio = if ($totalInputPoints -gt 0) {
    [Math]::Round($totalOutputPoints / $totalInputPoints, 6)
} else { 0 }
$spaceSavingRatio = if ($totalInputPoints -gt 0) {
    [Math]::Round(1 - ($totalOutputPoints / $totalInputPoints), 6)
} else { 0 }
$avgFileCompressionRatio = if ($completedFileCount -gt 0) {
    [Math]::Round((($summaryRows | Measure-Object -Property compression_ratio -Average).Average), 6)
} else { 0 }
$avgCompressedFraction = if ($completedFileCount -gt 0) {
    [Math]::Round((($summaryRows | Measure-Object -Property compressed_fraction -Average).Average), 6)
} else { 0 }
$avgRuntimeSeconds = if ($completedFileCount -gt 0) {
    [Math]::Round((($summaryRows | Measure-Object -Property runtime_seconds -Average).Average), 6)
} else { 0 }
$avgMemoryMb = if ($completedFileCount -gt 0) {
    [Math]::Round((($summaryRows | Measure-Object -Property memory_mb -Average).Average), 6)
} else { 0 }
$inputPointsPerSecond = if ($totalRuntimeSeconds -gt 0) {
    [Math]::Round($totalInputPoints / $totalRuntimeSeconds, 3)
} else { 0 }

[pscustomobject]@{
    algorithm = $AlgorithmLabel
    dataset = $DatasetName
    epsilon = $ThresholdLabel
    alg_opt = $AlgOpt
    files_total = $files.Count
    files_completed = $completedFileCount
    files_processed_now = $processedFiles
    files_reused = $resumedFiles
    files_skipped = $skippedFiles
    total_input_points = $totalInputPoints
    total_output_points = $totalOutputPoints
    total_runtime_seconds = [Math]::Round($totalRuntimeSeconds, 6)
    avg_file_runtime_seconds = $avgRuntimeSeconds
    avg_file_compression_ratio = $avgFileCompressionRatio
    avg_file_compressed_fraction = $avgCompressedFraction
    overall_compression_ratio = $overallCompressionRatio
    space_saving_ratio = $spaceSavingRatio
    avg_memory_mb = $avgMemoryMb
    max_memory_mb = [Math]::Round($maxMemoryMb, 6)
    input_points_per_second = $inputPointsPerSecond
    summary_csv = $summaryPath
    skipped_csv = if ($skippedRows.Count -gt 0) { $skippedPath } else { "" }
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

$finishedAt = Get-Date
@(
    "algorithm=$AlgorithmLabel"
    "dataset=$DatasetName"
    "epsilon=$ThresholdLabel"
    "alg_opt=$AlgOpt"
    "input_root=$datasetInputRoot"
    "files_total=$($files.Count)"
    "files_completed=$completedFileCount"
    "files_processed_now=$processedFiles"
    "files_reused=$resumedFiles"
    "files_skipped=$skippedFiles"
    "summary_csv=$summaryPath"
    "metrics_csv=$metricsPath"
    ("started_at={0}" -f $startedAt.ToString("yyyy-MM-dd HH:mm:ss"))
    ("finished_at={0}" -f $finishedAt.ToString("yyyy-MM-dd HH:mm:ss"))
    "note=DPTS on Windows uses external PowerShell timing for runtime_seconds"
) | Set-Content -Path $runInfoPath

Import-Csv -Path $metricsPath | Format-List
