@echo off
setlocal EnableExtensions

set "INSTALL_SCRIPT=%~dp0build\reshade-openxr-ui-release\ReShade-VR-UI-Addon-OpenXR\Install-OpenXR.cmd"
if not exist "%INSTALL_SCRIPT%" (
    echo Packaged installer not found.
    echo Run build.bat first.
    exit /b 1
)

call "%INSTALL_SCRIPT%"
exit /b %ERRORLEVEL%