@echo off
setlocal EnableExtensions

call "%~dp0tools\reshade-openxr-ui\Uninstall-OpenXR.cmd"
exit /b %ERRORLEVEL%