param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("chengdu", "geolife", "wuxi")]
    [string]$Dataset,

    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$DayList = "",
    [int]$MaxTrajectoriesPerDay = 40,
    [int]$MinPoints = 30
)

$ErrorActionPreference = "Stop"

function Ensure-Dir {
    param([string]$Path)
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Get-NonEmptyLines {
    param([string]$Path)
    return Get-Content -LiteralPath $Path | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
}

function Convert-Chengdu {
    param([string]$SourceDir, [string]$TargetDir)
    Ensure-Dir $TargetDir
    $files = Get-ChildItem -LiteralPath $SourceDir -File -Filter *.txt | Sort-Object Name
    foreach ($file in $files) {
        $lines = Get-NonEmptyLines $file.FullName
        if ($lines.Count -lt 2) {
            continue
        }
        $trajId = $lines[0].Trim()
        $outLines = New-Object System.Collections.Generic.List[string]
        for ($i = 1; $i -lt $lines.Count; $i++) {
            $parts = $lines[$i].Split(',')
            if ($parts.Count -lt 3) {
                continue
            }
            $timestamp = $parts[0].Trim()
            $lon = $parts[1].Trim()
            $lat = $parts[2].Trim()
            $outLines.Add(("{0},{1},{2},{3}" -f $trajId, $timestamp, $lon, $lat)) | Out-Null
        }
        if ($outLines.Count -gt 1) {
            $outPath = Join-Path $TargetDir $file.Name
            $outLines | Set-Content -Encoding UTF8 $outPath
        }
    }
}

function Convert-Geolife {
    param([string]$SourceDir, [string]$TargetDir)
    Ensure-Dir $TargetDir
    $files = Get-ChildItem -LiteralPath $SourceDir -File -Filter *.plt | Sort-Object Name
    foreach ($file in $files) {
        $lines = Get-Content -LiteralPath $file.FullName
        if ($lines.Count -le 6) {
            continue
        }
        $trajId = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
        $outLines = New-Object System.Collections.Generic.List[string]
        for ($i = 6; $i -lt $lines.Count; $i++) {
            if ([string]::IsNullOrWhiteSpace($lines[$i])) {
                continue
            }
            $parts = $lines[$i].Split(',')
            if ($parts.Count -lt 7) {
                continue
            }
            $lat = $parts[0].Trim()
            $lon = $parts[1].Trim()
            $date = $parts[5].Trim()
            $time = $parts[6].Trim()
            $timestamp = "$date $time"
            $outLines.Add(("{0},{1},{2},{3}" -f $trajId, $timestamp, $lon, $lat)) | Out-Null
        }
        if ($outLines.Count -gt 1) {
            $outPath = Join-Path $TargetDir ($trajId + ".txt")
            $outLines | Set-Content -Encoding UTF8 $outPath
        }
    }
}

function Select-StratifiedIds {
    param(
        [hashtable]$Counts,
        [int]$MaxTrajectories
    )
    $eligible = $Counts.GetEnumerator() | Where-Object { $_.Value -ge $MinPoints } | Sort-Object Value
    if ($eligible.Count -le $MaxTrajectories) {
        return @($eligible | ForEach-Object { $_.Key })
    }
    $selected = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $MaxTrajectories; $i++) {
        $index = [math]::Floor(($i * ($eligible.Count - 1)) / [math]::Max(1, ($MaxTrajectories - 1)))
        $selected.Add($eligible[$index].Key) | Out-Null
    }
    return @($selected | Select-Object -Unique)
}

function Convert-Wuxi {
    param([string]$SourceDir, [string]$TargetDir)
    Ensure-Dir $TargetDir
    $days = @()
    if ([string]::IsNullOrWhiteSpace($DayList)) {
        $days = Get-ChildItem -LiteralPath $SourceDir -Directory | Sort-Object Name | Select-Object -ExpandProperty Name
    } else {
        $days = $DayList.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
    }

    foreach ($day in $days) {
        $csvPath = Join-Path (Join-Path $SourceDir $day) ($day + ".csv")
        if (-not (Test-Path $csvPath)) {
            continue
        }

        $counts = @{}
        $reader = [System.IO.File]::OpenText($csvPath)
        try {
            $null = $reader.ReadLine()
            while (($line = $reader.ReadLine()) -ne $null) {
                if ([string]::IsNullOrWhiteSpace($line)) {
                    continue
                }
                $parts = $line.Split(',')
                if ($parts.Count -lt 4) {
                    continue
                }
                $id = $parts[0].Trim()
                if ([string]::IsNullOrWhiteSpace($id)) {
                    continue
                }
                if (-not $counts.ContainsKey($id)) {
                    $counts[$id] = 0
                }
                $counts[$id] += 1
            }
        }
        finally {
            $reader.Close()
        }

        $selectedIds = Select-StratifiedIds -Counts $counts -MaxTrajectories $MaxTrajectoriesPerDay
        if ($selectedIds.Count -eq 0) {
            continue
        }
        $selectedSet = @{}
        foreach ($id in $selectedIds) { $selectedSet[$id] = $true }

        $buffers = @{}
        $reader = [System.IO.File]::OpenText($csvPath)
        try {
            $null = $reader.ReadLine()
            while (($line = $reader.ReadLine()) -ne $null) {
                if ([string]::IsNullOrWhiteSpace($line)) {
                    continue
                }
                $parts = $line.Split(',')
                if ($parts.Count -lt 4) {
                    continue
                }
                $id = $parts[0].Trim()
                if (-not $selectedSet.ContainsKey($id)) {
                    continue
                }
                $lon = $parts[1].Trim()
                $lat = $parts[2].Trim()
                $timestamp = $parts[3].Trim()
                if ([string]::IsNullOrWhiteSpace($lon) -or [string]::IsNullOrWhiteSpace($lat) -or [string]::IsNullOrWhiteSpace($timestamp)) {
                    continue
                }
                if (-not $buffers.ContainsKey($id)) {
                    $buffers[$id] = New-Object System.Collections.Generic.List[string]
                }
                $buffers[$id].Add(("{0},{1},{2},{3}" -f $id, $timestamp, $lon, $lat)) | Out-Null
            }
        }
        finally {
            $reader.Close()
        }

        foreach ($entry in $buffers.GetEnumerator()) {
            if ($entry.Value.Count -lt $MinPoints) {
                continue
            }
            $outPath = Join-Path $TargetDir ($day + "_" + $entry.Key + ".txt")
            $entry.Value | Set-Content -Encoding UTF8 $outPath
        }
    }
}

switch ($Dataset) {
    "chengdu" { Convert-Chengdu -SourceDir $InputPath -TargetDir $OutputDir }
    "geolife" { Convert-Geolife -SourceDir $InputPath -TargetDir $OutputDir }
    "wuxi" { Convert-Wuxi -SourceDir $InputPath -TargetDir $OutputDir }
}

Write-Output "Prepared $Dataset inputs at: $OutputDir"
