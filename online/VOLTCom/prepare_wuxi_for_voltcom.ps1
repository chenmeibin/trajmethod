param(
    [string]$SourceDir = "D:\all_traj_method\data\TD-TR_wuxi_converted",
    [string]$OutputDir = "D:\all_traj_method\online\VOLTCom\data_set\wuxi"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $SourceDir)) {
    throw "Wuxi converted trajectory directory not found: $SourceDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$written = 0
$skipped = 0
$files = Get-ChildItem $SourceDir -File -Filter *.plt | Sort-Object Name

foreach ($file in $files) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $outputPath = Join-Path $OutputDir ($baseName + "_for_VOLTCom.csv")
    if (Test-Path $outputPath) {
        continue
    }

    $rows = New-Object System.Collections.Generic.List[string]
    $rows.Add("longitude,latitude,timestamp,name_id") | Out-Null

    foreach ($line in Get-Content $file.FullName) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line.Trim() -split '\s+'
        if ($parts.Length -lt 3) {
            continue
        }

        try {
            $lat = [double]$parts[0]
            $lon = [double]$parts[1]
            $epoch = [int64]$parts[2]
            $dt = [DateTimeOffset]::FromUnixTimeSeconds($epoch).ToString("yyyy-MM-dd HH:mm:ss")
            $rows.Add(("{0},{1},{2},1" -f $lon, $lat, $dt)) | Out-Null
        }
        catch {
            continue
        }
    }

    if ($rows.Count -le 1) {
        $skipped += 1
        continue
    }

    [System.IO.File]::WriteAllLines($outputPath, $rows.ToArray())
    $written += 1
}

[pscustomobject]@{
    written_files = $written
    skipped_files = $skipped
    output_dir = $OutputDir
} | Format-List
