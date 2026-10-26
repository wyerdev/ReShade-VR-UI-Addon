@echo off
setlocal
rem Starts BioShock Remastered VR on the 32-bit OpenXR Simulator with this repository's built 32-bit add-on and ReShade32.
rem Nothing is written to the game folder or the registry; the settings reach the game process only.
rem Runs skip the intro videos and continue the last save. Keep BioShock's window in front: it pauses in the background.
rem   test_bioshock_vr.bat              run, take a headset capture in the game world, game stays open
rem   test_bioshock_vr.bat capture      run, take a headset capture in the game world, close the game
rem   test_bioshock_vr.bat noreshade    run with the add-on only (no ReShade), game stays open
rem   test_bioshock_vr.bat headset      run on your real headset instead of the simulator, game stays open, you play
rem   test_bioshock_vr.bat stop         close the game and save the logs
rem   test_bioshock_vr.bat status       show what is installed and running, start nothing

set "ARGS=-Action Run -EnterGame -KeepRunning"
if /I "%~1"=="capture" set "ARGS=-Action Run -EnterGame"
if /I "%~1"=="noreshade" set "ARGS=-Action Run -EnterGame -WithoutReShade -KeepRunning"
if /I "%~1"=="headset" set "ARGS=-Action Run -UseActiveRuntime -KeepRunning"
if /I "%~1"=="stop" set "ARGS=-Action Stop"
if /I "%~1"=="status" set "ARGS=-Action Status"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\test_bioshock_vr.ps1" %ARGS%
set "RESULT=%ERRORLEVEL%"
endlocal & set "RESULT=%RESULT%"
pause
exit /b %RESULT%
