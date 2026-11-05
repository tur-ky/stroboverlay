@echo off
setlocal
cd /d "%~dp0"

:: Determine the path to MSBuild. Prefer Visual Studio 2022 Professional if installed.
set "MSBUILD_EXE="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_EXE if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_EXE (
  echo Could not find MSBuild. Please install Visual Studio 2022 Build Tools.
  if not defined CI pause
  exit /b 1
)

:: Build the solution in Release x64 configuration into bin\Release
"%MSBUILD_EXE%" "stroboverlay.sln" /m /v:m /t:Build /p:Configuration=Release;Platform=x64
if errorlevel 1 (
  echo Build failed.
  if not defined CI pause
  exit /b 1
)

echo Build succeeded. The executable can be found at bin\Release\stroboverlay.exe
if not defined CI pause