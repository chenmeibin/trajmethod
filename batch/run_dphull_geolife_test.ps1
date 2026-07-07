param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$DatasetRoot = "D:\all_traj_method\data\Geolife",
    [double]$Epsilon = 1e-5,
    [string]$EpsilonTag = "1e-5"
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

function Prepare-DpHullSources {
    param(
        [string]$SourceDir,
        [string]$BuildDir
    )

    Ensure-Directory $BuildDir
    Copy-Item -Path (Join-Path $SourceDir "DP.h") -Destination (Join-Path $BuildDir "DP.h") -Force
    Copy-Item -Path (Join-Path $SourceDir "PH.h") -Destination (Join-Path $BuildDir "PH.h") -Force

    $dpText = Get-Content (Join-Path $SourceDir "DP.c") -Raw
    $dpText = $dpText.Replace('#include "stdio.h"', "#include ""stdio.h""`r`n#include <string.h>")
    $dpText = $dpText.Replace('#include <sys/times.h>', "#ifndef WIN32`r`n#include <sys/times.h>`r`n#endif")
    $dpText = $dpText.Replace('struct tms      starttime, endtime;', "#ifndef WIN32`r`nstruct tms      starttime, endtime;`r`n#endif")
    $dpText = $dpText.Replace("void Start_Timing()`r`n{`r`n  times(&starttime);`r`n}", "void Start_Timing()`r`n{`r`n#ifndef WIN32`r`n  times(&starttime);`r`n#endif`r`n}")
    $dpText = $dpText.Replace("void  End_Timing()`r`n{`r`n  times(&endtime);`r`n  printf(""compression ratio: %lf\n"", n*1.0 / n_split);`r`n  printf(""compression time : %lf sec\n"", (double) (endtime.tms_utime - starttime.tms_utime) / 100.0);`r`n}", "void  End_Timing()`r`n{`r`n#ifndef WIN32`r`n  times(&endtime);`r`n  printf(""compression ratio: %lf\n"", n*1.0 / n_split);`r`n  printf(""compression time : %lf sec\n"", (double) (endtime.tms_utime - starttime.tms_utime) / 100.0);`r`n#endif`r`n}")
    $dpText = $dpText.Replace("  V = Alloc_Points(MAX_POINTS);`r`n  R = (POINT **) calloc(MAX_POINTS, sizeof(POINT *));", "  int point_capacity = n > 0 ? n : MAX_POINTS;`r`n  V = Alloc_Points(point_capacity);`r`n  R = (POINT **) calloc(point_capacity, sizeof(POINT *));")
    Set-Content -Path (Join-Path $BuildDir "DP.c") -Value $dpText

    $mainText = Get-Content (Join-Path $SourceDir "DPhull.c") -Raw
    $mainText = $mainText.Replace("		Init(""DPhull"");`r`n		double b, t;                  /*  l     r  */`r`n		int i;                        /*  bl-b-br  */`r`n		b = t = 0.0;`r`n 		for (i = 0; i < n; i++)`r`n    		{`r`n      			if (t < V[i][YY]) t = V[i][YY];`r`n      			if (t < V[i][XX]) t = V[i][XX];`r`n      			if (b > V[i][YY]) b = V[i][YY];`r`n      			if (b > V[i][XX]) b = V[i][XX];`r`n    		}`r`n  		top = t + 3.5 + t*0.05; bot = b - 10.5 - b*0.05;`r`n                n_split = 2;`r`n  		splits[0] = V;`r`n  		splits[1] = V+n - 1;`r`n		left = (PATH_HULL *)malloc(sizeof(PATH_HULL));`r`n		right = (PATH_HULL *)malloc(sizeof(PATH_HULL));`r`n		if (left == NULL || right == NULL)`r`n		{`r`n			printf(""malloc error\n"");`r`n			exit(0);`r`n		}`r`n		Get_Points(filename);", "		Init(""DPhull"");`r`n		Get_Points(filename);`r`n		double b, t;                  /*  l     r  */`r`n		int i;                        /*  bl-b-br  */`r`n		b = t = 0.0;`r`n 		for (i = 0; i < n; i++)`r`n    		{`r`n      			if (t < V[i][YY]) t = V[i][YY];`r`n      			if (t < V[i][XX]) t = V[i][XX];`r`n      			if (b > V[i][YY]) b = V[i][YY];`r`n      			if (b > V[i][XX]) b = V[i][XX];`r`n    		}`r`n  		top = t + 3.5 + t*0.05; bot = b - 10.5 - b*0.05;`r`n                n_split = 2;`r`n  		splits[0] = V;`r`n  		splits[1] = V+n - 1;`r`n		left = (PATH_HULL *)malloc(sizeof(PATH_HULL));`r`n		right = (PATH_HULL *)malloc(sizeof(PATH_HULL));`r`n		if (left == NULL || right == NULL)`r`n		{`r`n			printf(""malloc error\n"");`r`n			exit(0);`r`n		}")
    Set-Content -Path (Join-Path $BuildDir "DPhull.c") -Value $mainText

    Copy-Item -Path (Join-Path $SourceDir "PH.c") -Destination (Join-Path $BuildDir "PH.c") -Force
}

