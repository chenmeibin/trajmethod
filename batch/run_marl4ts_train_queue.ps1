param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "D:\all_traj_method\RLTS\.venv\Scripts\python.exe",
    [string]$TasksJsonPath,
    [string]$LogDir = "D:\all_traj_method\tmp\marl4ts_train_logs"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $TasksJsonPath)) {
    throw "Tasks json not found: $TasksJsonPath"
}

if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

$trainScript = Join-Path $WorkspaceRoot "batch\run_marl4ts_train.py"
$tasks = Get-Content $TasksJsonPath -Raw | ConvertFrom-Json

foreach ($task in $tasks) {
    $tag = [string]$task.tag
    $logPath = Join-Path $LogDir ($tag + ".queue.log")
    $header = @(
        "===== TASK START ====="
        ("timestamp={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss"))
        ("traj_dir={0}" -f $task.traj_dir)
        ("train_amount={0}" -f $task.train_amount)
        ("valid_amount={0}" -f $task.valid_amount)
        ("show_every={0}" -f $task.show_every)
        ("tag={0}" -f $tag)
    )
    $header | Add-Content -Path $logPath

    $args = @(
        $trainScript,
        "--workspace-root", $WorkspaceRoot,
        "--traj-dir", [string]$task.traj_dir,
        "--train-amount", [string]$task.train_amount,
        "--valid-amount", [string]$task.valid_amount,
        "--show-every", [string]$task.show_every,
        "--tag", $tag
    )

    & $PythonExe @args *>> $logPath
    if ($LASTEXITCODE -ne 0) {
        throw "Task failed for tag=$tag with exit code $LASTEXITCODE"
    }

    @(
        ("timestamp={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss"))
        "===== TASK END ====="
        ""
    ) | Add-Content -Path $logPath
}
