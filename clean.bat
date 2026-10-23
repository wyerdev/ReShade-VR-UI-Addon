@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"

call "%ROOT%clean-addon.bat" %*
if errorlevel 1 exit /b 1

call "%ROOT%clean-listener.bat"
if errorlevel 1 exit /b 1

echo Clean rebuild and package of the add-on and SteamVR listener completed.
exit /b 0