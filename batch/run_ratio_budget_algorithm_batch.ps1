param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [ValidateSet("Bellman", "SQUISH", "STTrace")]
    [string]$Algorithm,
    [string]$ResultDirName,
    [string]$PythonExe = "py",
    [switch]$Resume,
    [int]$CompressionTimeoutSeconds = 30,
    [string[]]$IncludeDatasets = @()
)

$ErrorActionPreference = "Stop"

$datasets = @(
    @{ Name = "chengdu"; InputDir = (Join-Path $WorkspaceRoot "data_small\chengdu") },
    @{ Name = "Geolife"; InputDir = (Join-Path $WorkspaceRoot "data_small\Geolife") },
    @{ Name = "T-drive"; InputDir = (Join-Path $WorkspaceRoot "data_small\T-drive") },
    @{ Name = "wuxi"; InputDir = (Join-Path $WorkspaceRoot "data_small\wuxi") },
    @{ Name = "ais"; InputDir = (Join-Path $WorkspaceRoot "Data\ais") },
    @{ Name = "trajair"; InputDir = (Join-Path $WorkspaceRoot "Data\trajair") }
)

if ($IncludeDatasets.Count -gt 0) {
    $includeSet = @{}
    foreach ($name in $IncludeDatasets) {
        if (-not [string]::IsNullOrWhiteSpace($name)) {
            $includeSet[$name.Trim().ToLowerInvariant()] = $true
        }
    }
    $datasets = @($datasets | Where-Object { $includeSet.ContainsKey($_.Name.ToLowerInvariant()) })
}

$ratios = @("0.5", "0.2", "0.1", "0.05", "0.02")
$workerScript = Join-Path $WorkspaceRoot "batch\run_ratio_budget_algorithm_dataset.ps1"
$sharedConvertedRoot = Join-Path $WorkspaceRoot "skwcl\_shared_converted"

foreach ($dataset in $datasets) {
    foreach ($ratio in $ratios) {
        Write-Host "=== $Algorithm $($dataset.Name) $ratio ==="
        $workerArgs = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", $workerScript,
            "-WorkspaceRoot", $WorkspaceRoot,
            "-Algorithm", $Algorithm,
            "-DatasetName", $dataset.Name,
            "-DatasetRoot", $dataset.InputDir,
            "-AlgorithmRoot", (Join-Path $WorkspaceRoot ("skwcl\{0}\{1}" -f $ResultDirName, $dataset.Name)),
            "-CompressionRatio", ([string][double]$ratio),
            "-RatioTag", $ratio,
            "-PythonExe", $PythonExe,
            "-SharedConvertedRoot", (Join-Path $sharedConvertedRoot $dataset.Name),
            "-CompressionTimeoutSeconds", ([string]$CompressionTimeoutSeconds)
        )
        if ($Resume.IsPresent) {
            $workerArgs += "-Resume"
        }
        powershell @workerArgs
        if ($LASTEXITCODE -ne 0) {
            throw "$Algorithm failed for $($dataset.Name) $ratio"
        }
    }
}
