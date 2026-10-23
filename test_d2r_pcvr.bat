@echo off
setlocal
rem Starts Diablo II: Resurrected with the D2R-PCVR mod and this repository's built add-on.
rem   test_d2r_pcvr.bat              run on the headset, game stays open
rem   test_d2r_pcvr.bat simulator    run on the OpenXR Simulator, game stays open
rem   test_d2r_pcvr.bat noaddon      run on the headset without the add-on (comparison run)
rem   test_d2r_pcvr.bat stop         close the game
rem   test_d2r_pcvr.bat status       show whether the game is running, start nothing

set "ARGS=-Action Run -UseActiveRuntime -KeepRunning"
if /I "%~1"=="simulator" set "ARGS=-Action Run -KeepRunning"
if /I "%~1"=="noaddon" set "ARGS=-Action Run -UseActiveRuntime -WithoutAddon -KeepRunning"
if /I "%~1"=="stop" set "ARGS=-Action Stop"
if /I "%~1"=="status" set "ARGS=-Action Status"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\test_d2r_pcvr.ps1" %ARGS%
set "RESULT=%ERRORLEVEL%"
endlocal & set "RESULT=%RESULT%"
pause
exit /b %RESULT%
