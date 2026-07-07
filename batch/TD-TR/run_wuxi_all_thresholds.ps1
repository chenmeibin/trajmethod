param(
    [string]$InputDir = "D:\all_traj_method\data\TD-TR_wuxi_converted",
    [string]$DataRoot = "D:\all_traj_method\data",
    [int]$ChunkSize = 4000
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$resumeScript = Join-Path $scriptDir "run_wuxi_tdtr_resumable.ps1"
$thresholds = @(
    @{ Label = "5e-5"; Value = 5e-5 },
    @{ Label = "1e-4"; Value = 1e-4 },
    @{ Label = "3e-4"; Value = 3e-4 },
    @{ Label = "5e-4"; Value = 5e-4 }
)

$totalInputs = (Get-ChildItem $InputDir -File -Filter *.plt | Measure-Object).Count
$statusRows = New-Object System.Collections.Generic.List[object]

foreach ($threshold in $thresholds) {
    $resultsDir = Join-Path $DataRoot ("TD-TR_wuxi_{0}_results" -f $threshold.Label)

    while ($true) {
        $existing = if (Test-Path $resultsDir) {
            (Get-ChildItem $resultsDir -File -Filter ("*_TD-TR_wuxi_{0}.csv" -f $threshold.Label) | Measure-Object).Count
        }
        else {
            0
        }

        if ($existing -ge $totalInputs) {
            break
        }

        & powershell -ExecutionPolicy Bypass -File $resumeScript `
            -InputDir $InputDir `
            -ResultsDir $resultsDir `
            -Epsilon $threshold.Value `
            -EpsilonLabel $threshold.Label `
            -MaxNewResults $ChunkSize

        if ($LASTEXITCODE -ne 0) {
            throw "Resumable run failed for threshold $($threshold.Label)"
        }
    }

    $summaryFile = Join-Path $resultsDir "summary.csv"
    $summary = Import-Csv $summaryFile
    $inputTotals = $summary | Measure-Object -Property input_points -Sum
    $outputTotals = $summary | Measure-Object -Property output_points -Sum
    $runtimeTotals = $summary | Measure-Object -Property runtime_seconds -Sum

    $statusRows.Add([pscustomobject]@{
        epsilon = $threshold.Label
        processed_files = $summary.Count
        total_input_points = [int64]$inputTotals.Sum
        total_output_points = [int64]$outputTotals.Sum
        overall_compression_ratio = if ($inputTotals.Sum -gt 0) {
            [Math]::Round($outputTotals.Sum / $inputTotals.Sum, 6)
        }
        else {
            0
        }
        total_runtime_seconds = [Math]::Round($runtimeTotals.Sum, 6)
        summary_file = $summaryFile
    }) | Out-Null
}

$statusRows | Format-Table -AutoSize