function Ensure-DpHullExecutable {
    param(
        [string]$WorkspaceRoot,
        [string]$BuildDir,
        [string]$ExePath
    )

    Prepare-DpHullSources -SourceDir (Join-Path $WorkspaceRoot "batch\DPhull") -BuildDir $BuildDir
    & gcc (Join-Path $BuildDir "DP.c") (Join-Path $BuildDir "DPhull.c") (Join-Path $BuildDir "PH.c") -lm -o $ExePath
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile DPhull"
    }
}

if (-not (Test-Path $DatasetRoot)) {
    throw "Dataset root not found: $DatasetRoot"
}

$epsilonTag = $EpsilonTag
$testRoot = Join-Path $WorkspaceRoot ("test\DPhull_test_Geolife_{0}" -f $epsilonTag)
$convertedDir = Join-Path $testRoot "converted"
$resultsDir = Join-Path $testRoot "results"
$buildDir = Join-Path $testRoot "build_src"
$binDir = Join-Path $testRoot "bin"
$summaryPath = Join-Path $testRoot "dphull_geolife_summary.csv"
$metricsPath = Join-Path $testRoot "dphull_geolife_metrics_summary.csv"
$masterInfoPath = Join-Path $testRoot "run_info.txt"

Ensure-Directory $testRoot
Ensure-Directory $convertedDir
Ensure-Directory $resultsDir
Ensure-Directory $buildDir
Ensure-Directory $binDir

$exePath = Join-Path $binDir "hull.exe"
Ensure-DpHullExecutable -WorkspaceRoot $WorkspaceRoot -BuildDir $buildDir -ExePath $exePath

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

    $convertedPath = Join-Path $convertedSubdir ($baseName + ".dphull_input.plt")
    $resultPath = Join-Path $resultsSubdir ($baseName + ".dphull_output.csv")

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

    if ($convertedLines.Count -eq 0) {
        continue
    }

    Set-Content -Path $convertedPath -Value $convertedLines

    & $exePath $convertedPath $convertedLines.Count $Epsilon $resultPath
    if ($LASTEXITCODE -ne 0) {
        throw "DPhull failed on $($file.FullName)"
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
        algorithm = "DPhull"
        dataset = "Geolife"
        epsilon = $epsilonTag
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
$compressionFractionSaved = if ($inputTotals.Sum -gt 0) {
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
    algorithm = "DPhull"
    dataset = "Geolife"
    epsilon = $epsilonTag
    processed_files = $rows.Count
    total_input_points = [int64]$inputTotals.Sum
    total_output_points = [int64]$outputTotals.Sum
    total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
    overall_compression_ratio = $overallCompressionRatio
    saved_ratio = $compressionFractionSaved
    avg_file_compression_ratio = [Math]::Round($fileCompressionAverage, 6)
    avg_file_runtime_seconds = [Math]::Round($fileRuntimeAverage, 6)
    input_points_per_second = $pointsPerSecond
    output_points_per_second = $outputPointsPerSecond
    metrics_note = "compression_ratio=output_points/input_points; speed uses total_input_points/total_runtime_seconds"
} | Export-Csv -Path $metricsPath -NoTypeInformation -Encoding UTF8

@(
    "algorithm=DPhull"
    "dataset=Geolife"
    ("epsilon={0}" -f $epsilonTag)
    ("processed_files={0}" -f $rows.Count)
    ("total_input_points={0}" -f [int64]$inputTotals.Sum)
    ("total_output_points={0}" -f [int64]$outputTotals.Sum)
    ("overall_compression_ratio={0}" -f $overallCompressionRatio)
    ("saved_ratio={0}" -f $compressionFractionSaved)
    ("total_runtime_seconds={0}" -f [Math]::Round($runtimeTotals.Sum, 6))
    ("avg_file_compression_ratio={0}" -f [Math]::Round($fileCompressionAverage, 6))
    ("avg_file_runtime_seconds={0}" -f [Math]::Round($fileRuntimeAverage, 6))
    ("input_points_per_second={0}" -f $pointsPerSecond)
    ("output_points_per_second={0}" -f $outputPointsPerSecond)
    "epsilon_unit=raw_coordinate_degrees"
    "distance_unit_note=DPhull uses perpendicular distance on latitude/longitude coordinates, not meters"
    ("summary_csv={0}" -f $summaryPath)
    ("metrics_csv={0}" -f $metricsPath)
) | Set-Content -Path $masterInfoPath

[pscustomobject]@{
    algorithm = "DPhull"
    dataset = "Geolife"
    epsilon = $epsilonTag
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
