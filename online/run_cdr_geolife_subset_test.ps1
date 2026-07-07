param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\data\Geolife",
    [double]$AccuracyBound = 1e-5,
    [string]$AccuracyTag = "1e-5",
    [int]$MaxPoints = 1000
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

function Get-GeolifeConvertedLines {
    param([string]$FilePath)

    $lines = New-Object System.Collections.Generic.List[string]
    $lineIndex = 0
    foreach ($line in Get-Content $FilePath) {
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
            $lines.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
        }
        catch {
            continue
        }
    }
    return $lines
}

function Ensure-CdrExecutable {
    param(
        [string]$WorkspaceRoot,
        [string]$ExePath
    )

    $sourcePath = Join-Path $WorkspaceRoot "online\CDR\CDR.cpp"
    $needsCompile = -not (Test-Path $ExePath)
    if (-not $needsCompile) {
        $needsCompile = (Get-Item $sourcePath).LastWriteTimeUtc -gt (Get-Item $ExePath).LastWriteTimeUtc
    }

    if ($needsCompile) {
        & g++ $sourcePath -o $ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile CDR.cpp"
        }
    }
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$accuracyTag = $AccuracyTag
$testRoot = Join-Path $WorkspaceRoot ("test\CDR_test_Geolife_le{0}_{1}" -f $MaxPoints, $accuracyTag)
$convertedDir = Join-Path $testRoot "converted"
$resultsDir = Join-Path $testRoot "results"
$binDir = Join-Path $testRoot "bin"
$summaryPath = Join-Path $testRoot "cdr_geolife_subset_summary.csv"
$metricsPath = Join-Path $testRoot "cdr_geolife_subset_metrics_summary.csv"
$masterInfoPath = Join-Path $testRoot "run_info.txt"

Ensure-Directory $testRoot
Ensure-Directory $convertedDir
Ensure-Directory $resultsDir
Ensure-Directory $binDir

$exePath = Join-Path $binDir "CDR_fixed.exe"
Ensure-CdrExecutable -WorkspaceRoot $WorkspaceRoot -ExePath $exePath

$files = Get-ChildItem $DatasetRoot -Recurse -File -Filter *.plt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $convertedLines = Get-GeolifeConvertedLines -FilePath $file.FullName
    if ($convertedLines.Count -le 1 -or $convertedLines.Count -gt $MaxPoints) {
        continue
    }

    $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $convertedSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $convertedDir } else { Join-Path $convertedDir $relativeDir }
    $resultsSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $convertedSubdir
    Ensure-Directory $resultsSubdir

    $convertedPath = Join-Path $convertedSubdir ($baseName + ".cdr_input.plt")
    $resultPath = Join-Path $resultsSubdir ($baseName + ".cdr_output.csv")

    Set-Content -Path $convertedPath -Value $convertedLines

    & $exePath CDR $convertedPath $convertedLines.Count $resultPath $AccuracyBound
    if ($LASTEXITCODE -ne 0) {
        throw "CDR failed on $($file.FullName)"
    }

    $outputLines = Get-Content $resultPath
    if ($outputLines.Count -eq 0) {
        continue
    }

    $runtimeSeconds = [double]$outputLines[-1]
    $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
    $compressionRatio = if ($convertedLines.Count -gt 0) {
        [Math]::Round($outputPointCount / $convertedLines.Count, 6)
    }
    else {
        0
    }

    $rows.Add([pscustomobject]@{
        algorithm = "CDR"
        dataset = "Geolife_subset"
        accuracy_bound = $accuracyTag
        max_points = $MaxPoints
        relative_file = $relativePath
        input_points = $convertedLines.Count
        output_points = $outputPointCount
        compression_ratio = $compressionRatio
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        converted_path = $convertedPath
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
    algorithm = "CDR"
    dataset = "Geolife_subset"
    accuracy_bound = $accuracyTag
    max_points = $MaxPoints
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
    metrics_note = "Subset run only; CDR current implementation is limited to trajectories with at most 1000 points"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=CDR"
    "dataset=Geolife_subset"
    ("accuracy_bound={0}" -f $accuracyTag)
    ("max_points={0}" -f $MaxPoints)
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
    "subset_note=only trajectories with input_points <= 1000 are included because the CDR implementation uses fixed-size arrays"
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
) | Set-Content -Path $masterInfoPath

[pscustomobject]@{
    algorithm = "CDR"
    dataset = "Geolife_subset"
    accuracy_bound = $accuracyTag
    max_points = $MaxPoints
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
