param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "py"
)

$ErrorActionPreference = "Stop"

$prepareScript = Join-Path $WorkspaceRoot "batch\prepare_simple_converted.py"
& $PythonExe $prepareScript --datasets ais trajair
if ($LASTEXITCODE -ne 0) {
    throw "Failed to prepare shared converted inputs for ais/trajair"
}

& (Join-Path $WorkspaceRoot "tools\run_skwcl_algorithm_suite.ps1") `
    -WorkspaceRoot $WorkspaceRoot `
    -ResultRoot (Join-Path $WorkspaceRoot "skwcl") `
    -Algorithms @("DP", "DPhull", "OPW", "BQS", "OPERB", "OPW-TR", "TD-TR") `
    -Datasets @("ais", "trajair")
if ($LASTEXITCODE -ne 0) {
    throw "Batch/online threshold-based suite failed for ais/trajair"
}

$scripts = @(
    @{ Path = "batch\run_skwcl_dimweaver_batch.ps1"; NeedsPython = $true },
    @{ Path = "batch\run_skwcl_ciseds_missing_batch.ps1"; NeedsPython = $true },
    @{ Path = "batch\run_skwcl_cisedw_batch.ps1"; NeedsPython = $true },
    @{ Path = "batch\run_skwcl_mrpa_batch.ps1"; NeedsPython = $true },
    @{ Path = "batch\run_skwcl_dots_batch.ps1"; NeedsPython = $true },
    @{ Path = "batch\run_dpts_sp_intersect_batch.ps1"; NeedsPython = $false },
    @{ Path = "batch\run_skwcl_voltcom_batch.ps1"; NeedsPython = $false }
)

foreach ($scriptSpec in $scripts) {
    $scriptPath = Join-Path $WorkspaceRoot $scriptSpec.Path
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $scriptPath,
        "-WorkspaceRoot", $WorkspaceRoot
    )
    if ($scriptSpec.NeedsPython) {
        $args += @("-PythonExe", $PythonExe)
    }
    powershell @args
    if ($LASTEXITCODE -ne 0) {
        throw "Failed while running $($scriptSpec.Path)"
    }
}

foreach ($algorithm in @("Threshold", "Interval", "Persistence")) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $WorkspaceRoot "skwcl\run_online_small_algorithm.ps1") `
        -Algorithm $algorithm `
        -WorkspaceRoot $WorkspaceRoot `
        -SkwclRoot (Join-Path $WorkspaceRoot "skwcl")
    if ($LASTEXITCODE -ne 0) {
        throw "Failed while running online small algorithm $algorithm"
    }
}
