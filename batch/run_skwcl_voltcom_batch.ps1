param(
    [string]$WorkspaceRoot = "D:\all_traj_method"
)

$ErrorActionPreference = "Stop"

$datasets = @(
    @{ Name = "ais"; RawDir = "D:\all_traj_method\Data\ais" },
    @{ Name = "trajair"; RawDir = "D:\all_traj_method\Data\trajair" }
)

$thresholds = @("1e-3", "5e-4", "1e-4", "5e-5", "1e-5")
$voltcomRoot = Join-Path $WorkspaceRoot "online\VOLTCom"
$prepareScript = Join-Path $voltcomRoot "prepare_simple_dataset_for_voltcom.ps1"
$runScript = Join-Path $voltcomRoot "run_voltcom_dataset_resumable.ps1"
$summarizeScript = Join-Path $WorkspaceRoot "batch\summarize_voltcom_metrics.ps1"

foreach ($dataset in $datasets) {
    $convertDir = Join-Path $WorkspaceRoot ("skwcl\voltcom_result\{0}\convert" -f $dataset.Name)
    powershell -NoProfile -ExecutionPolicy Bypass -File $prepareScript `
        -RawDir $dataset.RawDir `
        -OutputDir $convertDir
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to prepare VOLTCom inputs for $($dataset.Name)"
    }

    foreach ($threshold in $thresholds) {
        $epsilonValue = [double]::Parse($threshold, [System.Globalization.CultureInfo]::InvariantCulture)
        $resultDir = Join-Path $WorkspaceRoot ("skwcl\voltcom_result\{0}\{1}" -f $dataset.Name, $threshold)
        powershell -NoProfile -ExecutionPolicy Bypass -File $runScript `
            -InputDir $convertDir `
            -ResultsDir $resultDir `
            -Epsilon $epsilonValue `
            -EpsilonLabel $threshold `
            -DatasetLabel $dataset.Name
        if ($LASTEXITCODE -ne 0) {
            throw "VOLTCom failed for $($dataset.Name) $threshold"
        }

        $summaryPath = Join-Path $resultDir "summary.csv"
        $datasetSummaryPath = Join-Path $WorkspaceRoot ("skwcl\voltcom_result\{0}\summary_{1}.csv" -f $dataset.Name, $threshold)
        if (Test-Path $summaryPath) {
            Copy-Item -Path $summaryPath -Destination $datasetSummaryPath -Force
        }
    }
}

powershell -NoProfile -ExecutionPolicy Bypass -File $summarizeScript -WorkspaceRoot $WorkspaceRoot
if ($LASTEXITCODE -ne 0) {
    throw "Failed to summarize VOLTCom metrics"
}
