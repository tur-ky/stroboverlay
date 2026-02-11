$ErrorActionPreference = "Stop"

# Move to repo root
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$logsDir = Join-Path $root "logs"
$outputDir = Join-Path $root "bin\Release"

if (-not (Test-Path $logsDir)) {
    Write-Warning "Logs directory not found: $logsDir"
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$zipName = "stroboverlay_logs_$timestamp.zip"
$zipPath = Join-Path $root $zipName

$itemsToZip = @()
if (Test-Path $logsDir) {
    $itemsToZip += $logsDir
}
if (Test-Path $outputDir) {
    $itemsToZip += Get-ChildItem -Path $outputDir -Include *.dmp,*.log -File -Recurse
}

if ($itemsToZip.Count -eq 0) {
    Write-Warning "No logs or dumps found to archive."
    exit 1
}

Write-Host "Creating archive $zipPath ..."
Compress-Archive -Path $itemsToZip -DestinationPath $zipPath -Force

Write-Host "Archive created successfully."
