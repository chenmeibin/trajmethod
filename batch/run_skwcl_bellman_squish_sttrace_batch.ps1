param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "py",
    [switch]$Resume,
    [int]$CompressionTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

$datasets = @(
    @{ Name = "chengdu"; InputDir = (Join-Path $WorkspaceRoot "data_small\chengdu") },
    @{ Name = "Geolife"; InputDir = (Join-Path $WorkspaceRoot "data_small\Geolife") },
    @{ Name = "T-drive"; InputDir = (Join-Path $WorkspaceRoot "data_small\T-drive") },
    @{ Name = "wuxi"; InputDir = (Join-Path $WorkspaceRoot "data_small\wuxi") }
)

$ratios = @("0.5", "0.2", "0.1", "0.05", "0.02")
$algorithms = @(
    @{ Name = "Bellman"; ResultDir = "bellman_result" },
    @{ Name = "SQUISH"; ResultDir = "SQUISH_result" },
    @{ Name = "STTrace"; ResultDir = "STTrace_result" }
)

$scriptPath = Join-Path $WorkspaceRoot "batch\run_ratio_budget_algorithm_batch.ps1"
$logRoot = Join-Path $WorkspaceRoot "skwcl\_batch_logs"
Ensure-Directory $logRoot

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$processes = @()

foreach ($algorithm in $algorithms) {
    $outLog = Join-Path $logRoot ("{0}_data_small_batch_{1}.out.log" -f $algorithm.Name.ToLowerInvariant(), $timestamp)
    $errLog = Join-Path $logRoot ("{0}_data_small_batch_{1}.err.log" -f $algorithm.Name.ToLowerInvariant(), $timestamp)
    $argumentList = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $scriptPath,
        "-WorkspaceRoot", $WorkspaceRoot,
        "-Algorithm", $algorithm.Name,
        "-ResultDirName", $algorithm.ResultDir,
        "-PythonExe", $PythonExe,
        "-CompressionTimeoutSeconds", ([string]$CompressionTimeoutSeconds)
    )
    if ($Resume.IsPresent) {
        $argumentList += "-Resume"
    }
    $process = Start-Process -FilePath "powershell" `
        -ArgumentList $argumentList `
        -RedirectStandardOutput $outLog `
        -RedirectStandardError $errLog `
        -PassThru `
        -WindowStyle Hidden

    $processes += [pscustomobject]@{
        Algorithm = $algorithm.Name
        Process = $process
        OutLog = $outLog
        ErrLog = $errLog
    }
}

foreach ($entry in $processes) {
    Wait-Process -Id $entry.Process.Id
}

$failed = $processes | Where-Object { $_.Process.ExitCode -ne 0 }
if ($failed) {
    $failed | Select-Object Algorithm, @{ Name = "ExitCode"; Expression = { $_.Process.ExitCode } }, OutLog, ErrLog | Format-Table -AutoSize
    throw "One or more batch processes failed"
}

$processes | Select-Object Algorithm, @{ Name = "ExitCode"; Expression = { $_.Process.ExitCode } }, OutLog, ErrLog | Format-Table -AutoSize
