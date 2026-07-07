param(
    [string]$WorkspaceRoot = "D:\all_traj_method"
)

$ErrorActionPreference = "Stop"

$runScript = Join-Path $WorkspaceRoot "batch\run_dpts_sp_intersect_batch.ps1"
$logRoot = Join-Path $WorkspaceRoot "skwcl\_batch_logs"

if (-not (Test-Path $runScript)) {
    throw "Run script not found: $runScript"
}

if (-not (Test-Path $logRoot)) {
    New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
}

$existing = Get-CimInstance Win32_Process |
    Where-Object {
        $_.Name -eq "powershell.exe" -and
        $_.CommandLine -match [regex]::Escape($runScript)
    }

if ($existing) {
    Write-Host "DPTS SP/intersect batch already running."
    $existing | Select-Object ProcessId, CreationDate
    exit 0
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stdoutLog = Join-Path $logRoot ("run_dpts_sp_intersect_batch_{0}.out.log" -f $timestamp)
$stderrLog = Join-Path $logRoot ("run_dpts_sp_intersect_batch_{0}.err.log" -f $timestamp)

$proc = Start-Process -FilePath "powershell.exe" `
    -ArgumentList @(
        "-NoProfile"
        "-ExecutionPolicy"
        "Bypass"
        "-File"
        $runScript
        "-WorkspaceRoot"
        $WorkspaceRoot
    ) `
    -RedirectStandardOutput $stdoutLog `
    -RedirectStandardError $stderrLog `
    -WindowStyle Hidden `
    -PassThru

Write-Host ("Started DPTS SP/intersect batch pid={0}" -f $proc.Id)
Write-Host ("stdout_log={0}" -f $stdoutLog)
Write-Host ("stderr_log={0}" -f $stderrLog)
