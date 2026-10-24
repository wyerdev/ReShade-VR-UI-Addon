@echo off
setlocal EnableExtensions

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

rem fltmc only succeeds in a window with administrator rights.
fltmc >nul 2>&1
if not errorlevel 1 goto :has_administrator
if /I "%~1"=="--elevated" goto :administrator_required
if defined RESHADE_OPENXR_UI_NONINTERACTIVE goto :administrator_required
echo Asking Windows for administrator rights. Click Yes to continue.
echo The uninstall continues in a new window.
rem Restart this script elevated and wait for it. The elevated copy pauses itself.
set "ELEVATE_ARGUMENTS=/c ""%~f0" --elevated""
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "try { $p = Start-Process -FilePath $env:ComSpec -ArgumentList $env:ELEVATE_ARGUMENTS -Verb RunAs -PassThru -ErrorAction Stop; $null = $p.Handle; $p.WaitForExit(); exit $p.ExitCode } catch { exit 1223 }"
if errorlevel 1223 if not errorlevel 1224 goto :administrator_required
exit /b %ERRORLEVEL%

:has_administrator
if exist "%INSTALL_LISTENER%" "%INSTALL_LISTENER%" --stop >nul 2>&1
if exist "%LEGACY_LISTENER%" "%LEGACY_LISTENER%" --stop >nul 2>&1
if exist "%LEGACY_LOADER%" "%LEGACY_LOADER%" --stop >nul 2>&1
reg delete "%RUN_KEY%" /v "%RUN_VALUE%" /f >nul 2>&1
reg delete "%RUN_KEY%" /v "%LEGACY_RUN_VALUE%" /f >nul 2>&1
reg query "%RUN_KEY%" /v "%RUN_VALUE%" >nul 2>&1
if not errorlevel 1 goto :failed

del /F /Q "%INSTALL_ADDON%" >nul 2>&1
del /F /Q "%INSTALL_LISTENER%" >nul 2>&1
del /F /Q "%LEGACY_ADDON%" >nul 2>&1
del /F /Q "%LEGACY_LISTENER%" >nul 2>&1
del /F /Q "%LEGACY_LOADER%" >nul 2>&1
rmdir "%INSTALL_DIRECTORY%" >nul 2>&1
rmdir "%LEGACY_DIRECTORY%" >nul 2>&1

if exist "%INSTALL_ADDON%" goto :failed
if exist "%INSTALL_LISTENER%" goto :failed
if exist "%LEGACY_LISTENER%" goto :failed
if exist "%LEGACY_LOADER%" goto :failed

echo ReShade System-Wide SteamVR Listener uninstalled successfully.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:administrator_required
echo ReShade System-Wide SteamVR Listener requires administrator access to uninstall.
echo Windows did not give administrator rights. Nothing was uninstalled.
echo Right-click Uninstall-System-Wide-SteamVR-Listener.cmd and select Run as administrator.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:failed
echo Listener uninstall could not remove the startup entry or files.
echo Close running SteamVR applications and try again.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1