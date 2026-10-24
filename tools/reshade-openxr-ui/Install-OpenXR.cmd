@echo off
setlocal EnableExtensions

rem Console colour codes so errors show in red and success in green.
for /F %%A in ('echo prompt $E^| cmd') do set "ESC=%%A"
set "RED=%ESC%[91m"
set "GREEN=%ESC%[92m"
set "RESET=%ESC%[0m"

set "SOURCE_ADDON=%~dp0ReShade-OpenXR-UI.addon64"
set "SOURCE_MANIFEST=%~dp0openxr-layers\ReShade-OpenXR-UI-Layer.json"
set "INSTALL_DIRECTORY=%ProgramFiles%\ReShade VR UI Add-on"
set "INSTALL_LAYER_DIRECTORY=%INSTALL_DIRECTORY%\openxr-layers"
set "INSTALL_ADDON=%INSTALL_DIRECTORY%\ReShade-OpenXR-UI.addon64"
set "INSTALL_MANIFEST=%INSTALL_LAYER_DIRECTORY%\ReShade-OpenXR-UI-Layer.json"
set "REGISTRY_KEY=HKLM\Software\Khronos\OpenXR\1\ApiLayers\Implicit"
set "CENTRAL_RESHADE=%ProgramData%\ReShade\ReShade64.dll"
set "CENTRAL_APPS=%ProgramData%\ReShade\ReShadeApps.ini"

echo ================================================================
echo  ReShade VR UI Add-on for OpenXR - Install
echo ================================================================
echo.
echo This installer needs administrator rights because it:
echo   1. Copies the add-on to "%INSTALL_DIRECTORY%".
echo      Only administrators can change files in Program Files.
echo   2. Switches the add-on on for all your OpenXR games. Windows
echo      keeps that list in a protected part of the registry:
echo      HKEY_LOCAL_MACHINE\Software\Khronos\OpenXR\1\ApiLayers\Implicit
echo It does not change your game folders or your ReShade install.
echo.

if not exist "%SOURCE_ADDON%" goto :incomplete
if not exist "%SOURCE_MANIFEST%" goto :incomplete
if not exist "%CENTRAL_RESHADE%" goto :missing_reshade
if not exist "%CENTRAL_APPS%" goto :missing_reshade

rem fltmc only succeeds in a window with administrator rights.
fltmc >nul 2>&1
if not errorlevel 1 goto :has_administrator
if /I "%~1"=="--elevated" goto :administrator_required
if defined RESHADE_OPENXR_UI_NONINTERACTIVE goto :administrator_required
echo Asking Windows for administrator rights. Click Yes to continue.
echo The install continues in a new window.
rem Restart this script elevated and wait for it. The elevated copy pauses itself.
set "ELEVATE_ARGUMENTS=/c ""%~f0" --elevated""
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "try { $p = Start-Process -FilePath $env:ComSpec -ArgumentList $env:ELEVATE_ARGUMENTS -Verb RunAs -PassThru -ErrorAction Stop; $null = $p.Handle; $p.WaitForExit(); exit $p.ExitCode } catch { exit 1223 }"
if errorlevel 1223 if not errorlevel 1224 goto :administrator_required
exit /b %ERRORLEVEL%

:has_administrator
set "LAYER_STATE=0"
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /t REG_DWORD /reg:64 2>nul | findstr /R /C:"REG_DWORD *0x1" >nul
if not errorlevel 1 set "LAYER_STATE=1"

if exist "%INSTALL_LAYER_DIRECTORY%" goto :copy_files
set "FAILURE_STEP=creating the install folder"
set "FAILURE_PATH=%INSTALL_LAYER_DIRECTORY%"
mkdir "%INSTALL_LAYER_DIRECTORY%"
if errorlevel 1 goto :failed

:copy_files
set "FAILURE_STEP=copying the OpenXR add-on"
set "FAILURE_PATH=%INSTALL_ADDON%"
copy /Y "%SOURCE_ADDON%" "%INSTALL_ADDON%" >nul
if errorlevel 1 goto :failed
set "FAILURE_STEP=copying the OpenXR layer file"
set "FAILURE_PATH=%INSTALL_MANIFEST%"
copy /Y "%SOURCE_MANIFEST%" "%INSTALL_MANIFEST%" >nul
if errorlevel 1 goto :failed

set "FAILURE_STEP=switching the add-on on for OpenXR games"
set "FAILURE_PATH=%INSTALL_MANIFEST%"
reg add "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /t REG_DWORD /d %LAYER_STATE% /f /reg:64 >nul
if errorlevel 1 goto :failed
set "FAILURE_STEP=checking that the add-on is switched on for OpenXR games"
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /t REG_DWORD /reg:64 >nul 2>&1
if errorlevel 1 goto :failed

echo %GREEN%----------------------------------------------------------------
echo  DONE: ReShade VR UI Add-on for OpenXR is installed.
echo ----------------------------------------------------------------
echo It starts automatically in games where ReShade with full add-on
echo support is installed with the OpenXR layer ticked.%RESET%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:incomplete
echo %RED%----------------------------------------------------------------
echo  ERROR: Files are missing. Nothing was installed.
echo ----------------------------------------------------------------
echo This file must be next to ReShade-OpenXR-UI.addon64 and the
echo openxr-layers folder from the same ZIP.
echo Extract the complete ZIP to a folder, then run %~nx0 from there.
echo Running it from inside the ZIP without extracting does not work.%RESET%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:missing_reshade
echo %RED%----------------------------------------------------------------
echo  ERROR: ReShade is not installed. Nothing was installed.
echo ----------------------------------------------------------------
echo The add-on needs ReShade with full add-on support.
echo Install it from https://reshade.me/ for one of your games, with the
echo OpenXR layer ticked, then run %~nx0 again.%RESET%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:administrator_required
echo %RED%----------------------------------------------------------------
echo  ERROR: No administrator rights. Nothing was installed.
echo ----------------------------------------------------------------
echo Windows did not give administrator rights (the permission
echo request was declined), so Windows blocks the 2 steps above.
echo.
echo To fix it:
echo   1. Close this window.
echo   2. Right-click %~nx0 and choose "Run as administrator".
echo   3. Click Yes when Windows asks for permission.
echo If your Windows account is not an administrator, ask the person
echo who manages this PC to do these steps.%RESET%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:failed
set "FAILURE_CODE=%ERRORLEVEL%"
echo %RED%----------------------------------------------------------------
echo  ERROR: The install did not finish.
echo ----------------------------------------------------------------
echo It stopped while %FAILURE_STEP%.
if defined FAILURE_PATH echo Path: %FAILURE_PATH%
echo Windows error code: %FAILURE_CODE%
echo.
echo Close all games and SteamVR, then run %~nx0 as administrator
echo again. A game or SteamVR that is still open can keep the old
echo files locked.%RESET%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1
