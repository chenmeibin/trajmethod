param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "py",
    [int]$MaxFiles = 0
)

$ErrorActionPreference = "Stop"

$logRoot = Join-Path $WorkspaceRoot "skwcl\_batch_logs"
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

$trajicScript = Join-Path $WorkspaceRoot "batch\run_skwcl_trajic_batch.ps1"
$pilotcScript = Join-Path $WorkspaceRoot "batch\run_skwcl_pilotc_batch.ps1"

$trajicOutLog = Join-Path $logRoot "trajic_data_small_parallel.out.log"
$trajicErrLog = Join-Path $logRoot "trajic_data_small_parallel.err.log"
$pilotcOutLog = Join-Path $logRoot "pilotc_data_small_parallel.out.log"
$pilotcErrLog = Join-Path $logRoot "pilotc_data_small_parallel.err.log"

$trajicArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $trajicScript,
    "-WorkspaceRoot", $WorkspaceRoot,
    "-MaxFiles", $MaxFiles
)

$pilotcArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $pilotcScript,
    "-WorkspaceRoot", $WorkspaceRoot,
    "-PythonExe", $PythonExe,
    "-MaxFiles", $MaxFiles
)

$trajicProcess = Start-Process -FilePath "powershell" -ArgumentList $trajicArgs -RedirectStandardOutput $trajicOutLog -RedirectStandardError $trajicErrLog -WindowStyle Hidden -PassThru
$pilotcProcess = Start-Process -FilePath "powershell" -ArgumentList $pilotcArgs -RedirectStandardOutput $pilotcOutLog -RedirectStandardError $pilotcErrLog -WindowStyle Hidden -PassThru

Write-Output "Trajic PID=$($trajicProcess.Id) stdout=$trajicOutLog stderr=$trajicErrLog"
Write-Output "PILOT-C PID=$($pilotcProcess.Id) stdout=$pilotcOutLog stderr=$pilotcErrLog"
