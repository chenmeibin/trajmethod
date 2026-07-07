param(
    [string]$RawDir = "D:\all_traj_method\data\wuxi",
    [string]$OutputDir = "D:\all_traj_method\data\wuxi_extracted"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $RawDir)) {
    throw "Raw Wuxi dataset directory not found: $RawDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$csvFiles = Get-ChildItem $RawDir -Recurse -File -Filter *.csv | Sort-Object FullName
$doneFiles = Get-ChildItem $OutputDir -File -Filter *.done -ErrorAction SilentlyContinue
$nextIndex = 1

if (Test-Path $OutputDir) {
    $existingTxt = Get-ChildItem $OutputDir -File -Filter *.txt -ErrorAction SilentlyContinue
    if ($existingTxt) {
        $nextIndex = (($existingTxt | ForEach-Object {
            [int][System.IO.Path]::GetFileNameWithoutExtension($_.Name)
        } | Measure-Object -Maximum).Maximum + 1)
    }
}

$writtenFiles = 0
$skippedDays = 0

foreach ($csvFile in $csvFiles) {
    $dayTag = [System.IO.Path]::GetFileNameWithoutExtension($csvFile.Name)
    $doneMarker = Join-Path $OutputDir ($dayTag + ".done")
    if (Test-Path $doneMarker) {
        $skippedDays += 1
        continue
    }

    Get-ChildItem $OutputDir -File -Filter ($dayTag + "_partial_*") -ErrorAction SilentlyContinue | Remove-Item -Force

    $writers = @{}
    $reader = [System.IO.StreamReader]::new($csvFile.FullName)

    try {
        [void]$reader.ReadLine()
        while (($line = $reader.ReadLine()) -ne $null) {
            if ([string]::IsNullOrWhiteSpace($line)) {
                continue
            }

            $parts = $line.Split(",")
            if ($parts.Length -lt 4) {
                continue
            }

            try {
                $id = $parts[0].Trim()
                if ([string]::IsNullOrWhiteSpace($id)) {
                    continue
                }

                $lon = [double]$parts[1]
                $lat = [double]$parts[2]
                $timestamp = [datetime]::ParseExact($parts[3].Trim(), "yyyy-MM-dd HH:mm:ss", [System.Globalization.CultureInfo]::InvariantCulture)
                $timestampText = $timestamp.ToString("yyyy-MM-dd HH:mm:ss.0", [System.Globalization.CultureInfo]::InvariantCulture)
                $key = $dayTag + "|" + $id

                if (-not $writers.ContainsKey($key)) {
                    $fileIndex = $nextIndex
                    $nextIndex += 1
                    $fileName = "{0}.txt" -f $fileIndex
                    $outputPath = Join-Path $OutputDir $fileName
                    $writer = [System.IO.StreamWriter]::new($outputPath, $false, [System.Text.Encoding]::UTF8)
                    $writer.WriteLine($id)
                    $writers[$key] = $writer
                    $writtenFiles += 1
                }

                $writers[$key].WriteLine(("{0},{1},{2}" -f $timestampText, $lon, $lat))
            }
            catch {
                continue
            }
        }
    }
    finally {
        $reader.Close()
        foreach ($writer in $writers.Values) {
            $writer.Close()
        }
    }

    New-Item -ItemType File -Force -Path $doneMarker | Out-Null
}

[pscustomobject]@{
    written_files = $writtenFiles
    skipped_completed_days = $skippedDays
    output_dir = $OutputDir
} | Format-List
