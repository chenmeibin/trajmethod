param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$SmokeRoot = "D:\all_traj_method\tmp\batch_smoke",
    [string]$ChengduFile = "D:\all_traj_method\data\chengdu\1.txt",
    [double]$ChengduEpsilon = 1e-5,
    [string]$TDriveFile = "D:\all_traj_method\data\T-drive\1.txt",
    [double]$TDriveEpsilon = 1.0e-4,
    [int]$DptsAlgorithm = 6,
    [double]$DptsErrorTolerance = 1.0,
    [int]$MinErrorAlgorithm = 3,
    [double]$MinErrorBudget = 0.7
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

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

function Convert-ChengduToPlt {
    param(
        [string]$InputPath,
        [string]$OutputPath
    )

    $converted = New-Object System.Collections.Generic.List[string]
    $lineIndex = 0

    foreach ($line in Get-Content $InputPath) {
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
            $converted.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
        }
        catch {
            if ($lineIndex -eq 1) {
                continue
            }
        }
    }

    if ($converted.Count -eq 0) {
        throw "No Chengdu points were converted from $InputPath"
    }

    Set-Content -Path $OutputPath -Value $converted
    return $converted.Count
}

function Convert-TDriveToPlt {
    param(
        [string]$InputPath,
        [string]$OutputPath
    )

    $converted = New-Object System.Collections.Generic.List[string]

    foreach ($line in Get-Content $InputPath) {
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
            $converted.Add(("{0} {1} {2}" -f $lat, $lon, $epoch))
        }
        catch {
            continue
        }
    }

    if ($converted.Count -eq 0) {
        throw "No T-drive points were converted from $InputPath"
    }

    Set-Content -Path $OutputPath -Value $converted
    return $converted.Count
}

function Ensure-CompiledBinary {
    param(
        [string]$SourcePath,
        [string]$OutputPath
    )

    $needsCompile = -not (Test-Path $OutputPath)
    if (-not $needsCompile) {
        $needsCompile = (Get-Item $SourcePath).LastWriteTimeUtc -gt (Get-Item $OutputPath).LastWriteTimeUtc
    }

    if ($needsCompile) {
        & g++ $SourcePath -o $OutputPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile $SourcePath"
        }
    }
}

function Ensure-CompiledProjectBinary {
    param(
        [string]$ProjectGlob,
        [string]$OutputPath
    )

    $sources = Get-ChildItem $ProjectGlob | Sort-Object FullName
    if ($sources.Count -eq 0) {
        throw "No sources found for $ProjectGlob"
    }

    $needsCompile = -not (Test-Path $OutputPath)
    if (-not $needsCompile) {
        $binaryTime = (Get-Item $OutputPath).LastWriteTimeUtc
        foreach ($source in $sources) {
            if ($source.LastWriteTimeUtc -gt $binaryTime) {
                $needsCompile = $true
                break
            }
        }
    }

    if ($needsCompile) {
        & g++ -g $sources.FullName -o $OutputPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile sources under $ProjectGlob"
        }
    }
}

Ensure-Directory $SmokeRoot

$convertedDir = Join-Path $SmokeRoot "converted"
$resultDir = Join-Path $SmokeRoot "results"
$runDir = Join-Path $SmokeRoot "runs"

Ensure-Directory $convertedDir
Ensure-Directory $resultDir
Ensure-Directory $runDir

$dpExe = Join-Path $SmokeRoot "DP.exe"
$tdtrExe = Join-Path $SmokeRoot "TD-TR.exe"
$dptsExe = Join-Path $SmokeRoot "DPTS.exe"
$minErrorExe = Join-Path $SmokeRoot "MinError.exe"

Ensure-CompiledBinary -SourcePath (Join-Path $WorkspaceRoot "batch\DP\DP.cpp") -OutputPath $dpExe
Ensure-CompiledBinary -SourcePath (Join-Path $WorkspaceRoot "batch\TD-TR\TD-TR.cpp") -OutputPath $tdtrExe
Ensure-CompiledProjectBinary -ProjectGlob (Join-Path $WorkspaceRoot "batch\DPTS\src\*.cpp") -OutputPath $dptsExe
Ensure-CompiledProjectBinary -ProjectGlob (Join-Path $WorkspaceRoot "batch\MinError\src\*.cpp") -OutputPath $minErrorExe

$chengduPlt = Join-Path $convertedDir "chengdu_1.plt"
$tDrivePlt = Join-Path $convertedDir "tdrive_1.plt"

