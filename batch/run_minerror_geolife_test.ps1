param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\data\Geolife",
    [double]$Budget = 0.5,
    [string]$BudgetTag = "0.5",
    [int]$Algorithm = 3
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

function Ensure-MinErrorExecutable {
    param(
        [string]$WorkspaceRoot,
        [string]$ExePath
    )

    $sources = Get-ChildItem (Join-Path $WorkspaceRoot "batch\MinError\src\*.cpp") | Sort-Object FullName
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
        & g++ -g $sources.FullName -o $ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile MinError"
        }
    }
}

function Get-GeolifeEffectivePointCount {
    param([string]$FilePath)

    $preLat = $null
    $preLon = $null
    $count = 0
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
        }
        catch {
            continue
        }

        if ($null -ne $preLat -and $lat -eq $preLat -and $lon -eq $preLon) {
            continue
        }

        $preLat = $lat
        $preLon = $lon
        $count += 1
    }

    return $count
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$budgetTag = $BudgetTag
$testRoot = Join-Path $WorkspaceRoot ("test\MinError_test_Geolife_{0}" -f $budgetTag)
$binDir = Join-Path $testRoot "bin"
$runsDir = Join-Path $testRoot "runs"
$summaryPath = Join-Path $testRoot "minerror_geolife_summary.csv"
$metricsPath = Join-Path $testRoot "minerror_geolife_metrics_summary.csv"
$masterInfoPath = Join-Path $testRoot "run_info.txt"

Ensure-Directory $testRoot
Ensure-Directory $binDir
Ensure-Directory $runsDir

$exePath = Join-Path $binDir "MinError.exe"
Ensure-MinErrorExecutable -WorkspaceRoot $WorkspaceRoot -ExePath $exePath

$files = Get-ChildItem $DatasetRoot -Recurse -File -Filter *.plt | Sort-Object FullName
$rows = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $file.FullName
    $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $runSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $runsDir } else { Join-Path $runsDir $relativeDir }
    Ensure-Directory $runSubdir

    $fileRunDir = Join-Path $runSubdir $baseName
    Ensure-Directory $fileRunDir

    $effectiveInputPoints = Get-GeolifeEffectivePointCount -FilePath $file.FullName
    if ($effectiveInputPoints -le 0) {
        continue
    }

    $budgetPoints = [int]($Budget * $effectiveInputPoints)
    $configContent = "{0} {1}`n{2} {3}`n1" -f ($file.FullName -replace "\\", "/"), $effectiveInputPoints, $Budget, $Algorithm
    Set-Content -Path (Join-Path $fileRunDir "config.txt") -Value $configContent

    Push-Location $fileRunDir
    try {
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        & $exePath
        $stopwatch.Stop()
        if ($LASTEXITCODE -ne 0) {
            throw "MinError failed on $($file.FullName)"
        }
        $statPath = Join-Path $fileRunDir "stat.txt"
        $statLines = Get-Content $statPath
    }
    finally {
        Pop-Location
    }

    if ($statLines.Count -lt 4) {
        throw "Unexpected MinError stat format for $($file.FullName)"
    }

    $directionalError = [double]$statLines[0]
    $runtimeSeconds = $stopwatch.Elapsed.TotalSeconds
    $memoryMb = [double]$statLines[3]

    $rows.Add([pscustomobject]@{
        algorithm = "MinError"
        dataset = "Geolife"
        budget_ratio = $budgetTag
        alg_opt = $Algorithm
        relative_file = $relativePath
        effective_input_points = $effectiveInputPoints
        budget_points = $budgetPoints
        directional_error = [Math]::Round($directionalError, 6)
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        memory_mb = [Math]::Round($memoryMb, 6)
        stat_path = $statPath
    }) | Out-Null
}

$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

$inputTotals = $rows | Measure-Object -Property effective_input_points -Sum
$budgetTotals = $rows | Measure-Object -Property budget_points -Sum
$runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum
$memoryAverage = ($rows | Measure-Object -Property memory_mb -Average).Average
$runtimeAverage = ($rows | Measure-Object -Property runtime_seconds -Average).Average
$errorAverage = ($rows | Measure-Object -Property directional_error -Average).Average
$errorMin = ($rows | Measure-Object -Property directional_error -Minimum).Minimum
$errorMax = ($rows | Measure-Object -Property directional_error -Maximum).Maximum
$effectiveBudgetRatio = if ($inputTotals.Sum -gt 0) {
    [Math]::Round($budgetTotals.Sum / $inputTotals.Sum, 6)
}
else {
    0
}
$savedRatio = if ($inputTotals.Sum -gt 0) {
    [Math]::Round(1 - ($budgetTotals.Sum / $inputTotals.Sum), 6)
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

[pscustomobject]@{
    algorithm = "MinError"
    dataset = "Geolife"
    budget_ratio = $budgetTag
    alg_opt = $Algorithm
    processed_files = $rows.Count
    total_effective_input_points = [int64]$inputTotals.Sum
    total_budget_points = [int64]$budgetTotals.Sum
    effective_budget_ratio = $effectiveBudgetRatio
    saved_ratio = $savedRatio
    avg_directional_error = [Math]::Round($errorAverage, 6)
    min_directional_error = [Math]::Round($errorMin, 6)
    max_directional_error = [Math]::Round($errorMax, 6)
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    avg_file_runtime_seconds = [Math]::Round($runtimeAverage, 6)
    avg_memory_mb = [Math]::Round($memoryAverage, 6)
    input_points_per_second = $pointsPerSecond
    metrics_note = "MinError is budget-constrained; directional_error is minimized under budget_points=floor(budget_ratio*effective_input_points)"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=MinError"
    "dataset=Geolife"
    ("budget_ratio={0}" -f $budgetTag)
    ("alg_opt={0}" -f $Algorithm)
    ("processed_files={0}" -f $rows.Count)
    ("total_effective_input_points={0}" -f [int64]$inputTotals.Sum)
    ("total_budget_points={0}" -f [int64]$budgetTotals.Sum)
    ("effective_budget_ratio={0}" -f $effectiveBudgetRatio)
    ("saved_ratio={0}" -f $savedRatio)
    ("avg_directional_error={0}" -f [Math]::Round($errorAverage, 6))
    ("min_directional_error={0}" -f [Math]::Round($errorMin, 6))
    ("max_directional_error={0}" -f [Math]::Round($errorMax, 6))
    ("total_runtime_seconds={0}" -f [Math]::Round($runtimeTotals.Sum, 6))
    ("avg_file_runtime_seconds={0}" -f [Math]::Round($runtimeAverage, 6))
    ("avg_memory_mb={0}" -f [Math]::Round($memoryAverage, 6))
    ("input_points_per_second={0}" -f $pointsPerSecond)
    "budget_unit=fraction_of_effective_input_points"
    "error_note=MinError minimizes direction-based error, so there is no epsilon threshold in this test"
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
) | Set-Content -Path $masterInfoPath

[pscustomobject]@{
    algorithm = "MinError"
    dataset = "Geolife"
    budget_ratio = $budgetTag
    alg_opt = $Algorithm
    processed_files = $rows.Count
    total_effective_input_points = [int64]$inputTotals.Sum
    total_budget_points = [int64]$budgetTotals.Sum
    avg_directional_error = [Math]::Round($errorAverage, 6)
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    input_points_per_second = $pointsPerSecond
    summary_csv = $summaryPath
    metrics_csv = $metricsPath
    info_file = $masterInfoPath
} | Format-List
