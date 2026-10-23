@echo off
setlocal EnableExtensions

set "SOURCE_ADDON=%~dp0ReShade-SteamVR-UI.addon64"
set "SOURCE_LISTENER=%~dp0ReShade-System-Wide-SteamVR-Listener.exe"
set "INSTALL_DIRECTORY=%ProgramFiles%\ReShade System-Wide SteamVR Listener"
set "INSTALL_ADDON=%INSTALL_DIRECTORY%\ReShade-SteamVR-UI.addon64"
set "INSTALL_LISTENER=%INSTALL_DIRECTORY%\ReShade-System-Wide-SteamVR-Listener.exe"
rem Older versions installed the listener into the add-on's folder.
set "LEGACY_DIRECTORY=%ProgramFiles%\ReShade VR UI Add-on"
set "LEGACY_ADDON=%LEGACY_DIRECTORY%\ReShade-SteamVR-UI.addon64"
set "LEGACY_LISTENER=%LEGACY_DIRECTORY%\ReShade-System-Wide-SteamVR-Listener.exe"
set "LEGACY_LOADER=%LEGACY_DIRECTORY%\ReShade-VR-Loader.exe"
set "RUN_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"
set "RUN_VALUE=ReShade System-Wide SteamVR Listener"
set "LEGACY_RUN_VALUE=ReShade VR UI Loader"
set "CENTRAL_RESHADE=%ProgramData%\ReShade\ReShade64.dll"
set "CENTRAL_APPS=%ProgramData%\ReShade\ReShadeApps.ini"

if not exist "%SOURCE_ADDON%" goto :incomplete
if not exist "%SOURCE_LISTENER%" goto :incomplete
if not exist "%CENTRAL_RESHADE%" goto :missing_reshade
if not exist "%CENTRAL_APPS%" goto :missing_reshade

fltmc >nul 2>&1
if errorlevel 1 goto :administrator_required

if exist "%INSTALL_LISTENER%" "%INSTALL_LISTENER%" --stop
if errorlevel 1 goto :failed
if exist "%LEGACY_LISTENER%" "%LEGACY_LISTENER%" --stop
if errorlevel 1 goto :failed
if exist "%LEGACY_LOADER%" "%LEGACY_LOADER%" --stop
if errorlevel 1 goto :failed

if exist "%INSTALL_DIRECTORY%" goto :copy_files
set "FAILURE_STEP=creating the install directory"
set "FAILURE_PATH=%INSTALL_DIRECTORY%"
mkdir "%INSTALL_DIRECTORY%"
if errorlevel 1 goto :failed

:copy_files
set "FAILURE_STEP=copying the SteamVR add-on"
set "FAILURE_PATH=%INSTALL_ADDON%"
copy /Y "%SOURCE_ADDON%" "%INSTALL_ADDON%"
if errorlevel 1 goto :failed
set "FAILURE_STEP=copying the system-wide SteamVR listener"
set "FAILURE_PATH=%INSTALL_LISTENER%"
copy /Y "%SOURCE_LISTENER%" "%INSTALL_LISTENER%"
if errorlevel 1 goto :failed

reg delete "%RUN_KEY%" /v "%LEGACY_RUN_VALUE%" /f >nul 2>&1
del /F /Q "%LEGACY_LOADER%" >nul 2>&1
del /F /Q "%LEGACY_LISTENER%" >nul 2>&1
del /F /Q "%LEGACY_ADDON%" >nul 2>&1
rmdir "%LEGACY_DIRECTORY%" >nul 2>&1
set "FAILURE_STEP=registering the listener to start with Windows"
reg add "%RUN_KEY%" /v "%RUN_VALUE%" /t REG_SZ /d "\"%INSTALL_LISTENER%\"" /f
if errorlevel 1 goto :failed
reg query "%RUN_KEY%" /v "%RUN_VALUE%" >nul 2>&1
if errorlevel 1 goto :failed

set "FAILURE_STEP=starting the system-wide SteamVR listener"
start "" /B "%INSTALL_LISTENER%"
if errorlevel 1 goto :failed

echo ReShade System-Wide SteamVR Listener installed successfully.
echo It starts with Windows and applies to compatible SteamVR games enabled in ReShade Setup.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:incomplete
echo The listener release files are incomplete. Extract the complete ZIP and try again.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:missing_reshade
echo Official ReShade is not installed centrally.
echo Install ReShade with full add-on support, then try again.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:administrator_required
echo ReShade System-Wide SteamVR Listener requires administrator access to install.
echo Right-click Install-System-Wide-SteamVR-Listener.cmd and select Run as administrator.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:failed
echo Listener installation failed during: %FAILURE_STEP%.
if defined FAILURE_PATH echo Path: %FAILURE_PATH%
echo Windows error level: %ERRORLEVEL%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1