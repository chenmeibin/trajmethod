param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [int]$MaxFiles = 0,
    [int]$CompressionTimeoutSeconds = 30,
    [switch]$Resume = $true
)

$ErrorActionPreference = "Stop"

$runner = Join-Path $WorkspaceRoot "batch\run_trajic_dataset_skwcl.ps1"
$resultRoot = Join-Path $WorkspaceRoot "skwcl\trajic_result"
if (-not (Test-Path $resultRoot)) {
    New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null
}

$datasets = @(
    @{ Name = "chengdu"; InputDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\chengdu") },
    @{ Name = "geolife"; InputDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\Geolife") },
    @{ Name = "t-drive"; InputDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\T-drive") },
    @{ Name = "wuxi"; InputDir = (Join-Path $WorkspaceRoot "skwcl\_shared_converted\wuxi") }
)

$thresholds = @(
    @{ Tag = "1e-5"; Value = "1e-5" },
    @{ Tag = "5e-5"; Value = "5e-5" },
    @{ Tag = "1e-4"; Value = "1e-4" },
    @{ Tag = "5e-4"; Value = "5e-4" },
    @{ Tag = "1e-3"; Value = "1e-3" }
)

$allSummaries = New-Object System.Collections.Generic.List[object]

foreach ($dataset in $datasets) {
    $datasetRoot = Join-Path $resultRoot $dataset.Name
    foreach ($threshold in $thresholds) {
        Write-Host "=== Trajic $($dataset.Name) $($threshold.Tag) ==="
        $args = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", $runner,
            "-WorkspaceRoot", $WorkspaceRoot,
            "-DatasetName", $dataset.Name,
            "-InputDir", $dataset.InputDir,
            "-DatasetRoot", $datasetRoot,
            "-SpatialError", $threshold.Value,
            "-ThresholdTag", $threshold.Tag,
            "-TemporalError", "0",
            "-MaxFiles", $MaxFiles,
            "-CompressionTimeoutSeconds", $CompressionTimeoutSeconds
        )
        if ($Resume) {
            $args += "-Resume"
        }
        powershell @args
        if ($LASTEXITCODE -ne 0) {
            throw "Trajic failed for $($dataset.Name) $($threshold.Tag)"
        }

        $summaryPath = Join-Path $datasetRoot (Join-Path $threshold.Tag "summary.csv")
        if (Test-Path $summaryPath) {
            $allSummaries.Add((Import-Csv $summaryPath | Select-Object -First 1)) | Out-Null
        }
    }
}

$masterSummaryPath = Join-Path $resultRoot "trajic_master_summary.csv"
$allSummaries | Export-Csv -Path $masterSummaryPath -NoTypeInformation -Encoding UTF8
Write-Output "Saved Trajic master summary to: $masterSummaryPath"
