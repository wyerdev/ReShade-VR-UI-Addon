@echo off
setlocal EnableExtensions

set "INSTALL_DIRECTORY=%ProgramFiles%\ReShade VR UI Add-on"
set "INSTALL_ADDON=%INSTALL_DIRECTORY%\ReShade-SteamVR-UI.addon64"
set "INSTALL_LISTENER=%INSTALL_DIRECTORY%\ReShade-System-Wide-SteamVR-Listener.exe"
set "LEGACY_LOADER=%INSTALL_DIRECTORY%\ReShade-VR-Loader.exe"
set "RUN_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"
set "RUN_VALUE=ReShade System-Wide SteamVR Listener"
set "LEGACY_RUN_VALUE=ReShade VR UI Loader"

fltmc >nul 2>&1
if errorlevel 1 goto :administrator_required

if exist "%INSTALL_LISTENER%" "%INSTALL_LISTENER%" --stop >nul 2>&1
if exist "%LEGACY_LOADER%" "%LEGACY_LOADER%" --stop >nul 2>&1
reg delete "%RUN_KEY%" /v "%RUN_VALUE%" /f >nul 2>&1
reg delete "%RUN_KEY%" /v "%LEGACY_RUN_VALUE%" /f >nul 2>&1
reg query "%RUN_KEY%" /v "%RUN_VALUE%" >nul 2>&1
if not errorlevel 1 goto :failed

del /F /Q "%INSTALL_ADDON%" >nul 2>&1
del /F /Q "%INSTALL_LISTENER%" >nul 2>&1
del /F /Q "%LEGACY_LOADER%" >nul 2>&1
rmdir "%INSTALL_DIRECTORY%" >nul 2>&1

if exist "%INSTALL_ADDON%" goto :failed
if exist "%INSTALL_LISTENER%" goto :failed
if exist "%LEGACY_LOADER%" goto :failed

echo ReShade System-Wide SteamVR Listener uninstalled successfully.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:administrator_required
echo ReShade System-Wide SteamVR Listener requires administrator access to uninstall.
echo Right-click Uninstall-System-Wide-SteamVR-Listener.cmd and select Run as administrator.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:failed
echo Listener uninstall could not remove the startup entry or files.
echo Close running SteamVR applications and try again.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1