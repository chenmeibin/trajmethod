param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\data\Geolife",
    [double]$Epsilon = 1e-5,
    [string]$EpsilonTag = "1e-5",
    [int]$AlgOpt = -1
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

function Ensure-DptsExecutable {
    param(
        [string]$WorkspaceRoot,
        [string]$ExePath
    )

    $sources = Get-ChildItem (Join-Path $WorkspaceRoot "batch\DPTS\src\*.cpp") | Sort-Object FullName
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
            throw "Failed to compile DPTS"
        }
    }
}

function Get-GeolifeDataLineCount {
    param([string]$FilePath)

    $count = 0
    $lineIndex = 0
    foreach ($line in Get-Content $FilePath) {
        $lineIndex += 1
        if ($lineIndex -le 6 -or [string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $count += 1
    }
    return $count
}

$algorithmMap = [ordered]@{
    0 = "basic_dp"
    1 = "enhanced_dp"
    2 = "basic_sp"
    3 = "sp_theory"
    4 = "sp_practice"
    5 = "sp_both"
    6 = "intersect"
    7 = "split"
    8 = "merge"
    9 = "greedy"
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$epsilonTag = $EpsilonTag
$testRoot = Join-Path $WorkspaceRoot ("test\DPTS_test_Geolife_{0}" -f $epsilonTag)
$binDir = Join-Path $testRoot "bin"
$runsDir = Join-Path $testRoot "runs"
$masterSummaryPath = Join-Path $testRoot "dpts_geolife_master_summary.csv"
$masterMetricsPath = Join-Path $testRoot "dpts_geolife_master_metrics.csv"
$masterInfoPath = Join-Path $testRoot "run_info.txt"

Ensure-Directory $testRoot
Ensure-Directory $binDir
Ensure-Directory $runsDir

$exePath = Join-Path $binDir "DPTS.exe"
Ensure-DptsExecutable -WorkspaceRoot $WorkspaceRoot -ExePath $exePath

$files = Get-ChildItem $DatasetRoot -Recurse -File -Filter *.plt | Sort-Object FullName
$allRows = New-Object System.Collections.Generic.List[object]
$metricRows = New-Object System.Collections.Generic.List[object]

$algEntries = if ($AlgOpt -ge 0) {
    if (-not $algorithmMap.Contains($AlgOpt)) {
        throw "Unsupported AlgOpt: $AlgOpt"
    }
    @([pscustomobject]@{ Key = $AlgOpt; Value = $algorithmMap[$AlgOpt] })
}
else {
    @($algorithmMap.GetEnumerator())
}

foreach ($algEntry in $algEntries) {
    $algOpt = [int]$algEntry.Key
    $algName = [string]$algEntry.Value
    $algRunRoot = Join-Path $runsDir ("alg_{0}_{1}" -f $algOpt, $algName)
    Ensure-Directory $algRunRoot

    $rows = New-Object System.Collections.Generic.List[object]

    foreach ($file in $files) {
        $relativePath = Get-RelativePathSafe -BasePath $DatasetRoot -TargetPath $file.FullName
        $relativeDir = [System.IO.Path]::GetDirectoryName($relativePath)
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

        $runSubdir = if ([string]::IsNullOrEmpty($relativeDir)) { $algRunRoot } else { Join-Path $algRunRoot $relativeDir }
        Ensure-Directory $runSubdir
        $fileRunDir = Join-Path $runSubdir $baseName
        Ensure-Directory $fileRunDir

        $lineCount = Get-GeolifeDataLineCount -FilePath $file.FullName
        $configContent = "{0} {1}`n{2} {3}`n1" -f ($file.FullName -replace "\\", "/"), $lineCount, $Epsilon, $algOpt
        Set-Content -Path (Join-Path $fileRunDir "config.txt") -Value $configContent

        Push-Location $fileRunDir
        try {
            $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
            & $exePath
            $stopwatch.Stop()
            if ($LASTEXITCODE -ne 0) {
                throw "DPTS failed on $($file.FullName) with alg $algOpt"
            }
            $statPath = Join-Path $fileRunDir "stat.txt"
            $statLines = Get-Content $statPath
        }
        finally {
            Pop-Location
        }

        if ($statLines.Count -lt 5) {
            throw "Unexpected DPTS stat format for $($file.FullName) with alg $algOpt"
        }

        $inputPoints = [int]$statLines[0]
        $outputPoints = [int]$statLines[1]
        $compressionRatio = [double]$statLines[2]
        $runtimeSeconds = $stopwatch.Elapsed.TotalSeconds
        $memoryMb = [double]$statLines[4]

        $row = [pscustomobject]@{
            algorithm = "DPTS"
            alg_opt = $algOpt
            alg_name = $algName
            dataset = "Geolife"
            epsilon = $epsilonTag
            relative_file = $relativePath
            input_points = $inputPoints
            output_points = $outputPoints
            compression_ratio = [Math]::Round($compressionRatio, 6)
            runtime_seconds = [Math]::Round($runtimeSeconds, 6)
            memory_mb = [Math]::Round($memoryMb, 6)
            stat_path = $statPath
        }
        $rows.Add($row) | Out-Null
        $allRows.Add($row) | Out-Null
    }

    $algSummaryPath = Join-Path $testRoot ("dpts_geolife_alg_{0}_{1}_summary.csv" -f $algOpt, $algName)
    $algMetricsPath = Join-Path $testRoot ("dpts_geolife_alg_{0}_{1}_metrics.csv" -f $algOpt, $algName)
    $rows | Export-Csv -Path $algSummaryPath -NoTypeInformation -Encoding UTF8

    $inputTotals = $rows | Measure-Object -Property input_points -Sum
    $outputTotals = $rows | Measure-Object -Property output_points -Sum
    $runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum
    $memoryAverage = ($rows | Measure-Object -Property memory_mb -Average).Average
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

    $metricRow = [pscustomobject]@{
        algorithm = "DPTS"
        alg_opt = $algOpt
        alg_name = $algName
        dataset = "Geolife"
        epsilon = $epsilonTag
        processed_files = $rows.Count
        total_input_points = [int64]$inputTotals.Sum
        total_output_points = [int64]$outputTotals.Sum
        total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
        overall_compression_ratio = $overallCompressionRatio
        saved_ratio = $savedRatio
        avg_file_compression_ratio = [Math]::Round($fileCompressionAverage, 6)
        avg_file_runtime_seconds = [Math]::Round($fileRuntimeAverage, 6)
        avg_memory_mb = [Math]::Round($memoryAverage, 6)
        input_points_per_second = $pointsPerSecond
        metrics_note = "runtime_seconds is measured externally in PowerShell because Windows build does not emit timing"
    }
    $metricRows.Add($metricRow) | Out-Null
    $metricRow | Export-Csv -Path $algMetricsPath -NoTypeInformation -Encoding UTF8
}

$allRows | Export-Csv -Path $masterSummaryPath -NoTypeInformation -Encoding UTF8
$metricRows | Export-Csv -Path $masterMetricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=DPTS"
    "dataset=Geolife"
    ("epsilon={0}" -f $epsilonTag)
    ("algorithms_run={0}" -f ($algorithmMap.Keys -join ","))
    ("master_summary_csv={0}" -f $masterSummaryPath)
    ("master_metrics_csv={0}" -f $masterMetricsPath)
    "note=DPTS contains multiple algorithm variants; see master_metrics for per-algorithm comparison"
) | Set-Content -Path $masterInfoPath

$metricRows | Format-Table -AutoSize
