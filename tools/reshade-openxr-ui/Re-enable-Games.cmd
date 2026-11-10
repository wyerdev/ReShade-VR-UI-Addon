@echo off
setlocal EnableExtensions

set "DISABLED_DIRECTORY=%LOCALAPPDATA%\ReShade VR UI Add-on\disabled-games"
set "FOUND=0"
set "FAILED=0"

echo ================================================================
echo  ReShade VR UI Add-on - Turn it on again for all games
echo ================================================================
echo.
rem Empty files belong to games that are running right now and are not switched off.
for %%F in ("%DISABLED_DIRECTORY%\*.txt") do if %%~zF GTR 0 call :turn_on "%%~fF"
if %FOUND%==0 (
	echo The add-on is not switched off for any game. Nothing was changed.
	if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
	exit /b 0
)
if not %FAILED%==0 (
	echo.
	echo ERROR: Some files could not be deleted. Close the games and run this file again.
	if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
	exit /b 1
)
echo.
echo DONE: The add-on starts again with these games the next time you start them.
if not defined RESHADE_OPENXR_UI_NONINTERACTIVE pause
exit /b 0

:turn_on
if %FOUND%==0 echo Switched off for:
set /a FOUND+=1
echo   %~n1
del /F /Q "%~1" >nul 2>&1
if exist "%~1" set /a FAILED+=1
exit /b 0