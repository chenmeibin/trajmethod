param(
    [string]$RawDir = "D:\all_traj_method\data\T-drive",
    [string]$OutputDir = "D:\all_traj_method\online\VOLTCom\data_set\T-drive"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $RawDir)) {
    throw "T-drive source directory not found: $RawDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$written = 0
$skipped = 0
$files = Get-ChildItem $RawDir -File -Filter *.txt | Sort-Object Name

foreach ($file in $files) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $outputPath = Join-Path $OutputDir ($baseName + "_for_VOLTCom.csv")

    $rows = New-Object System.Collections.Generic.List[string]
    $rows.Add("longitude,latitude,timestamp,name_id") | Out-Null

    foreach ($line in Get-Content $file.FullName) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line.Split(",")
        if ($parts.Length -lt 4) {
            continue
        }

        try {
            $id = [int64]($parts[0].Trim())
            $timestamp = $parts[1].Trim()
            $longitude = [double]($parts[2].Trim())
            $latitude = [double]($parts[3].Trim())
            $rows.Add(("{0},{1},{2},{3}" -f $longitude, $latitude, $timestamp, $id)) | Out-Null
        }
        catch {
            continue
        }
    }

    if ($rows.Count -le 1) {
        $skipped += 1
        continue
    }

    Set-Content -Path $outputPath -Value $rows
    $written += 1
}

[pscustomobject]@{
    written_files = $written
    skipped_files = $skipped
    output_dir = $OutputDir
} | Format-List
