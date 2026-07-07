param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [int]$PollSeconds = 30,
    [string]$PostCompletionScript = ""
)

$ErrorActionPreference = "Stop"

$logDir = Join-Path $WorkspaceRoot "skwcl\_batch_logs"
if (-not (Test-Path $logDir)) {
    throw "Log directory not found: $logDir"
}

$runLog = Get-ChildItem -Path $logDir -Filter "run_skwcl_batch_algorithms_*.out.log" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $runLog) {
    throw "No batch run log found in $logDir"
}

$watchScript = Join-Path $WorkspaceRoot "batch\watch_skwcl_batch_completion.ps1"
if (-not (Test-Path $watchScript)) {
    throw "Watcher script not found: $watchScript"
}

$existing = Get-CimInstance Win32_Process |
    Where-Object {
        $_.Name -eq "powershell.exe" -and
        $_.CommandLine -match [regex]::Escape($watchScript) -and
        $_.CommandLine -match [regex]::Escape($runLog.FullName)
    }

if ($existing) {
    Write-Host ("Watcher already running for {0}" -f $runLog.FullName)
    $existing | Select-Object ProcessId, CreationDate
    exit 0
}

$watchOut = Join-Path $logDir "watch_skwcl_batch_completion.out.log"
$watchErr = Join-Path $logDir "watch_skwcl_batch_completion.err.log"
$baseName = [System.IO.Path]::GetFileNameWithoutExtension($runLog.FullName)
$statusFile = Join-Path $logDir ($baseName + ".status.txt")
$doneFile = Join-Path $logDir ($baseName + ".done.txt")

$args = @(
    "-NoProfile"
    "-ExecutionPolicy"
    "Bypass"
    "-File"
    $watchScript
    "-WorkspaceRoot"
    $WorkspaceRoot
    "-RunLogPath"
    $runLog.FullName
    "-PollSeconds"
    $PollSeconds
    "-NotificationRoot"
    $logDir
)

if (-not [string]::IsNullOrWhiteSpace($PostCompletionScript)) {
    $args += @(
        "-PostCompletionScript"
        $PostCompletionScript
    )
}

$proc = Start-Process -FilePath "powershell.exe" `
    -ArgumentList $args `
    -RedirectStandardOutput $watchOut `
    -RedirectStandardError $watchErr `
    -WindowStyle Hidden `
    -PassThru

Write-Host ("Started watcher pid={0}" -f $proc.Id)
Write-Host ("run_log={0}" -f $runLog.FullName)
Write-Host ("status_file={0}" -f $statusFile)
Write-Host ("done_file={0}" -f $doneFile)
Write-Host ("flag_file={0}" -f (Join-Path $logDir "ALL_RESULTS_COMPLETE.flag"))
