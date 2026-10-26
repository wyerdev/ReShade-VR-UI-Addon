@echo off
setlocal EnableExtensions

rem Console colour codes so errors show in red and success in green.
for /F %%A in ('echo prompt $E^| cmd') do set "ESC=%%A"
set "RED=%ESC%[91m"
set "GREEN=%ESC%[92m"
set "RESET=%ESC%[0m"

set "INSTALL_DIRECTORY=%ProgramFiles%\ReShade VR UI Add-on"
set "INSTALL_LAYER_DIRECTORY=%INSTALL_DIRECTORY%\openxr-layers"
set "INSTALL_ADDON=%INSTALL_DIRECTORY%\ReShade-OpenXR-UI.addon64"
set "INSTALL_ADDON32=%INSTALL_DIRECTORY%\ReShade-OpenXR-UI.addon32"
set "INSTALL_MANIFEST=%INSTALL_LAYER_DIRECTORY%\ReShade-OpenXR-UI-Layer.json"
set "INSTALL_MANIFEST32=%INSTALL_LAYER_DIRECTORY%\ReShade-OpenXR-UI-Layer32.json"
set "PREFERENCES_DIRECTORY=%LOCALAPPDATA%\ReShade VR UI Add-on"
rem /reg:64 reaches the 64-bit list, /reg:32 the 32-bit list (WOW6432Node).
set "REGISTRY_KEY=HKLM\Software\Khronos\OpenXR\1\ApiLayers\Implicit"

echo ================================================================
echo  ReShade VR UI Add-on for OpenXR - Uninstall
echo ================================================================
echo.
echo This uninstaller needs administrator rights because it:
echo   1. Switches the add-on off for all your OpenXR games. Windows
echo      keeps those lists in a protected part of the registry:
echo      HKEY_LOCAL_MACHINE\Software\Khronos\OpenXR\1\ApiLayers\Implicit
echo      HKEY_LOCAL_MACHINE\Software\WOW6432Node\Khronos\OpenXR\1\ApiLayers\Implicit
echo   2. Deletes the add-on from "%INSTALL_DIRECTORY%".
echo      Only administrators can change files in Program Files.
echo It does not change your game folders or your ReShade install.
echo.

set "IS_INSTALLED=0"
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /reg:64 >nul 2>&1
if not errorlevel 1 set "IS_INSTALLED=1"
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST32%" /reg:32 >nul 2>&1
if not errorlevel 1 set "IS_INSTALLED=1"
if exist "%INSTALL_ADDON%" set "IS_INSTALLED=1"
if exist "%INSTALL_ADDON32%" set "IS_INSTALLED=1"
if exist "%INSTALL_MANIFEST%" set "IS_INSTALLED=1"
if exist "%INSTALL_MANIFEST32%" set "IS_INSTALLED=1"
if "%IS_INSTALLED%"=="0" goto :not_installed

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
reg delete "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /f /reg:64 >nul 2>&1
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST%" /reg:64 >nul 2>&1
if not errorlevel 1 goto :registry_failed
reg delete "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST32%" /f /reg:32 >nul 2>&1
reg query "%REGISTRY_KEY%" /v "%INSTALL_MANIFEST32%" /reg:32 >nul 2>&1
if not errorlevel 1 goto :registry_failed

del /F /Q "%INSTALL_ADDON%" >nul 2>&1
del /F /Q "%INSTALL_ADDON32%" >nul 2>&1
del /F /Q "%INSTALL_MANIFEST%" >nul 2>&1
del /F /Q "%INSTALL_MANIFEST32%" >nul 2>&1
rmdir "%INSTALL_LAYER_DIRECTORY%" >nul 2>&1
rmdir "%INSTALL_DIRECTORY%" >nul 2>&1

if exist "%INSTALL_ADDON%" goto :files_failed
if exist "%INSTALL_ADDON32%" goto :files_failed
if exist "%INSTALL_MANIFEST%" goto :files_failed
if exist "%INSTALL_MANIFEST32%" goto :files_failed

echo %GREEN%----------------------------------------------------------------
echo  DONE: ReShade VR UI Add-on for OpenXR is uninstalled.
echo ----------------------------------------------------------------
echo Your saved settings were kept in "%PREFERENCES_DIRECTORY%".
echo Delete that folder too if you do not plan to install it again.%RESET%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:not_installed
echo ----------------------------------------------------------------
echo  ReShade VR UI Add-on for OpenXR is not installed.
echo ----------------------------------------------------------------
echo Nothing to remove, so nothing was changed.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:administrator_required
echo %RED%----------------------------------------------------------------
echo  ERROR: No administrator rights. Nothing was uninstalled.
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

:registry_failed
echo %RED%----------------------------------------------------------------
echo  ERROR: The uninstall did not finish. Nothing was removed.
echo ----------------------------------------------------------------
echo Windows did not let it switch the add-on off for OpenXR games.
echo Run %~nx0 as administrator again.%RESET%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1

:files_failed
echo %RED%----------------------------------------------------------------
echo  ERROR: The uninstall did not finish.
echo ----------------------------------------------------------------
echo The add-on is switched off for OpenXR games, but some files in
echo "%INSTALL_DIRECTORY%" could not be deleted.
echo A game or SteamVR that is still open can keep them locked.
echo Close all games and SteamVR, then run %~nx0 as administrator again.%RESET%
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 1
