param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "py",
    [int]$MaxFiles = 0
)

$ErrorActionPreference = "Stop"

$prepareScript = Join-Path $WorkspaceRoot "batch\prepare_pilotc_converted_xy.py"
$runner = Join-Path $WorkspaceRoot "batch\run_pilotc_simple_dataset_full.ps1"
$resultRoot = Join-Path $WorkspaceRoot "skwcl\PILOT-C_result"
$preparedRoot = Join-Path $WorkspaceRoot "skwcl\_pilotc_prepared"
New-Item -ItemType Directory -Force -Path $resultRoot | Out-Null
New-Item -ItemType Directory -Force -Path $preparedRoot | Out-Null

$datasets = @(
    @{
        Name = "chengdu"
        SourceDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\chengdu")
        A = 0.75
        B = 25.0
        C = 0.8
    },
    @{
        Name = "geolife"
        SourceDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\Geolife")
        A = 0.5
        B = 25.0
        C = 0.8
    },
    @{
        Name = "t-drive"
        SourceDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\T-drive")
        A = 0.75
        B = 10.0
        C = 0.6
    },
    @{
        Name = "wuxi"
        SourceDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\wuxi")
        A = 1.0
        B = 25.0
        C = 1.1
    },
    @{
        Name = "ais"
        SourceDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\ais")
        A = 0.75
        B = 25.0
        C = 0.8
    },
    @{
        Name = "trajair"
        SourceDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\trajair")
        A = 0.75
        B = 25.0
        C = 0.8
    }
)

$allSummaries = New-Object System.Collections.Generic.List[object]
$thresholdTags = @("1e-5", "5e-5", "1e-4", "5e-4", "1e-3")

foreach ($dataset in $datasets) {
    $preparedDir = Join-Path $preparedRoot $dataset.Name
    if (-not (Test-Path $preparedDir) -or -not (Get-ChildItem -Path $preparedDir -File -ErrorAction SilentlyContinue | Select-Object -First 1)) {
        Write-Host "=== Prepare PILOT-C input $($dataset.Name) ==="
        & $PythonExe $prepareScript --input-dir $dataset.SourceDir --output-dir $preparedDir
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to prepare PILOT-C input for $($dataset.Name)"
        }
    }

    Write-Host "=== PILOT-C $($dataset.Name) ==="
    powershell -NoProfile -ExecutionPolicy Bypass -File $runner `
        -DatasetName $dataset.Name `
        -InputDir $preparedDir `
        -DatasetRoot (Join-Path $resultRoot $dataset.Name) `
        -DataSource "geolife" `
        -A $dataset.A `
        -B $dataset.B `
        -C $dataset.C `
        -Accuracy 0.5 `
        -MaxFiles $MaxFiles
    if ($LASTEXITCODE -ne 0) {
        throw "PILOT-C failed for $($dataset.Name)"
    }

    foreach ($tag in $thresholdTags) {
        $summaryPath = Join-Path $resultRoot (Join-Path $dataset.Name (Join-Path $tag "summary.csv"))
        if (Test-Path $summaryPath) {
            $allSummaries.Add((Import-Csv $summaryPath | Select-Object -First 1)) | Out-Null
        }
    }
}

$masterSummaryPath = Join-Path $resultRoot "pilotc_master_summary.csv"
$allSummaries | Export-Csv -Path $masterSummaryPath -NoTypeInformation -Encoding UTF8
Write-Output "Saved PILOT-C master summary to: $masterSummaryPath"
