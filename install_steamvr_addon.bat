@echo off
setlocal
rem Copies the locally built SteamVR add-on next to Propagation: Paradise Hotel's game exe.
rem   install_steamvr_addon.bat             install (or update) the add-on
rem   install_steamvr_addon.bat uninstall   remove the add-on
rem   install_steamvr_addon.bat status      show what is installed, change nothing
rem Optional second argument: another SteamVR game's exe.
rem ReShade must already be installed for the game with ReShade Setup.

set "ACTION=Install"
if /I "%~1"=="uninstall" set "ACTION=Uninstall"
if /I "%~1"=="status" set "ACTION=Status"

if "%~2"=="" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install_steamvr_addon.ps1" -Action %ACTION%
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install_steamvr_addon.ps1" -Action %ACTION% -GameExe "%~2"
)
set "RESULT=%ERRORLEVEL%"
endlocal & set "RESULT=%RESULT%"
pause
exit /b %RESULT%
