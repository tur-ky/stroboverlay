Param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Move to repo root
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

if ($Clean) {
    if (Test-Path "bin\Release") {
        Write-Host "Cleaning bin\Release ..."
        Remove-Item -Recurse -Force "bin\Release"
    }
}

$msbuildPaths = @(
  "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
  "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuild = $msbuildPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $msbuild) {
  $msbuild = "msbuild"  # fallback if VS is already on PATH
}

Write-Host "Using MSBuild at $msbuild"
$Solution = "stroboverlay.sln"
$Config = "Release"
$Platform = "x64"

# Directly invoke MSBuild without calling build.bat
& $msbuild $Solution /t:Build /p:Configuration=$Config /p:Platform=$Platform /m /v:m
exit $LASTEXITCODE
