param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\data\Geolife",
    [double]$MaxError = 50,
    [string]$MaxErrorTag = "50m",
    [int]$PolygonNum = 16,
    [double]$Accuracy = 0.2,
    [int]$Utf = 3
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

function Ensure-CisedsExecutable {
    param(
        [string]$SourcePath,
        [string]$ExePath
    )

    $needsCompile = -not (Test-Path $ExePath)
    if (-not $needsCompile) {
        $needsCompile = (Get-Item $SourcePath).LastWriteTimeUtc -gt (Get-Item $ExePath).LastWriteTimeUtc
    }

    if ($needsCompile) {
        & g++ -std=c++17 -O2 -Wall -Wextra -pedantic $SourcePath -o $ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile ciseds_reference.cpp"
        }
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

function Parse-Metrics {
    param([string[]]$Lines)

    $metrics = @{}
    foreach ($line in $Lines) {
        if ($line -match "=") {
            $parts = $line -split "=", 2
            $metrics[$parts[0]] = $parts[1]
        }
    }
    return $metrics
}

$testRoot = Join-Path $WorkspaceRoot ("test\CISEDS_test_Geolife_{0}" -f $MaxErrorTag)
$preparedDir = Join-Path $testRoot "prepared_xy"
$resultsDir = Join-Path $testRoot "results"
$exeDir = Join-Path $testRoot "bin"
$summaryPath = Join-Path $testRoot "ciseds_geolife_summary.csv"
$metricsPath = Join-Path $testRoot "ciseds_geolife_metrics_summary.csv"
$skippedPath = Join-Path $testRoot "skipped.csv"
$masterInfoPath = Join-Path $testRoot "run_info.txt"

Ensure-Directory $testRoot
Ensure-Directory $preparedDir
Ensure-Directory $resultsDir
Ensure-Directory $exeDir

$sourcePath = Join-Path $WorkspaceRoot "PILOT-C\cpp_port\src\ciseds_reference.cpp"
$exePath = Join-Path $exeDir "ciseds_reference.exe"
Ensure-CisedsExecutable -SourcePath $sourcePath -ExePath $exePath
$pythonExe = "py"
$prepareScript = Join-Path $WorkspaceRoot "batch\prepare_pilotc_geolife_xy.py"

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

& $pythonExe $prepareScript --input-dir $DatasetRoot --output-dir $preparedDir
if ($LASTEXITCODE -ne 0) {
    throw "Failed to prepare PILOT-C Geolife xy inputs."
}

$files = Get-ChildItem $preparedDir -Recurse -File -Filter *.txt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]
$skipped = New-Object System.Collections.Generic.List[object]
$wallClock = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $preparedDir -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $resultsSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $resultsDir } else { Join-Path $resultsDir $relativeDir }
    Ensure-Directory $resultsSubdir

    $resultPath = Join-Path $resultsSubdir ($baseName + ".csv")
    $lineCount = (Get-Content $file.FullName | Measure-Object -Line).Lines
    if ($lineCount -lt 2) {
        $skipped.Add([pscustomobject]@{
            algorithm = "CISED-S"
            dataset = "Geolife"
            max_error_tag = $MaxErrorTag
            relative_file = $relativePath
            reason = "too_few_valid_points_after_preparation"
        }) | Out-Null
        continue
    }

    try {
        $output = & $exePath $file.FullName $MaxError $PolygonNum $resultPath $Accuracy $Utf 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw ($output | Out-String)
        }

        $metrics = Parse-Metrics -Lines $output
        if (-not $metrics.ContainsKey("compress_ratio")) {
            throw "Missing compression metrics in executable output."
        }

        $inputPoints = [int64]$metrics["points"]
        $outputPoints = [int64]$metrics["kept_points"]
        $compressionRatio = [double]$metrics["compress_ratio"]
        $compressionTimeUs = [double]$metrics["compression_time_us"]
        $actualMaxError = [double]$metrics["max_error"]
        $meanError = [double]$metrics["mean_error"]
        $varError = [double]$metrics["var_error"]

        $rows.Add([pscustomobject]@{
            algorithm = "CISED-S"
            dataset = "Geolife"
            max_error_tag = $MaxErrorTag
            max_error_meters = $MaxError
            polygon_num = $PolygonNum
            accuracy = $Accuracy
            utf = $Utf
            relative_file = $relativePath
            input_points = $inputPoints
            output_points = $outputPoints
            compress_bits = [int64]$metrics["compress_bits"]
            compression_ratio = [Math]::Round($compressionRatio, 6)
            compressed_fraction = [Math]::Round([double]$metrics["compressed_fraction"], 6)
            saved_ratio = if ($metrics["compressed_fraction"]) { [Math]::Round(1 - [double]$metrics["compressed_fraction"], 6) } else { 0.0 }
            compression_time_us = [Math]::Round($compressionTimeUs, 3)
            max_error = $actualMaxError
            mean_error = $meanError
            var_error = $varError
            prepared_path = $file.FullName
            result_path = $resultPath
        }) | Out-Null
    }
    catch {
        $skipped.Add([pscustomobject]@{
            algorithm = "CISED-S"
            dataset = "Geolife"
            max_error_tag = $MaxErrorTag
            relative_file = $relativePath
            reason = "runtime_error"
            detail = $_.Exception.Message
        }) | Out-Null
    }
}

$wallClock.Stop()

