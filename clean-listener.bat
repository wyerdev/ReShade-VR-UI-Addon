@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "LISTENER_BUILD=%ROOT%build\steamvr-listener"
set "LISTENER_RELEASE=%ROOT%build\steamvr-listener-release"

if exist "%LISTENER_BUILD%" rmdir /s /q "%LISTENER_BUILD%"
if exist "%LISTENER_BUILD%" (
    echo Could not remove "%LISTENER_BUILD%".
    exit /b 1
)
if exist "%LISTENER_RELEASE%" rmdir /s /q "%LISTENER_RELEASE%"
if exist "%LISTENER_RELEASE%" (
    echo Could not remove "%LISTENER_RELEASE%".
    exit /b 1
)

call "%ROOT%build-listener.bat"
if errorlevel 1 exit /b 1

echo Clean SteamVR listener rebuild and package completed.
exit /b 0