$chengduInputPoints = Convert-ChengduToPlt -InputPath $ChengduFile -OutputPath $chengduPlt
$tDriveInputPoints = Convert-TDriveToPlt -InputPath $TDriveFile -OutputPath $tDrivePlt
$tDriveRawPoints = (Get-Content $TDriveFile).Count

$rows = New-Object System.Collections.Generic.List[object]

function Add-Row {
    param(
        [string]$Algorithm,
        [string]$Dataset,
        [string]$InputFile,
        [string]$Parameter,
        [string]$Status,
        [string]$OutputFile,
        [string]$Notes,
        [Nullable[int]]$InputPoints,
        [Nullable[int]]$OutputPoints,
        [Nullable[double]]$Metric1,
        [Nullable[double]]$Metric2
    )

    $rows.Add([pscustomobject]@{
        algorithm = $Algorithm
        dataset = $Dataset
        input_file = $InputFile
        parameter = $Parameter
        status = $Status
        input_points = $InputPoints
        output_points = $OutputPoints
        metric_1 = $Metric1
        metric_2 = $Metric2
        output_file = $OutputFile
        notes = $Notes
    }) | Out-Null
}

try {
    $dpChengduOut = Join-Path $resultDir "dp_chengdu_1.csv"
    & $dpExe $chengduPlt $ChengduEpsilon $dpChengduOut
    $dpChengduLines = Get-Content $dpChengduOut
    Add-Row -Algorithm "DP" -Dataset "chengdu" -InputFile $ChengduFile -Parameter ("epsilon={0}" -f $ChengduEpsilon) -Status "ok" -OutputFile $dpChengduOut -Notes "lat lon epoch input converted from raw Chengdu text" -InputPoints $chengduInputPoints -OutputPoints ($dpChengduLines.Count - 1) -Metric1 ([double]$dpChengduLines[-1]) -Metric2 ($null)
}
catch {
    Add-Row -Algorithm "DP" -Dataset "chengdu" -InputFile $ChengduFile -Parameter ("epsilon={0}" -f $ChengduEpsilon) -Status "failed" -OutputFile "" -Notes $_.Exception.Message -InputPoints $chengduInputPoints -OutputPoints ($null) -Metric1 ($null) -Metric2 ($null)
}

try {
    $tdtrChengduOut = Join-Path $resultDir "tdtr_chengdu_1.csv"
    & $tdtrExe $chengduPlt $ChengduEpsilon $tdtrChengduOut
    $tdtrChengduLines = Get-Content $tdtrChengduOut
    Add-Row -Algorithm "TD-TR" -Dataset "chengdu" -InputFile $ChengduFile -Parameter ("epsilon={0}" -f $ChengduEpsilon) -Status "ok" -OutputFile $tdtrChengduOut -Notes "lat lon epoch input converted from raw Chengdu text" -InputPoints $chengduInputPoints -OutputPoints ($tdtrChengduLines.Count - 1) -Metric1 ([double]$tdtrChengduLines[-1]) -Metric2 ($null)
}
catch {
    Add-Row -Algorithm "TD-TR" -Dataset "chengdu" -InputFile $ChengduFile -Parameter ("epsilon={0}" -f $ChengduEpsilon) -Status "failed" -OutputFile "" -Notes $_.Exception.Message -InputPoints $chengduInputPoints -OutputPoints ($null) -Metric1 ($null) -Metric2 ($null)
}

try {
    $dpTDriveOut = Join-Path $resultDir "dp_tdrive_1.csv"
    & $dpExe $tDrivePlt $TDriveEpsilon $dpTDriveOut
    $dpTDriveLines = Get-Content $dpTDriveOut
    Add-Row -Algorithm "DP" -Dataset "T-drive" -InputFile $TDriveFile -Parameter ("epsilon={0}" -f $TDriveEpsilon) -Status "ok" -OutputFile $dpTDriveOut -Notes "lat lon epoch input converted from raw T-drive text" -InputPoints $tDriveInputPoints -OutputPoints ($dpTDriveLines.Count - 1) -Metric1 ([double]$dpTDriveLines[-1]) -Metric2 ($null)
}
catch {
    Add-Row -Algorithm "DP" -Dataset "T-drive" -InputFile $TDriveFile -Parameter ("epsilon={0}" -f $TDriveEpsilon) -Status "failed" -OutputFile "" -Notes $_.Exception.Message -InputPoints $tDriveInputPoints -OutputPoints ($null) -Metric1 ($null) -Metric2 ($null)
}

