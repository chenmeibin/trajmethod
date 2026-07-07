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
    @{ Tag = "1e-3"; Deg = 1e-3 },
    @{ Tag = "5e-4"; Deg = 5e-4 },
    @{ Tag = "1e-4"; Deg = 1e-4 },
    @{ Tag = "5e-5"; Deg = 5e-5 },
    @{ Tag = "1e-5"; Deg = 1e-5 }
)

foreach ($dataset in $datasets) {
    foreach ($threshold in $thresholds) {
        $runInfoPath = Join-Path $WorkspaceRoot ("skwcl\DimWeaver_result\{0}\{1}\run_info.txt" -f $dataset.Name, $threshold.Tag)
        if ((Test-Path $runInfoPath) -and (Select-String -Path $runInfoPath -Pattern '^total_decompress_time_s=' -Quiet)) {
            Write-Host "=== DimWeaver $($dataset.Name) $($threshold.Tag) SKIP ==="
            continue
        }

        Write-Host "=== DimWeaver $($dataset.Name) $($threshold.Tag) ==="
        powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $WorkspaceRoot "batch\run_dimweaver_converted_dataset.ps1") `
            -WorkspaceRoot $WorkspaceRoot `
            -DatasetName $dataset.Name `
            -ThresholdLabel $threshold.Tag `
            -Epsilon $threshold.Deg `
            -InputDir $dataset.InputDir `
            -DatasetRoot (Join-Path $WorkspaceRoot ("skwcl\DimWeaver_result\{0}" -f $dataset.Name)) `
            -PythonExe $PythonExe
        if ($LASTEXITCODE -ne 0) { throw "DimWeaver failed for $($dataset.Name) $($threshold.Tag)" }
    }
}
