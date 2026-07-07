param(
    [string]$WorkspaceRoot = "D:\all_traj_method"
)

$ErrorActionPreference = "Stop"

$preparedRoot = Join-Path $WorkspaceRoot "batch\DPTS_prepared"
if (-not (Test-Path $preparedRoot)) {
    & py -3 (Join-Path $WorkspaceRoot "batch\prepare_dpts_inputs.py") --output-root $preparedRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to prepare DPTS inputs"
    }
}

$datasets = @("t-drive", "chengdu", "geolife", "wuxi")
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
            $runInfoPath = Join-Path $algorithm.OutputRoot ("{0}\{1}\run_info.txt" -f $dataset, $threshold.Tag)
            if (Test-Path $runInfoPath) {
                Write-Host ("=== {0} {1} {2} SKIP ===" -f $algorithm.Label, $dataset, $threshold.Tag)
                continue
            }

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