try {
    $tdtrTDriveOut = Join-Path $resultDir "tdtr_tdrive_1.csv"
    & $tdtrExe $tDrivePlt $TDriveEpsilon $tdtrTDriveOut
    $tdtrTDriveLines = Get-Content $tdtrTDriveOut
    Add-Row -Algorithm "TD-TR" -Dataset "T-drive" -InputFile $TDriveFile -Parameter ("epsilon={0}" -f $TDriveEpsilon) -Status "ok" -OutputFile $tdtrTDriveOut -Notes "lat lon epoch input converted from raw T-drive text" -InputPoints $tDriveInputPoints -OutputPoints ($tdtrTDriveLines.Count - 1) -Metric1 ([double]$tdtrTDriveLines[-1]) -Metric2 ($null)
}
catch {
    Add-Row -Algorithm "TD-TR" -Dataset "T-drive" -InputFile $TDriveFile -Parameter ("epsilon={0}" -f $TDriveEpsilon) -Status "failed" -OutputFile "" -Notes $_.Exception.Message -InputPoints $tDriveInputPoints -OutputPoints ($null) -Metric1 ($null) -Metric2 ($null)
}

try {
    $dptsRunDir = Join-Path $runDir "dpts"
    Ensure-Directory $dptsRunDir
    Set-Content -Path (Join-Path $dptsRunDir "config.txt") -Value ("{0} {1}`n{2} {3}`n2" -f ($TDriveFile -replace "\\", "/"), $tDriveRawPoints, $DptsErrorTolerance, $DptsAlgorithm)
    Push-Location $dptsRunDir
    try {
        & $dptsExe
        $dptsStat = Get-Content (Join-Path $dptsRunDir "stat.txt")
    }
    finally {
        Pop-Location
    }
    Add-Row -Algorithm "DPTS" -Dataset "T-drive" -InputFile $TDriveFile -Parameter ("eps={0};alg={1}" -f $DptsErrorTolerance, $DptsAlgorithm) -Status "ok" -OutputFile (Join-Path $dptsRunDir "stat.txt") -Notes "metric_1=runtime_seconds, metric_2=memory_mb" -InputPoints ([int]$dptsStat[0]) -OutputPoints ([int]$dptsStat[1]) -Metric1 ([double]$dptsStat[3]) -Metric2 ([double]$dptsStat[4])
}
catch {
    Add-Row -Algorithm "DPTS" -Dataset "T-drive" -InputFile $TDriveFile -Parameter ("eps={0};alg={1}" -f $DptsErrorTolerance, $DptsAlgorithm) -Status "failed" -OutputFile "" -Notes $_.Exception.Message -InputPoints $tDriveRawPoints -OutputPoints ($null) -Metric1 ($null) -Metric2 ($null)
}

try {
    $minErrorRunDir = Join-Path $runDir "minerror"
    Ensure-Directory $minErrorRunDir
    Set-Content -Path (Join-Path $minErrorRunDir "config.txt") -Value ("{0} {1}`n{2} {3}`n2" -f ($TDriveFile -replace "\\", "/"), $tDriveRawPoints, $MinErrorBudget, $MinErrorAlgorithm)
    Push-Location $minErrorRunDir
    try {
        & $minErrorExe
        $minErrorStat = Get-Content (Join-Path $minErrorRunDir "stat.txt")
    }
    finally {
        Pop-Location
    }
    Add-Row -Algorithm "MinError" -Dataset "T-drive" -InputFile $TDriveFile -Parameter ("budget={0};alg={1}" -f $MinErrorBudget, $MinErrorAlgorithm) -Status "ok" -OutputFile (Join-Path $minErrorRunDir "stat.txt") -Notes "metric_1=direction_error, metric_2=memory_mb" -InputPoints $tDriveRawPoints -OutputPoints ($null) -Metric1 ([double]$minErrorStat[0]) -Metric2 ([double]$minErrorStat[3])
}
catch {
    Add-Row -Algorithm "MinError" -Dataset "T-drive" -InputFile $TDriveFile -Parameter ("budget={0};alg={1}" -f $MinErrorBudget, $MinErrorAlgorithm) -Status "failed" -OutputFile "" -Notes $_.Exception.Message -InputPoints $tDriveRawPoints -OutputPoints ($null) -Metric1 ($null) -Metric2 ($null)
}

$summaryPath = Join-Path $SmokeRoot "smoke_summary.csv"
$rows | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8
$rows | Format-Table -AutoSize
Write-Output ""
Write-Output ("Summary CSV: {0}" -f $summaryPath)
