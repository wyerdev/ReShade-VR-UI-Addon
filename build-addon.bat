@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"

if not exist "%ROOT%.deps\reshade\include\reshade.hpp" (
    echo ReShade dependency checkout is missing. Setting it up now...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%setup-reshade.ps1" -Action Setup
    if errorlevel 1 exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%build-openxr-ui.ps1" %*
if errorlevel 1 exit /b 1

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\reshade-openxr-ui\package.ps1"
if errorlevel 1 exit /b 1

echo Add-on build and package completed.
echo Install with: "%ROOT%install.bat"
exit /b 0
