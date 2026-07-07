param(
    [string]$ExePath = "",
    [string]$RecordsRoot = "",
    [string]$Thresholds = "1e-5,5e-5,1e-4,3e-4,5e-4,1e-3"
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultBuildDir = Join-Path $scriptRoot "build"

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $defaultBuildDir "pilotc_reference.exe"
}
if ([string]::IsNullOrWhiteSpace($RecordsRoot)) {
    $RecordsRoot = Join-Path $defaultBuildDir "records_source_style"
}

if (-not (Test-Path $ExePath)) {
    throw "Executable not found: $ExePath"
}

New-Item -ItemType Directory -Force -Path $RecordsRoot | Out-Null

$jobs = @(
    @{
        DatasetName = "t_drive"
        InputDir = "D:\all_traj_method\data\T-drive"
        InputFormat = "tdrive"
        DataSource = "geolife"
        A = 0.75
        B = 10
        C = 0.6
    },
    @{
        DatasetName = "chengdu"
        InputDir = "D:\all_traj_method\PILOT-C\cpp_port\prepared_inputs\chengdu_tdrive"
        InputFormat = "tdrive"
        DataSource = "geolife"
        A = 0.75
        B = 25
        C = 0.8
    },
    @{
        DatasetName = "geolife"
        InputDir = "D:\all_traj_method\PILOT-C\cpp_port\prepared_inputs\geolife_tdrive"
        InputFormat = "tdrive"
        DataSource = "geolife"
        A = 0.5
        B = 25
        C = 0.8
    },
    @{
        DatasetName = "wuxi"
        InputDir = "D:\all_traj_method\PILOT-C\cpp_port\prepared_inputs\wuxi_tdrive_full"
        InputFormat = "tdrive"
        DataSource = "geolife"
        A = 1
        B = 25
        C = 1.1
    }
)

foreach ($job in $jobs) {
    $datasetRoot = Join-Path $RecordsRoot $job.DatasetName
    New-Item -ItemType Directory -Force -Path $datasetRoot | Out-Null

    Write-Output ("Starting dataset: " + $job.DatasetName)
    & powershell -ExecutionPolicy Bypass -File (Join-Path $scriptRoot "run_pilotc_dataset_full.ps1") `
        -ExePath $ExePath `
        -DatasetName $job.DatasetName `
        -InputDir $job.InputDir `
        -InputFormat $job.InputFormat `
        -DataSource $job.DataSource `
        -RecordsRoot $datasetRoot `
        -Thresholds $Thresholds `
        -A $job.A `
        -B $job.B `
        -C $job.C `
        -Accuracy 0.5
    if ($LASTEXITCODE -ne 0) {
        throw "Dataset run failed: $($job.DatasetName)"
    }
    Write-Output ("Completed dataset: " + $job.DatasetName)
}

Write-Output ("All sequential source-style runs saved under: " + $RecordsRoot)
