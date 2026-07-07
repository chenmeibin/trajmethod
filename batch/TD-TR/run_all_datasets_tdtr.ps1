param(
    [string]$DataRoot = "D:\all_traj_method\data",
    [string]$OutputRoot = "D:\all_traj_method\data\TD-TR-batch-runs"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceFile = Join-Path $scriptDir "TD-TR.cpp"
$exeFile = Join-Path $scriptDir "TD-TR.exe"
$masterSummary = Join-Path $OutputRoot "TD-TR_master_summary.csv"

$datasets = @(
    @{ Name = "chengdu"; Path = (Join-Path $DataRoot "chengdu"); Pattern = "*.txt"; Format = "chengdu" },
    @{ Name = "T-drive"; Path = (Join-Path $DataRoot "T-drive"); Pattern = "*.txt"; Format = "tdrive" },
    @{ Name = "Geolife"; Path = (Join-Path $DataRoot "Geolife"); Pattern = "*.plt"; Format = "geolife" }
)

$epsilons = @("1e-5", "5e-5", "1e-4", "5e-4", "3e-4")

function Convert-ToUnixSeconds {
    param(
        [string]$DateText,
        [string[]]$Formats
    )

    foreach ($format in $Formats) {
        try {
            $dt = [datetime]::ParseExact($DateText, $format, [System.Globalization.CultureInfo]::InvariantCulture)
            return [int64][DateTimeOffset]::new($dt).ToUnixTimeSeconds()
        }
        catch {
            continue
        }
    }

    throw "Unsupported datetime format: $DateText"
}

function Get-ConvertedLines {
    param(
        [System.IO.FileInfo]$File,
        [string]$Format
    )

    $convertedLines = New-Object System.Collections.Generic.List[string]

    switch ($Format) {
        "tdrive" {
            foreach ($line in Get-Content $File.FullName) {
                if ([string]::IsNullOrWhiteSpace($line)) {
                    continue
                }

                $parts = $line.Split(",")
                if ($parts.Length -lt 4) {
                    continue
                }

                try {
                    $epoch = Convert-ToUnixSeconds -DateText $parts[1].Trim() -Formats @("yyyy-MM-dd HH:mm:ss")
                    $lon = [double]$parts[2]
                    $lat = [double]$parts[3]
                    $convertedLines.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
                }
                catch {
                    continue
                }
            }
        }
        "chengdu" {
            $lineIndex = 0
            foreach ($line in Get-Content $File.FullName) {
                $lineIndex += 1
                if ([string]::IsNullOrWhiteSpace($line)) {
                    continue
                }

                $parts = $line.Split(",")
                if ($parts.Length -lt 3) {
                    continue
                }

                try {
                    $epoch = Convert-ToUnixSeconds -DateText $parts[0].Trim() -Formats @("yyyy-MM-dd HH:mm:ss.0", "yyyy-MM-dd HH:mm:ss")
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
        }
        "geolife" {
            $lineIndex = 0
            foreach ($line in Get-Content $File.FullName) {
                $lineIndex += 1
                if ($lineIndex -le 6 -or [string]::IsNullOrWhiteSpace($line)) {
                    continue
                }

                $parts = $line.Split(",")
                if ($parts.Length -lt 7) {
                    continue
                }

                try {
                    $epoch = Convert-ToUnixSeconds -DateText (($parts[5].Trim() + " " + $parts[6].Trim())) -Formats @("yyyy-MM-dd HH:mm:ss")
                    $lat = [double]$parts[0]
                    $lon = [double]$parts[1]
                    $convertedLines.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
                }
                catch {
                    continue
                }
            }
        }
        default {
            throw "Unsupported dataset format: $Format"
        }
    }

    return $convertedLines
}

function Ensure-TdtrExecutable {
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
}

Ensure-TdtrExecutable
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$masterRows = New-Object System.Collections.Generic.List[object]

foreach ($dataset in $datasets) {
    if (-not (Test-Path $dataset.Path)) {
        throw "Dataset path not found: $($dataset.Path)"
    }

    $files = Get-ChildItem $dataset.Path -Recurse -File -Filter $dataset.Pattern | Sort-Object FullName

    foreach ($epsilonText in $epsilons) {
        $epsilonValue = [double]::Parse($epsilonText, [System.Globalization.CultureInfo]::InvariantCulture)
        $runTag = "TD-TR_{0}_{1}" -f $dataset.Name, $epsilonText
        $convertedDir = Join-Path $OutputRoot ($runTag + "_converted")
        $resultsDir = Join-Path $OutputRoot ($runTag + "_results")
        $summaryPath = Join-Path $OutputRoot ($runTag + "_summary.csv")

        New-Item -ItemType Directory -Force -Path $convertedDir | Out-Null
        New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

        $rows = New-Object System.Collections.Generic.List[object]

        foreach ($file in $files) {
            $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
            $convertedPath = Join-Path $convertedDir ("{0}_TD-TR_{1}_{2}.plt" -f $baseName, $dataset.Name, $epsilonText)
            $resultPath = Join-Path $resultsDir ("{0}_TD-TR_{1}_{2}.csv" -f $baseName, $dataset.Name, $epsilonText)

            $convertedLines = Get-ConvertedLines -File $file -Format $dataset.Format
            if ($convertedLines.Count -eq 0) {
                continue
            }

            Set-Content -Path $convertedPath -Value $convertedLines

            & $exeFile $convertedPath $epsilonValue $resultPath
            if ($LASTEXITCODE -ne 0) {
                throw "TD-TR failed on $($file.FullName) with epsilon $epsilonText"
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
                algorithm = "TD-TR"
                dataset = $dataset.Name
                epsilon = $epsilonText
                file_name = $file.Name
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

        $masterRows.Add([pscustomobject]@{
            algorithm = "TD-TR"
            dataset = $dataset.Name
            epsilon = $epsilonText
            processed_files = $rows.Count
            total_input_points = [int64]$inputTotals.Sum
            total_output_points = [int64]$outputTotals.Sum
            overall_compression_ratio = if ($inputTotals.Sum -gt 0) {
                [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6)
            }
            else {
                0
            }
            total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
            summary_file = $summaryPath
            results_dir = $resultsDir
        }) | Out-Null
    }
}

$masterRows | Export-Csv -Path $masterSummary -NoTypeInformation -Encoding UTF8
$masterRows | Format-Table -AutoSize
