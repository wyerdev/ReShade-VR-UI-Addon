@echo off
setlocal EnableExtensions

set "INSTALL_DIRECTORY=%LOCALAPPDATA%\ReShade VR UI Add-on"
set "INSTALL_LAYER_DIRECTORY=%INSTALL_DIRECTORY%\openxr-layers"
set "INSTALL_ADDON=%INSTALL_DIRECTORY%\ReShade-OpenXR-UI.addon64"
set "INSTALL_STEAMVR_ADDON=%INSTALL_DIRECTORY%\ReShade-SteamVR-UI.addon64"
set "INSTALL_LOADER=%INSTALL_DIRECTORY%\ReShade-VR-Loader.exe"
set "INSTALL_MANIFEST=%INSTALL_LAYER_DIRECTORY%\ReShade-OpenXR-UI-Layer.json"
set "REGISTRY_KEY=HKCU\Software\Khronos\OpenXR\1\ApiLayers\Implicit"
set "RUN_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"
set "RUN_VALUE=ReShade VR UI Loader"

if exist "%INSTALL_LOADER%" "%INSTALL_LOADER%" --stop >nul 2>&1
reg delete "%RUN_KEY%" /v "%RUN_VALUE%" /f >nul 2>&1
reg query "%RUN_KEY%" /v "%RUN_VALUE%" >nul 2>&1
if not errorlevel 1 goto :failed

reg delete "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /f >nul 2>&1
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" >nul 2>&1
if not errorlevel 1 goto :failed
del /F /Q "%INSTALL_ADDON%" >nul 2>&1
del /F /Q "%INSTALL_STEAMVR_ADDON%" >nul 2>&1
del /F /Q "%INSTALL_LOADER%" >nul 2>&1
del /F /Q "%INSTALL_MANIFEST%" >nul 2>&1
rmdir "%INSTALL_LAYER_DIRECTORY%" >nul 2>&1

if exist "%INSTALL_ADDON%" goto :failed
if exist "%INSTALL_STEAMVR_ADDON%" goto :failed
if exist "%INSTALL_LOADER%" goto :failed
if exist "%INSTALL_MANIFEST%" goto :failed

echo ReShade VR UI Add-on uninstalled successfully.
echo Your saved preferences were kept in "%INSTALL_DIRECTORY%".
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:failed
echo Uninstall could not remove the registration or files.
echo Close running OpenXR and SteamVR applications and try again.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1