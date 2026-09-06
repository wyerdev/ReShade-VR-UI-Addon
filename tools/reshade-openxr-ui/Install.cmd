@echo off
setlocal EnableExtensions

set "SOURCE_ADDON=%~dp0ReShade-OpenXR-UI.addon64"
set "SOURCE_STEAMVR_ADDON=%~dp0ReShade-SteamVR-UI.addon64"
set "SOURCE_LOADER=%~dp0ReShade-VR-Loader.exe"
set "SOURCE_MANIFEST=%~dp0openxr-layers\ReShade-OpenXR-UI-Layer.json"
set "INSTALL_DIRECTORY=%ProgramFiles%\ReShade VR UI Add-on"
set "INSTALL_LAYER_DIRECTORY=%INSTALL_DIRECTORY%\openxr-layers"
set "INSTALL_ADDON=%INSTALL_DIRECTORY%\ReShade-OpenXR-UI.addon64"
set "INSTALL_STEAMVR_ADDON=%INSTALL_DIRECTORY%\ReShade-SteamVR-UI.addon64"
set "INSTALL_LOADER=%INSTALL_DIRECTORY%\ReShade-VR-Loader.exe"
set "INSTALL_MANIFEST=%INSTALL_LAYER_DIRECTORY%\ReShade-OpenXR-UI-Layer.json"
set "REGISTRY_KEY=HKLM\Software\Khronos\OpenXR\1\ApiLayers\Implicit"
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

fltmc >nul 2>&1
if errorlevel 1 goto :administrator_required

:prepare_install
set "LAYER_STATE=0"
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /t REG_DWORD /reg:64 2>nul | findstr /R /C:"REG_DWORD *0x1" >nul
if not errorlevel 1 set "LAYER_STATE=1"

if exist "%INSTALL_LOADER%" (
	set "FAILURE_STEP=stopping the installed loader"
	set "FAILURE_PATH=%INSTALL_LOADER%"
	"%INSTALL_LOADER%" --stop
	if errorlevel 1 goto :failed
)

if exist "%INSTALL_LAYER_DIRECTORY%" goto :copy_files
set "FAILURE_STEP=creating the install directory"
set "FAILURE_PATH=%INSTALL_LAYER_DIRECTORY%"
mkdir "%INSTALL_LAYER_DIRECTORY%"
if errorlevel 1 goto :failed

:copy_files
set "FAILURE_STEP=copying the OpenXR add-on"
set "FAILURE_PATH=%INSTALL_ADDON%"
copy /Y "%SOURCE_ADDON%" "%INSTALL_ADDON%"
if errorlevel 1 goto :failed
set "FAILURE_STEP=copying the SteamVR add-on"
set "FAILURE_PATH=%INSTALL_STEAMVR_ADDON%"
copy /Y "%SOURCE_STEAMVR_ADDON%" "%INSTALL_STEAMVR_ADDON%"
if errorlevel 1 goto :failed
set "FAILURE_STEP=copying the loader"
set "FAILURE_PATH=%INSTALL_LOADER%"
copy /Y "%SOURCE_LOADER%" "%INSTALL_LOADER%"
if errorlevel 1 goto :failed
set "FAILURE_STEP=copying the OpenXR layer manifest"
set "FAILURE_PATH=%INSTALL_MANIFEST%"
copy /Y "%SOURCE_MANIFEST%" "%INSTALL_MANIFEST%"
if errorlevel 1 goto :failed

set "FAILURE_STEP=registering the OpenXR API layer"
set "FAILURE_PATH=%INSTALL_MANIFEST%"
reg add "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /t REG_DWORD /d %LAYER_STATE% /f /reg:64
if errorlevel 1 goto :failed
set "FAILURE_STEP=verifying the OpenXR API-layer registration"
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /t REG_DWORD /reg:64 >nul 2>&1
if errorlevel 1 goto :failed

set "FAILURE_STEP=registering loader startup"
set "FAILURE_PATH=%INSTALL_LOADER%"
reg add "%RUN_KEY%" /v "%RUN_VALUE%" /t REG_SZ /d "\"%INSTALL_LOADER%\"" /f
if errorlevel 1 goto :failed
set "FAILURE_STEP=verifying loader startup registration"
reg query "%RUN_KEY%" /v "%RUN_VALUE%" >nul 2>&1
if errorlevel 1 goto :failed

set "FAILURE_STEP=starting the loader"
start "" /B "%INSTALL_LOADER%"
if errorlevel 1 goto :failed

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

:administrator_required
echo ReShade VR UI Add-on requires administrator access to install.
echo Right-click Install.cmd and select Run as administrator.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:failed
echo Installation failed during: %FAILURE_STEP%.
if defined FAILURE_PATH echo Path: %FAILURE_PATH%
echo Windows error level: %ERRORLEVEL%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1