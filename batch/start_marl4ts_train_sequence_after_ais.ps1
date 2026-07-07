param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$PythonExe = "D:\all_traj_method\RLTS\.venv\Scripts\python.exe"
)

$ErrorActionPreference = "Stop"

function Wait-For-AisToFinish {
    param([string]$PythonPath)

    while ($true) {
        $running = Get-CimInstance Win32_Process | Where-Object {
            $_.ExecutablePath -eq $PythonPath -and
            $_.CommandLine -like "*run_marl4ts_train.py*" -and
            $_.CommandLine -like "*MARL4TS/TrajData/ais_2000_out*"
        }
        if (-not $running) {
            break
        }
        Start-Sleep -Seconds 20
    }
}

function Invoke-Marl4tsTrain {
    param(
        [string]$PythonPath,
        [string]$Workspace,
        [string]$TrajDir,
        [int]$TrainAmount,
        [int]$ValidAmount,
        [int]$ShowEvery,
        [string]$Tag
    )

    $scriptPath = Join-Path $Workspace "batch\run_marl4ts_train.py"
    & $PythonPath $scriptPath `
        --workspace-root $Workspace `
        --traj-dir $TrajDir `
        --train-amount $TrainAmount `
        --valid-amount $ValidAmount `
        --show-every $ShowEvery `
        --tag $Tag

    if ($LASTEXITCODE -ne 0) {
        throw "MARL4TS training failed for $Tag with exit code $LASTEXITCODE"
    }
}

$tasks = @(
    @{ traj_dir = "MARL4TS/TrajData/chengdu_2000_out"; train_amount = 1900; valid_amount = 100; show_every = 5; tag = "chengdu_2000_defaultlike_train1900_valid100_v5" }
    @{ traj_dir = "MARL4TS/TrajData/geolife_2000_out"; train_amount = 1900; valid_amount = 100; show_every = 5; tag = "geolife_2000_defaultlike_train1900_valid100_v5" }
    @{ traj_dir = "MARL4TS/TrajData/t-drive_1002_out"; train_amount = 899; valid_amount = 100; show_every = 5; tag = "tdrive_1002_defaultlike_train899_valid100_v5" }
    @{ traj_dir = "MARL4TS/TrajData/trajair_2000_out"; train_amount = 1900; valid_amount = 100; show_every = 5; tag = "trajair_2000_defaultlike_train1900_valid100_v5" }
    @{ traj_dir = "MARL4TS/TrajData/wuxi_2000_out"; train_amount = 1898; valid_amount = 100; show_every = 5; tag = "wuxi_2000_defaultlike_train1898_valid100_v5" }
)

Wait-For-AisToFinish -PythonPath $PythonExe

foreach ($task in $tasks) {
    Invoke-Marl4tsTrain `
        -PythonPath $PythonExe `
        -Workspace $WorkspaceRoot `
        -TrajDir $task.traj_dir `
        -TrainAmount $task.train_amount `
        -ValidAmount $task.valid_amount `
        -ShowEvery $task.show_every `
        -Tag $task.tag
}
