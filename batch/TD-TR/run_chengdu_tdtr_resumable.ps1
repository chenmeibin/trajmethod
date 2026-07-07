param(
    [string]$RawDir = "D:\all_traj_method\data\chengdu",
    [string]$ConvertedDir = "D:\all_traj_method\data\TD-TR_chengdu_converted",
    [string]$ResultsDir = "D:\all_traj_method\data\TD-TR_chengdu_1e-5_results",
    [double]$Epsilon = 1e-5,
    [switch]$PrepareOnly,
    [int]$MaxNewResults = 0
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceFile = Join-Path $scriptDir "TD-TR.cpp"
$exeFile = Join-Path $scriptDir "TD-TR.exe"
$summaryFile = Join-Path $ResultsDir "summary.csv"

function Convert-ToEpoch {
    param([string]$Text)

    foreach ($fmt in @("yyyy-MM-dd HH:mm:ss.0", "yyyy-MM-dd HH:mm:ss")) {
        try {
            $dt = [datetime]::ParseExact($Text, $fmt, [System.Globalization.CultureInfo]::InvariantCulture)
            return [int64][DateTimeOffset]::new($dt).ToUnixTimeSeconds()
        }
        catch {
            continue
        }
    }

    throw "Unsupported Chengdu datetime: $Text"
}

if (-not (Test-Path $RawDir)) {
    throw "Raw dataset directory not found: $RawDir"
}

New-Item -ItemType Directory -Force -Path $ConvertedDir | Out-Null
New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

$needsCompile = -not (Test-Path $exeFile)
if (-not $needsCompile) {
    $needsCompile = (Get-Item $sourceFile).LastWriteTimeUtc -gt (Get-Item $exeFile).LastWriteTimeUtc
}

if ($needsCompile) {
    & g++ $sourceFile -o $exeFile
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile TD-TR.cpp"
    }
}

$files = Get-ChildItem $RawDir -File -Filter *.txt | Sort-Object Name
$convertedCount = 0
$resultCount = 0
$rows = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $convertedPath = Join-Path $ConvertedDir ($baseName + ".plt")
    $resultPath = Join-Path $ResultsDir ($baseName + "_tdtr.csv")

    if (-not (Test-Path $convertedPath)) {
        $convertedLines = New-Object System.Collections.Generic.List[string]
        $lineIndex = 0

        foreach ($line in Get-Content $file.FullName) {
            $lineIndex += 1
            if ([string]::IsNullOrWhiteSpace($line)) {
                continue
            }

            $parts = $line.Split(",")
            if ($parts.Length -lt 3) {
                continue
            }

            try {
                $epoch = Convert-ToEpoch $parts[0].Trim()
                $lon = [double]$parts[1]
                $lat = [double]$parts[2]
                $convertedLines.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
            }
            catch {
                if ($lineIndex -eq 1) {
                    continue
                }
                continue
            }
        }

        if ($convertedLines.Count -eq 0) {
            continue
        }

        Set-Content -Path $convertedPath -Value $convertedLines
        $convertedCount += 1
    }

    if ($PrepareOnly) {
        continue
    }

    if (-not (Test-Path $resultPath)) {
        if ($MaxNewResults -gt 0 -and $resultCount -ge $MaxNewResults) {
            break
        }
        & $exeFile $convertedPath $Epsilon $resultPath
        if ($LASTEXITCODE -ne 0) {
            throw "TD-TR failed on $($file.Name)"
        }
        $resultCount += 1
    }

    $outputLines = Get-Content $resultPath
    if ($outputLines.Count -eq 0) {
        continue
    }

    $inputPointCount = (Get-Content $convertedPath).Count
    $runtimeSeconds = [double]$outputLines[-1]
    $outputPointCount = [Math]::Max(0, $outputLines.Count - 1)
    $compressionRatio = if ($inputPointCount -gt 0) {
        [Math]::Round($outputPointCount / $inputPointCount, 6)
    }
    else {
        0
    }

    $rows.Add([pscustomobject]@{
        file_name = $file.Name
        input_points = $inputPointCount
        output_points = $outputPointCount
        compression_ratio = $compressionRatio
        runtime_seconds = [Math]::Round($runtimeSeconds, 6)
        converted_path = $convertedPath
        result_path = $resultPath
    }) | Out-Null
}

if (-not $PrepareOnly) {
    $rows | Export-Csv -Path $summaryFile -NoTypeInformation -Encoding UTF8

    $inputTotals = $rows | Measure-Object -Property input_points -Sum
    $outputTotals = $rows | Measure-Object -Property output_points -Sum
    $runtimeTotals = $rows | Measure-Object -Property runtime_seconds -Sum

    [pscustomobject]@{
        processed_files = $rows.Count
        newly_converted_files = $convertedCount
        newly_computed_results = $resultCount
        total_input_points = [int64]$inputTotals.Sum
        total_output_points = [int64]$outputTotals.Sum
        overall_compression_ratio = if ($inputTotals.Sum -gt 0) {
            [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6)
        }
        else {
            0
        }
        total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
        summary_file = $summaryFile
    } | Format-List
}
else {
    [pscustomobject]@{
        prepared_new_files = $convertedCount
        converted_directory = $ConvertedDir
    } | Format-List
}
