@echo off
rem Replaces the machine-wide ReShade (C:\ProgramData\ReShade\ReShade64.dll) with the test build from .deps\reshade.
rem The original is backed up once to ReShade64.dll.BAK; running this again never overwrites that backup.
rem Asks for administrator approval. Undo with restore_reshade.bat.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\reshade_test_build.ps1" -Action Deploy
exit /b %ERRORLEVEL%
