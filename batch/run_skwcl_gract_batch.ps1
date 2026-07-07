param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "py",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$datasets = @(
    @{ Name = "chengdu"; Root = (Join-Path $WorkspaceRoot "data_small\chengdu") },
    @{ Name = "Geolife"; Root = (Join-Path $WorkspaceRoot "data_small\Geolife") },
    @{ Name = "T-drive"; Root = (Join-Path $WorkspaceRoot "data_small\T-drive") },
    @{ Name = "wuxi"; Root = (Join-Path $WorkspaceRoot "data_small\wuxi") }
)

$thresholds = @(
    @{ Tag = "10"; Value = 10 },
    @{ Tag = "100"; Value = 100 },
    @{ Tag = "500"; Value = 500 },
    @{ Tag = "1000"; Value = 1000 },
    @{ Tag = "2000"; Value = 2000 }
)

foreach ($dataset in $datasets) {
    foreach ($threshold in $thresholds) {
        Write-Host "=== GraCT $($dataset.Name) $($threshold.Tag) ==="
        $args = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", (Join-Path $WorkspaceRoot "batch\run_gract_dataset.ps1"),
            "-WorkspaceRoot", $WorkspaceRoot,
            "-DatasetName", $dataset.Name,
            "-DatasetRoot", $dataset.Root,
            "-PeriodSnapshot", $threshold.Value,
            "-ThresholdTag", $threshold.Tag,
            "-PythonExe", $PythonExe
        )
        if ($Force) {
            $args += "-Force"
        }

        powershell @args
        if ($LASTEXITCODE -ne 0) {
            throw "GraCT failed for $($dataset.Name) $($threshold.Tag)"
        }
    }
}
