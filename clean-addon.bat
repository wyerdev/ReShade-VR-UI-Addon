@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%clean-build.ps1" %*
if errorlevel 1 exit /b %ERRORLEVEL%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\reshade-openxr-ui\package.ps1"
if errorlevel 1 exit /b %ERRORLEVEL%

echo Clean add-on rebuild and package completed.
exit /b 0
