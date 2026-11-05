$ErrorActionPreference = "Stop"

# Move to repo root
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$scanDirs = @(
    (Join-Path $root "logs"),
    (Join-Path $root "bin\Release")
)

$validDirs = @($scanDirs | Where-Object { Test-Path $_ })
$rootLatencyProfile = Join-Path $root "latency_profile.csv"
$hasRootItem = Test-Path $rootLatencyProfile

if ($validDirs.Count -eq 0 -and -not $hasRootItem) {
    foreach ($dir in $scanDirs) {
        if (-not (Test-Path $dir)) {
            Write-Warning "Directory not found: $dir"
        }
    }
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$zipName = "stroboverlay_logs_$timestamp.zip"
$zipPath = Join-Path $root $zipName

$itemsToZip = @()

# Include root latency_profile.csv if it exists
if ($hasRootItem) {
    $itemsToZip += (Get-Item $rootLatencyProfile).FullName
}

foreach ($dir in $validDirs) {
    $files = Get-ChildItem -Path $dir -File -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.csv','.log','.dmp' }
    if ($files) {
        $itemsToZip += ($files | Select-Object -ExpandProperty FullName)
    }
}

$itemsToZip = @($itemsToZip | Select-Object -Unique)

if ($itemsToZip.Count -eq 0) {
    Write-Warning "No logs or dumps found to archive."
    exit 1
}

# Helper function to safely copy files that may be open / locked
function Copy-FileSafely {
    param(
        [string]$SourcePath,
        [string]$DestinationPath
    )
    try {
        Copy-Item -Path $SourcePath -Destination $DestinationPath -Force -ErrorAction Stop
        return $true
    } catch {
        try {
            $destDir = [System.IO.Path]::GetDirectoryName($DestinationPath)
            if (-not (Test-Path $destDir)) {
                New-Item -ItemType Directory -Path $destDir -Force | Out-Null
            }
            $sourceStream = [System.IO.File]::Open($SourcePath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            $destStream = [System.IO.File]::Create($DestinationPath)
            try {
                $sourceStream.CopyTo($destStream)
            } finally {
                $destStream.Dispose()
                $sourceStream.Dispose()
            }
            return $true
        } catch {
            Write-Warning "Could not copy $SourcePath (file locked or inaccessible): $_"
            return $false
        }
    }
}

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "stroboverlay_collect_$timestamp"
try {
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

    $copiedCount = 0
    foreach ($file in $itemsToZip) {
        $relPath = if ($file.StartsWith($root.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
            $file.Substring($root.Path.Length).TrimStart('\', '/')
        } else {
            [System.IO.Path]::GetFileName($file)
        }
        $destFile = Join-Path $tempDir $relPath
        $destFileDir = [System.IO.Path]::GetDirectoryName($destFile)
        if (-not (Test-Path $destFileDir)) {
            New-Item -ItemType Directory -Path $destFileDir -Force | Out-Null
        }

        if (Copy-FileSafely -SourcePath $file -DestinationPath $destFile) {
            $copiedCount++
        }
    }

    if ($copiedCount -eq 0) {
        Write-Warning "No logs or dumps could be collected to archive."
        exit 1
    }

    Write-Host "Creating archive $zipPath ..."
    Compress-Archive -Path (Join-Path $tempDir "*") -DestinationPath $zipPath -Force

    Write-Host "Archive created successfully."
}
finally {
    if (Test-Path $tempDir) {
        Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
