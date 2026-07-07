param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [ValidateSet("chengdu", "Geolife", "T-drive", "wuxi")]
    [string]$DatasetName,
    [string]$DatasetRoot,
    [int]$PeriodSnapshot,
    [string]$ThresholdTag,
    [string]$PythonExe = "py",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Get-DatasetSlug {
    param([string]$Name)

    switch ($Name) {
        "Geolife" { return "geolife" }
        "T-drive" { return "tdrive" }
        default { return $Name.ToLowerInvariant() }
    }
}

function Find-GraCTExecutable {
    param([string]$SearchRoot)

    $candidates = @(
        (Join-Path $SearchRoot "build_codex\GraCT_create_index.exe"),
        (Join-Path $SearchRoot "build_codex\Release\GraCT_create_index.exe"),
        (Join-Path $SearchRoot "build_mingw\GraCT_create_index.exe"),
        (Join-Path $SearchRoot "build_mingw\Release\GraCT_create_index.exe"),
        (Join-Path $SearchRoot "build_mingw_static\GraCT_create_index.exe"),
        (Join-Path $SearchRoot "build_mingw_static\Release\GraCT_create_index.exe"),
        (Join-Path $SearchRoot "build\GraCT_create_index.exe"),
        (Join-Path $SearchRoot "build\Release\GraCT_create_index.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Ensure-GraCTExecutable {
    param(
        [string]$WorkspaceRoot,
        [string]$GraCTRoot,
        [string]$LogDir
    )

    $existing = Find-GraCTExecutable -SearchRoot $GraCTRoot
    if ($existing) {
        return $existing
    }

    $buildDir = Join-Path $GraCTRoot "build_codex"
    Ensure-Directory $buildDir

    $configureLog = Join-Path $LogDir "cmake_configure.log"
    $buildLog = Join-Path $LogDir "cmake_build.log"

    $configureOutput = & cmake -S $GraCTRoot -B $buildDir -G "MinGW Makefiles" 2>&1
    $configureOutput | Set-Content -Path $configureLog -Encoding UTF8
    if ($LASTEXITCODE -ne 0) {
        throw "GraCT cmake configure failed. See $configureLog"
    }

    $buildOutput = & cmake --build $buildDir --target GraCT_create_index --config Release 2>&1
    $buildOutput | Set-Content -Path $buildLog -Encoding UTF8
    if ($LASTEXITCODE -ne 0) {
        throw "GraCT build failed. See $buildLog"
    }

    $built = Find-GraCTExecutable -SearchRoot $GraCTRoot
    if (-not $built) {
        throw "GraCT_create_index.exe was not found after build."
    }

    return $built
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$datasetRootDir = Join-Path $WorkspaceRoot ("skwcl\GraCT_result\{0}" -f $DatasetName)
$thresholdRoot = Join-Path $datasetRootDir $ThresholdTag
$toolingRoot = Join-Path $datasetRootDir "_tooling"
$logDir = Join-Path $thresholdRoot "logs"
$runInfoPath = Join-Path $thresholdRoot "run_info.txt"

if ((Test-Path $runInfoPath) -and -not $Force) {
    Write-Host "GraCT $DatasetName $ThresholdTag already exists. Skipping."
    exit 0
}

Ensure-Directory $datasetRootDir
Ensure-Directory $thresholdRoot
Ensure-Directory $toolingRoot
Ensure-Directory $logDir

$datasetSlug = Get-DatasetSlug -Name $DatasetName
$convertedPath = Join-Path $toolingRoot ("gract_{0}_dataset.txt" -f $datasetSlug)
$conversionLogPath = Join-Path $toolingRoot ("prepare_{0}.json" -f $datasetSlug)
$indexPath = Join-Path $thresholdRoot ("gract_{0}.idx" -f $datasetSlug)
$summaryPath = Join-Path $thresholdRoot "summary.csv"
$metricsPath = Join-Path $thresholdRoot "metrics_summary.csv"
$stdoutLogPath = Join-Path $logDir "gract_build_stdout.log"

$prepareScript = Join-Path $WorkspaceRoot "batch\prepare_gract_dataset.py"
$prepareOutput = & $PythonExe $prepareScript --dataset $DatasetName --input-dir $DatasetRoot --output $convertedPath
if ($LASTEXITCODE -ne 0) {
    throw "GraCT dataset conversion failed for $DatasetName"
}

$prepareJson = ($prepareOutput | Select-Object -Last 1)
$prepareJson | Set-Content -Path $conversionLogPath -Encoding UTF8
$prepareStats = $prepareJson | ConvertFrom-Json

$graCTRoot = Join-Path $WorkspaceRoot "gract"
$exePath = Ensure-GraCTExecutable -WorkspaceRoot $WorkspaceRoot -GraCTRoot $graCTRoot -LogDir $logDir

if (Test-Path $indexPath) {
    Remove-Item -LiteralPath $indexPath -Force
}

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$buildOutput = & $exePath $convertedPath $PeriodSnapshot $indexPath 2>&1
$stopwatch.Stop()
$buildOutput | Set-Content -Path $stdoutLogPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    throw "GraCT build failed for $DatasetName threshold $ThresholdTag. See $stdoutLogPath"
}

if (-not (Test-Path $indexPath)) {
    throw "GraCT index was not produced: $indexPath"
}

$inputPoints = [int64]$prepareStats.point_count
$outputPoints = $inputPoints
$trajectoryCount = [int64]$prepareStats.trajectory_count
$rawSourceBytes = [int64]$prepareStats.raw_source_bytes
$convertedBytes = (Get-Item $convertedPath).Length
$indexBytes = (Get-Item $indexPath).Length
$buildSeconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
$inputPointsPerSecond = if ($buildSeconds -gt 0) { $inputPoints / $buildSeconds } else { 0 }
$outputPointsPerSecond = if ($buildSeconds -gt 0) { $outputPoints / $buildSeconds } else { 0 }
$indexOverConverted = if ($convertedBytes -gt 0) { $indexBytes / $convertedBytes } else { 0 }
$convertedOverIndex = if ($indexBytes -gt 0) { $convertedBytes / $indexBytes } else { 0 }
$indexOverRaw = if ($rawSourceBytes -gt 0) { $indexBytes / $rawSourceBytes } else { 0 }
$rawOverIndex = if ($indexBytes -gt 0) { $rawSourceBytes / $indexBytes } else { 0 }
$spaceSavingVsConverted = 1 - $indexOverConverted
$spaceSavingVsRaw = 1 - $indexOverRaw

$row = [pscustomobject]@{
    algorithm = "GraCT"
    dataset = $DatasetName
    period_snapshot = $PeriodSnapshot
    trajectory_count = $trajectoryCount
    point_count = $inputPoints
    input_points = $inputPoints
    output_points = $outputPoints
    raw_source_bytes = $rawSourceBytes
    converted_dataset_bytes = $convertedBytes
    index_bytes = $indexBytes
    index_over_converted_ratio = $indexOverConverted
    converted_over_index_ratio = $convertedOverIndex
    index_over_raw_ratio = $indexOverRaw
    raw_over_index_ratio = $rawOverIndex
    space_saving_vs_converted = $spaceSavingVsConverted
    space_saving_vs_raw = $spaceSavingVsRaw
    build_seconds = $buildSeconds
    input_points_per_second = $inputPointsPerSecond
    output_points_per_second = $outputPointsPerSecond
    coord_scale = [int64]$prepareStats.coord_scale
    converted_dataset_path = $convertedPath
    index_path = $indexPath
}

$row | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8
$row | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "GraCT batch result for $DatasetName with period_snapshot=$PeriodSnapshot."
    ""
    "Run result:"
    "- build_seconds: $buildSeconds"
    "- trajectory_count: $trajectoryCount"
    "- input_points: $inputPoints"
    "- output_points: $outputPoints"
    "- raw_source_bytes: $rawSourceBytes"
    "- converted_dataset_bytes: $convertedBytes"
    "- index_bytes: $indexBytes"
    "- input_points_per_second: $inputPointsPerSecond"
    "- output_points_per_second: $outputPointsPerSecond"
    ""
    "Compression interpretation:"
    "- index_over_converted_ratio: $indexOverConverted"
    "- converted_over_index_ratio: $convertedOverIndex"
    "- index_over_raw_ratio: $indexOverRaw"
    "- raw_over_index_ratio: $rawOverIndex"
    "- space_saving_vs_converted: $spaceSavingVsConverted"
    "- space_saving_vs_raw: $spaceSavingVsRaw"
    ""
    "Paths:"
    "- converted_dataset_path: $convertedPath"
    "- index_path: $indexPath"
    "- build_stdout_log: $stdoutLogPath"
) | Set-Content -Path $runInfoPath -Encoding UTF8

Write-Host "GraCT $DatasetName $ThresholdTag completed in $buildSeconds s"
