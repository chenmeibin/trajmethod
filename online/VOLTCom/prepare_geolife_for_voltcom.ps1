param(
    [string]$RawDir = "D:\all_traj_method\data\Geolife",
    [string]$OutputDir = "D:\all_traj_method\online\VOLTCom\data_set\Geolife"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $RawDir)) {
    throw "Geolife source directory not found: $RawDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$written = 0
$skipped = 0
$files = Get-ChildItem $RawDir -Recurse -File -Filter *.plt | Sort-Object FullName
$uid = 1

foreach ($file in $files) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $outputPath = Join-Path $OutputDir ($baseName + "_for_VOLTCom.csv")

    $rows = New-Object System.Collections.Generic.List[string]
    $rows.Add("latitude,longitude,date,time,U_id") | Out-Null

    $lineIndex = 0
    foreach ($line in Get-Content $file.FullName) {
        $lineIndex += 1
        if ($lineIndex -le 6 -or [string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line.Split(",")
        if ($parts.Length -lt 7) {
            continue
        }

        try {
            $latitude = [double]$parts[0]
            $longitude = [double]$parts[1]
            $date = $parts[5].Trim() -replace '-', '/'
            $time = $parts[6].Trim()
            $rows.Add(("{0},{1},{2},{3},{4}" -f $latitude, $longitude, $date, $time, $uid)) | Out-Null
        }
        catch {
            continue
        }
    }

    if ($rows.Count -le 1) {
        $skipped += 1
        $uid += 1
        continue
    }

    Set-Content -Path $outputPath -Value $rows
    $written += 1
    $uid += 1
}

[pscustomobject]@{
    written_files = $written
    skipped_files = $skipped
    output_dir = $OutputDir
} | Format-List
