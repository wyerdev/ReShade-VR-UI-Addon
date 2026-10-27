@echo off
rem Puts the original machine-wide ReShade back from ReShade64.dll.BAK (made by deploy_reshade.bat).
rem Asks for administrator approval.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\reshade_test_build.ps1" -Action Restore
exit /b %ERRORLEVEL%
