param(
    [string]$WorkspaceRoot = "D:\all_traj_method"
)

$ErrorActionPreference = "Stop"

$preparedRoot = Join-Path $WorkspaceRoot "batch\DPTS_prepared"
$logRoot = Join-Path $WorkspaceRoot "skwcl\_batch_logs"
$doneFlag = Join-Path $logRoot "DPTS_SP_INTERSECT_COMPLETE.flag"
$doneText = Join-Path $logRoot "DPTS_SP_INTERSECT_COMPLETE.txt"

if (Test-Path $doneFlag) {
    Remove-Item -LiteralPath $doneFlag -Force
}
if (Test-Path $doneText) {
    Remove-Item -LiteralPath $doneText -Force
}

& py -3 (Join-Path $WorkspaceRoot "batch\prepare_dpts_inputs.py") --output-root $preparedRoot
if ($LASTEXITCODE -ne 0) {
    throw "Failed to prepare DPTS inputs"
}

$datasets = @("t-drive", "chengdu", "geolife", "wuxi", "ais", "trajair")
$thresholds = @(
    @{ Tag = "1e-3"; Value = 1e-3 }
    @{ Tag = "5e-4"; Value = 5e-4 }
    @{ Tag = "1e-4"; Value = 1e-4 }
    @{ Tag = "5e-5"; Value = 5e-5 }
    @{ Tag = "1e-5"; Value = 1e-5 }
)
$algorithms = @(
    @{ Label = "SP"; AlgOpt = 5; OutputRoot = (Join-Path $WorkspaceRoot "skwcl\SP_result") }
    @{ Label = "intersect"; AlgOpt = 6; OutputRoot = (Join-Path $WorkspaceRoot "skwcl\intersect_result") }
)

foreach ($algorithm in $algorithms) {
    foreach ($dataset in $datasets) {
        foreach ($threshold in $thresholds) {
            Write-Host ("=== {0} {1} {2} ===" -f $algorithm.Label, $dataset, $threshold.Tag)
            powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $WorkspaceRoot "batch\run_dpts_algorithm_dataset.ps1") `
                -WorkspaceRoot $WorkspaceRoot `
                -PreparedInputRoot $preparedRoot `
                -DatasetName $dataset `
                -Epsilon $threshold.Value `
                -ThresholdLabel $threshold.Tag `
                -AlgOpt $algorithm.AlgOpt `
                -AlgorithmLabel $algorithm.Label `
                -OutputRoot $algorithm.OutputRoot
            if ($LASTEXITCODE -ne 0) {
                throw ("DPTS {0} failed for {1} {2}" -f $algorithm.Label, $dataset, $threshold.Tag)
            }
        }
    }
}

$completedAt = Get-Date
@(
    "status=done"
    ("completed_at={0}" -f $completedAt.ToString("yyyy-MM-dd HH:mm:ss"))
    ("workspace_root={0}" -f $WorkspaceRoot)
    ("sp_root={0}" -f (Join-Path $WorkspaceRoot "skwcl\SP_result"))
    ("intersect_root={0}" -f (Join-Path $WorkspaceRoot "skwcl\intersect_result"))
) | Set-Content -Path $doneFlag

@(
    "DPTS SP and intersect batch is complete."
    ("completed_at={0}" -f $completedAt.ToString("yyyy-MM-dd HH:mm:ss"))
    ("sp_root={0}" -f (Join-Path $WorkspaceRoot "skwcl\SP_result"))
    ("intersect_root={0}" -f (Join-Path $WorkspaceRoot "skwcl\intersect_result"))
) | Set-Content -Path $doneText

[console]::beep(900, 700)
[console]::beep(1200, 700)
