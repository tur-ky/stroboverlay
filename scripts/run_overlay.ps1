$ErrorActionPreference = "Stop"

$exePath = Join-Path $PSScriptRoot "..\bin\Release\stroboverlay.exe"

if (-not (Test-Path $exePath)) {
    Write-Error "Executable not found at $exePath. Please build first."
    exit 1
}

Write-Host "Launching stroboverlay..."
Start-Process -FilePath $exePath
