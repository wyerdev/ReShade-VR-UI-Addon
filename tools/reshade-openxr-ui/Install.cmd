@echo off
setlocal EnableExtensions

set "SOURCE_ADDON=%~dp0ReShade-OpenXR-UI.addon64"
set "SOURCE_STEAMVR_ADDON=%~dp0ReShade-SteamVR-UI.addon64"
set "SOURCE_LOADER=%~dp0ReShade-VR-Loader.exe"
set "SOURCE_MANIFEST=%~dp0openxr-layers\ReShade-OpenXR-UI-Layer.json"
set "INSTALL_DIRECTORY=%LOCALAPPDATA%\ReShade VR UI Add-on"
set "INSTALL_LAYER_DIRECTORY=%INSTALL_DIRECTORY%\openxr-layers"
set "INSTALL_ADDON=%INSTALL_DIRECTORY%\ReShade-OpenXR-UI.addon64"
set "INSTALL_STEAMVR_ADDON=%INSTALL_DIRECTORY%\ReShade-SteamVR-UI.addon64"
set "INSTALL_LOADER=%INSTALL_DIRECTORY%\ReShade-VR-Loader.exe"
set "INSTALL_MANIFEST=%INSTALL_LAYER_DIRECTORY%\ReShade-OpenXR-UI-Layer.json"
set "REGISTRY_KEY=HKCU\Software\Khronos\OpenXR\1\ApiLayers\Implicit"
set "RUN_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"
set "RUN_VALUE=ReShade VR UI Loader"
set "CENTRAL_RESHADE=%ProgramData%\ReShade\ReShade64.dll"
set "CENTRAL_APPS=%ProgramData%\ReShade\ReShadeApps.ini"

if not exist "%SOURCE_ADDON%" goto :incomplete
if not exist "%SOURCE_STEAMVR_ADDON%" goto :incomplete
if not exist "%SOURCE_LOADER%" goto :incomplete
if not exist "%SOURCE_MANIFEST%" goto :incomplete
if not exist "%CENTRAL_RESHADE%" goto :missing_reshade
if not exist "%CENTRAL_APPS%" goto :missing_reshade

if exist "%INSTALL_LOADER%" "%INSTALL_LOADER%" --stop >nul 2>&1

if not exist "%INSTALL_LAYER_DIRECTORY%" mkdir "%INSTALL_LAYER_DIRECTORY%"
if errorlevel 1 goto :failed

copy /Y "%SOURCE_ADDON%" "%INSTALL_ADDON%" >nul
if errorlevel 1 goto :failed
copy /Y "%SOURCE_STEAMVR_ADDON%" "%INSTALL_STEAMVR_ADDON%" >nul
if errorlevel 1 goto :failed
copy /Y "%SOURCE_LOADER%" "%INSTALL_LOADER%" >nul
if errorlevel 1 goto :failed
copy /Y "%SOURCE_MANIFEST%" "%INSTALL_MANIFEST%" >nul
if errorlevel 1 goto :failed

reg add "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /t REG_DWORD /d 0 /f >nul
if errorlevel 1 goto :failed
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" >nul 2>&1
if errorlevel 1 goto :failed
reg add "%RUN_KEY%" /v "%RUN_VALUE%" /t REG_SZ /d "\"%INSTALL_LOADER%\"" /f >nul
if errorlevel 1 goto :failed
reg query "%RUN_KEY%" /v "%RUN_VALUE%" >nul 2>&1
if errorlevel 1 goto :failed

start "" /B "%INSTALL_LOADER%"

echo ReShade VR UI Add-on installed successfully.
echo It will start automatically in compatible ReShade OpenXR and SteamVR/OpenVR games.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:incomplete
echo The release files are incomplete. Extract the complete ZIP and try again.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:missing_reshade
echo Official ReShade OpenXR is not installed centrally.
echo Install ReShade with full add-on support and OpenXR enabled, then try again.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:failed
echo Installation failed. Close running OpenXR and SteamVR applications and try again.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1