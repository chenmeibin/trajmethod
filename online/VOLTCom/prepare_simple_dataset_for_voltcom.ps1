param(
    [Parameter(Mandatory = $true)]
    [string]$RawDir,
    [Parameter(Mandatory = $true)]
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $RawDir)) {
    throw "Source directory not found: $RawDir"
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

    $lineIndex = 0
    foreach ($line in Get-Content $file.FullName) {
        $lineIndex += 1
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        if ($lineIndex -eq 1) {
            continue
        }

        $parts = $line.Split(",")
        if ($parts.Length -lt 3) {
            continue
        }

        try {
            $timestamp = $parts[0].Trim()
            $longitude = [double]($parts[1].Trim())
            $latitude = [double]($parts[2].Trim())
            [void][datetime]::Parse($timestamp, [System.Globalization.CultureInfo]::InvariantCulture)
            $rows.Add(("{0},{1},{2},{3}" -f $longitude, $latitude, $timestamp, $baseName)) | Out-Null
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
