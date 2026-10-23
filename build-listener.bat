@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"

if not exist "%ROOT%build\reshade-openxr-ui\ReShade-SteamVR-UI.addon64" (
    echo The SteamVR add-on has not been built yet.
    echo Run build-addon.bat or build.bat first.
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\steamvr-listener\build.ps1"
if errorlevel 1 exit /b 1

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\steamvr-listener\package.ps1"
if errorlevel 1 exit /b 1

echo SteamVR listener build and package completed.
echo Package: "%ROOT%build\steamvr-listener-release\ReShade-System-Wide-SteamVR-Listener.zip"
exit /b 0