$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8
$skipped | Export-Csv -Path $skippedPath -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property input_points -Sum
$outputTotals = $rows | Measure-Object -Property output_points -Sum
$bitTotals = $rows | Measure-Object -Property compress_bits -Sum
$timeTotalsUs = $rows | Measure-Object -Property compression_time_us -Sum
$avgFileCompressionRatio = ($rows | Measure-Object -Property compression_ratio -Average).Average
$avgCompressedFraction = ($rows | Measure-Object -Property compressed_fraction -Average).Average
$avgFileCompressionTimeUs = ($rows | Measure-Object -Property compression_time_us -Average).Average
$avgFileMeanError = ($rows | Measure-Object -Property mean_error -Average).Average
$avgFileVarError = ($rows | Measure-Object -Property var_error -Average).Average
$globalMaxError = ($rows | Measure-Object -Property max_error -Maximum).Maximum

$overallCompressionRatio = if ($bitTotals.Sum -gt 0) {
    [Math]::Round(($inputTotals.Sum * 3 * 8 * 8) / $bitTotals.Sum, 6)
}
else {
    0
}
$weightedCompressedFraction = if ($overallCompressionRatio -gt 0) {
    [Math]::Round(1 / $overallCompressionRatio, 6)
}
else {
    0
}
$savedRatio = if ($weightedCompressedFraction -gt 0) {
    [Math]::Round(1 - $weightedCompressedFraction, 6)
}
else {
    0
}
$totalRuntimeSeconds = if ($timeTotalsUs.Sum) {
    [Math]::Round($timeTotalsUs.Sum / 1000000.0, 6)
}
else {
    0
}
$inputPointsPerSecond = if ($timeTotalsUs.Sum -gt 0) {
    [Math]::Round(($inputTotals.Sum * 1000000.0) / $timeTotalsUs.Sum, 3)
}
else {
    0
}

[pscustomobject]@{
    algorithm = "CISED-S"
    dataset = "Geolife"
    max_error_tag = $MaxErrorTag
    max_error_meters = $MaxError
    polygon_num = $PolygonNum
    accuracy = $Accuracy
    utf = $Utf
    processed_files = $rows.Count
    skipped_files = $skipped.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    total_compress_bits = [int64]$bitTotals.Sum
    total_runtime_seconds = $totalRuntimeSeconds
    wall_clock_seconds = [Math]::Round($wallClock.Elapsed.TotalSeconds, 6)
    overall_compression_ratio = $overallCompressionRatio
    average_compressed_fraction = [Math]::Round($avgCompressedFraction, 6)
    weighted_compressed_fraction = $weightedCompressedFraction
    saved_ratio = $savedRatio
    avg_file_compression_ratio = [Math]::Round($avgFileCompressionRatio, 6)
    avg_file_compression_time_us = [Math]::Round($avgFileCompressionTimeUs, 3)
    avg_file_mean_error = [Math]::Round($avgFileMeanError, 8)
    avg_file_var_error = [Math]::Round($avgFileVarError, 8)
    global_max_error = [Math]::Round($globalMaxError, 8)
    input_points_per_second = $inputPointsPerSecond
    metrics_note = "compression_ratio follows original PILOT-C code: raw_bits=(points*(dims+1)*8*8) divided by encoded_bit_length"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=CISED-S"
    "dataset=Geolife"
    ("max_error={0}" -f $MaxErrorTag)
    ("max_error_meters={0}" -f $MaxError)
    ("polygon_num={0}" -f $PolygonNum)
    ("accuracy={0}" -f $Accuracy)
    ("utf={0}" -f $Utf)
    ("processed_files={0}" -f $rows.Count)
    ("skipped_files={0}" -f $skipped.Count)
    ("total_input_points={0}" -f [int64]$inputTotals.Sum)
    ("total_output_points={0}" -f [int64]$outputTotals.Sum)
    ("total_compress_bits={0}" -f [int64]$bitTotals.Sum)
    ("overall_compression_ratio={0}" -f $overallCompressionRatio)
    ("average_compressed_fraction={0}" -f [Math]::Round($avgCompressedFraction, 6))
    ("weighted_compressed_fraction={0}" -f $weightedCompressedFraction)
    ("saved_ratio={0}" -f $savedRatio)
    ("total_runtime_seconds={0}" -f $totalRuntimeSeconds)
    ("wall_clock_seconds={0}" -f [Math]::Round($wallClock.Elapsed.TotalSeconds, 6))
    ("avg_file_compression_ratio={0}" -f [Math]::Round($avgFileCompressionRatio, 6))
    ("avg_file_compression_time_us={0}" -f [Math]::Round($avgFileCompressionTimeUs, 3))
    ("avg_file_mean_error={0}" -f [Math]::Round($avgFileMeanError, 8))
    ("avg_file_var_error={0}" -f [Math]::Round($avgFileVarError, 8))
    ("global_max_error={0}" -f [Math]::Round($globalMaxError, 8))
    ("input_points_per_second={0}" -f $inputPointsPerSecond)
    "coordinate_unit=utm_like_projected_xy_meters_using_original_pilotc_preprocess_style"
    "implementation_note=cplusplus_port_validated_against_pilotc_python_ciseds_on_sample"
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
    ("skipped_csv={0}" -f $skippedPath)
) | Set-Content -Path $masterInfoPath

[pscustomobject]@{
    algorithm = "CISED-S"
    dataset = "Geolife"
    max_error_tag = $MaxErrorTag
    max_error_meters = $MaxError
    polygon_num = $PolygonNum
    processed_files = $rows.Count
    skipped_files = $skipped.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    overall_compression_ratio = $overallCompressionRatio
    global_max_error = [Math]::Round($globalMaxError, 8)
    input_points_per_second = $inputPointsPerSecond
    metrics_csv = $metricsPath
    run_info = $masterInfoPath
} | Format-List
