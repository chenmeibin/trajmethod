param(
    [ValidateSet("STTrace", "SQUISH")]
    [string]$Algorithm,
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "py",
    [int]$CompressionTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

$resultDirName = if ($Algorithm -eq "STTrace") { "STTrace_result" } else { "SQUISH_result" }

& (Join-Path $WorkspaceRoot "batch\run_ratio_budget_algorithm_batch.ps1") `
    -WorkspaceRoot $WorkspaceRoot `
    -Algorithm $Algorithm `
    -ResultDirName $resultDirName `
    -PythonExe $PythonExe `
    -IncludeDatasets @("ais", "trajair") `
    -CompressionTimeoutSeconds $CompressionTimeoutSeconds `
    -Resume
