@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"

call "%ROOT%build-addon.bat" %*
if errorlevel 1 exit /b 1

call "%ROOT%build-listener.bat"
if errorlevel 1 exit /b 1

echo Add-on and SteamVR listener build and package completed.
exit /b 0