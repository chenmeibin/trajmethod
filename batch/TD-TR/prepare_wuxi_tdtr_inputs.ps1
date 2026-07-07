param(
    [string]$RawDir = "D:\all_traj_method\data\wuxi",
    [string]$OutputDir = "D:\all_traj_method\data\TD-TR_wuxi_converted"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $RawDir)) {
    throw "Raw Wuxi dataset directory not found: $RawDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$csvFiles = Get-ChildItem $RawDir -Recurse -File -Filter *.csv | Sort-Object FullName
$writtenFiles = 0
$skippedDays = 0

foreach ($csvFile in $csvFiles) {
    $dayTag = [System.IO.Path]::GetFileNameWithoutExtension($csvFile.Name)
    $doneMarker = Join-Path $OutputDir ($dayTag + ".done")
    if (Test-Path $doneMarker) {
        $skippedDays += 1
        continue
    }

    # Clean up any partial outputs from a previous interrupted run for this day.
    Get-ChildItem $OutputDir -File -Filter ($dayTag + "_*.plt") -ErrorAction SilentlyContinue | Remove-Item -Force

    $writers = @{}
    $reader = [System.IO.StreamReader]::new($csvFile.FullName)

    try {
        $header = $reader.ReadLine()
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
                $dt = [datetime]::ParseExact($parts[3].Trim(), "yyyy-MM-dd HH:mm:ss", [System.Globalization.CultureInfo]::InvariantCulture)
                $epoch = [int64][DateTimeOffset]::new($dt).ToUnixTimeSeconds()

                $safeId = ($id -replace '[^0-9A-Za-z_-]', '_')
                $key = $dayTag + "_" + $safeId

                if (-not $writers.ContainsKey($key)) {
                    $outputPath = Join-Path $OutputDir ($key + ".plt")
                    $writers[$key] = [System.IO.StreamWriter]::new($outputPath, $false, [System.Text.Encoding]::UTF8)
                    $writtenFiles += 1
                }

                $writers[$key].WriteLine(("{0} {1} {2}" -f $lat, $lon, $epoch))
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
