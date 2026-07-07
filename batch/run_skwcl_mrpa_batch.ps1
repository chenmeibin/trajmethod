param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "py"
)

$ErrorActionPreference = "Stop"

$datasets = @(
    @{ Name = "t-drive"; InputDir = "D:\all_traj_method\skwcl\TR-TR_result\converted" },
    @{ Name = "chengdu"; InputDir = "D:\all_traj_method\skwcl\TR-TR_result\chengdu\converted" },
    @{ Name = "geolife"; InputDir = "D:\all_traj_method\skwcl\TR-TR_result\geolife\converted" },
    @{ Name = "wuxi"; InputDir = "D:\all_traj_method\Data\TD-TR_wuxi_converted" },
    @{ Name = "ais"; InputDir = "D:\all_traj_method\skwcl\_shared_converted\ais" },
    @{ Name = "trajair"; InputDir = "D:\all_traj_method\skwcl\_shared_converted\trajair" }
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
        $runInfoPath = Join-Path $WorkspaceRoot ("skwcl\MRPA_result\{0}\{1}\run_info.txt" -f $dataset.Name, $threshold.Tag)
        if (Test-Path $runInfoPath) {
            Write-Host "=== MRPA $($dataset.Name) $($threshold.Tag) SKIP ==="
            continue
        }

        Write-Host "=== MRPA $($dataset.Name) $($threshold.Tag) ==="
        powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $WorkspaceRoot "batch\run_mrpa_converted_dataset.ps1") `
            -WorkspaceRoot $WorkspaceRoot `
            -DatasetName $dataset.Name `
            -DatasetRoot (Join-Path $WorkspaceRoot ("skwcl\MRPA_result\{0}" -f $dataset.Name)) `
            -InputDir $dataset.InputDir `
            -LISSEDth $threshold.Value `
            -ThresholdTag $threshold.Tag `
            -PythonExe $PythonExe
        if ($LASTEXITCODE -ne 0) { throw "MRPA failed for $($dataset.Name) $($threshold.Tag)" }
    }
}
