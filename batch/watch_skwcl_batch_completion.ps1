param(
    [string]$WorkspaceRoot = "D:\all_traj_method",
    [string]$RunLogPath,
    [int]$PollSeconds = 30,
    [string]$NotificationRoot = "",
    [string]$PostCompletionScript = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RunLogPath)) {
    throw "RunLogPath is required."
}

if (-not (Test-Path $RunLogPath)) {
    throw "Run log not found: $RunLogPath"
}

$logFile = Get-Item $RunLogPath
$runStartTime = $logFile.CreationTime
$nameForTime = [System.IO.Path]::GetFileName($RunLogPath)
if ($nameForTime -match '(\d{8})_(\d{6})') {
    $datePart = $matches[1]
    $timePart = $matches[2]
    $parsed = [datetime]::ParseExact(($datePart + $timePart), 'yyyyMMddHHmmss', [System.Globalization.CultureInfo]::InvariantCulture)
    $runStartTime = $parsed
}
$logDir = Split-Path -Parent $RunLogPath
$baseName = [System.IO.Path]::GetFileNameWithoutExtension($RunLogPath)
$donePath = Join-Path $logDir ($baseName + ".done.txt")
$statusPath = Join-Path $logDir ($baseName + ".status.txt")
if ([string]::IsNullOrWhiteSpace($NotificationRoot)) {
    $NotificationRoot = $logDir
}
if (-not (Test-Path $NotificationRoot)) {
    New-Item -ItemType Directory -Path $NotificationRoot -Force | Out-Null
}
$flagPath = Join-Path $NotificationRoot "ALL_RESULTS_COMPLETE.flag"
$popupTextPath = Join-Path $NotificationRoot "ALL_RESULTS_COMPLETE.txt"

$algorithms = @("DimWeaver", "CISED-S", "CISED-W")
$datasets = @("t-drive", "chengdu", "geolife", "wuxi")
$thresholds = @("1e-3", "5e-4", "1e-4", "5e-5", "1e-5")

function Get-ExpectedArtifacts {
    param(
        [string]$Algorithm,
        [string]$Dataset,
        [string]$Threshold
    )

    $root = Join-Path $WorkspaceRoot ("skwcl\{0}_result\{1}\{2}" -f $Algorithm, $Dataset, $Threshold)
    return @(
        Join-Path $root "metrics_summary.csv"
        Join-Path $root "run_info.txt"
    )
}

function Test-IsFreshFile {
    param(
        [string]$Path,
        [datetime]$Since
    )

    if (-not (Test-Path $Path)) {
        return $false
    }

    return (Get-Item $Path).LastWriteTime -ge $Since
}

while ($true) {
    $completed = 0
    $pending = New-Object System.Collections.Generic.List[string]

    foreach ($algorithm in $algorithms) {
        foreach ($dataset in $datasets) {
            foreach ($threshold in $thresholds) {
                $artifacts = Get-ExpectedArtifacts -Algorithm $algorithm -Dataset $dataset -Threshold $threshold
                $ok = $true
                foreach ($artifact in $artifacts) {
                    if (-not (Test-IsFreshFile -Path $artifact -Since $runStartTime)) {
                        $ok = $false
                        break
                    }
                }

                if ($ok) {
                    $completed += 1
                }
                else {
                    $pending.Add(("{0}/{1}/{2}" -f $algorithm, $dataset, $threshold)) | Out-Null
                }
            }
        }
    }

    @(
        ("run_log={0}" -f $RunLogPath)
        ("run_start_time={0}" -f $runStartTime.ToString("yyyy-MM-dd HH:mm:ss"))
        ("completed_groups={0}" -f $completed)
        ("total_groups=60")
        ("pending_count={0}" -f $pending.Count)
        ("pending_groups={0}" -f [string]::Join(";", $pending))
        ("last_checked={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss"))
    ) | Set-Content -Path $statusPath

    if ($completed -eq 60) {
        $completedAt = Get-Date
        $postActionStatus = "none"
        @(
            "status=done"
            ("completed_at={0}" -f $completedAt.ToString("yyyy-MM-dd HH:mm:ss"))
            ("run_log={0}" -f $RunLogPath)
            ("status_file={0}" -f $statusPath)
            "completed_groups=60"
        ) | Set-Content -Path $donePath

        if (-not [string]::IsNullOrWhiteSpace($PostCompletionScript) -and (Test-Path $PostCompletionScript)) {
            try {
                $postStdout = Join-Path $logDir "post_completion_action.out.log"
                $postStderr = Join-Path $logDir "post_completion_action.err.log"
                $postProcess = Start-Process -FilePath "powershell.exe" `
                    -ArgumentList @(
                        "-NoProfile"
                        "-ExecutionPolicy"
                        "Bypass"
                        "-File"
                        $PostCompletionScript
                        "-WorkspaceRoot"
                        $WorkspaceRoot
                    ) `
                    -RedirectStandardOutput $postStdout `
                    -RedirectStandardError $postStderr `
                    -WindowStyle Hidden `
                    -PassThru
                $postActionStatus = ("started pid={0} script={1}" -f $postProcess.Id, $PostCompletionScript)
            }
            catch {
                $postActionStatus = ("failed: {0}" -f $_.Exception.Message)
            }
        }

        @(
            "ALL SKWCL batch results are complete."
            ("completed_at={0}" -f $completedAt.ToString("yyyy-MM-dd HH:mm:ss"))
            ("run_log={0}" -f $RunLogPath)
            ("status_file={0}" -f $statusPath)
            ("done_file={0}" -f $donePath)
            ("post_completion_action={0}" -f $postActionStatus)
        ) | Set-Content -Path $popupTextPath

        @(
            "status=done"
            ("completed_at={0}" -f $completedAt.ToString("yyyy-MM-dd HH:mm:ss"))
            ("run_log={0}" -f $RunLogPath)
            ("post_completion_action={0}" -f $postActionStatus)
        ) | Set-Content -Path $flagPath

        [console]::beep(1000, 800)
        [console]::beep(1200, 800)
        try {
            $wshell = New-Object -ComObject WScript.Shell
            [void]$wshell.Popup("SKWCL batch algorithms finished. All 60 result groups are complete.`n`nSee:`n$popupTextPath", 30, "Batch Complete", 64)
        }
        catch {
        }
        break
    }

    Start-Sleep -Seconds $PollSeconds
}
