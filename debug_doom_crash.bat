@echo off
rem Waits for DOOMx64vk.exe, attaches a debugger and logs the crash to build\doom-dumps.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\watch_doom_crash.ps1"
pause
