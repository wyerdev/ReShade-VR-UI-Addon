@echo off
setlocal EnableExtensions

rem Builds everything, runs every test, then prints one PASSED/FAILED line per test.
rem Usage: test.bat [path-to-OpenXR-Simulator-test]
rem Without a path it uses RESHADE_VR_UI_TEST_ROOT, .deps\openxr-simulator-test, or an
rem OpenXR-Simulator-test folder next to this repository, in that order.

for /F %%A in ('echo prompt $E^| cmd') do set "ESC=%%A"
set "RED=%ESC%[91m"
set "GREEN=%ESC%[92m"
set "YELLOW=%ESC%[93m"
set "RESET=%ESC%[0m"

set "ROOT=%~dp0"
set "TOOLS=%ROOT%tools\reshade-openxr-ui"
set "RUN_PS=powershell.exe -NoProfile -ExecutionPolicy Bypass -File"

set "TEST_ROOT="
if not "%~1"=="" (
	set "TEST_ROOT=%~f1"
) else if defined RESHADE_VR_UI_TEST_ROOT (
	set "TEST_ROOT=%RESHADE_VR_UI_TEST_ROOT%"
) else if exist "%ROOT%.deps\openxr-simulator-test\openxr_simulator.json" (
	set "TEST_ROOT=%ROOT%.deps\openxr-simulator-test"
) else if exist "%ROOT%..\OpenXR-Simulator-test\openxr_simulator.json" (
	for %%I in ("%ROOT%..\OpenXR-Simulator-test") do set "TEST_ROOT=%%~fI"
)

set "BUILD_RESULT=NOT RUN"
set "SMOKE_RESULT=NOT RUN"
set "CAPTURE_RESULT=NOT RUN"

echo ================================================================
echo  1/3 Build and built-in tests
echo ================================================================
call "%ROOT%build.bat"
if errorlevel 1 (
	set "BUILD_RESULT=FAILED"
	goto :summary
)
set "BUILD_RESULT=PASSED"

if not defined TEST_ROOT goto :no_simulator
if not exist "%TEST_ROOT%\openxr_simulator.json" goto :no_simulator

echo.
echo ================================================================
echo  2/3 Quick headset test (DirectX 11 and 12)
echo ================================================================
%RUN_PS% "%TOOLS%\smoke.ps1" -TestRoot "%TEST_ROOT%"
if errorlevel 1 (set "SMOKE_RESULT=FAILED") else (set "SMOKE_RESULT=PASSED")

echo.
echo ================================================================
echo  3/3 Headset menu screenshot test (DirectX 11 and 12)
echo ================================================================
echo An OpenXR Simulator window opens. Do not click in it or close it.
%RUN_PS% "%TOOLS%\capture_simulator_ui.ps1" -TestRoot "%TEST_ROOT%"
if errorlevel 1 (set "CAPTURE_RESULT=FAILED") else (set "CAPTURE_RESULT=PASSED")
goto :summary

:no_simulator
set "SMOKE_RESULT=NOT RUN (no OpenXR Simulator test folder found)"
set "CAPTURE_RESULT=NOT RUN (no OpenXR Simulator test folder found)"

:summary
echo.
echo ================================================================
echo  RESULTS
echo ================================================================
call :print_result "Build and built-in tests " "%BUILD_RESULT%"
call :print_result "Quick headset test       " "%SMOKE_RESULT%"
call :print_result "Menu screenshot test     " "%CAPTURE_RESULT%"
if "%CAPTURE_RESULT%"=="PASSED" (
	echo.
	echo Screenshots: "%ROOT%build\reshade-openxr-ui\simulator-captures"
)
echo.
if "%BUILD_RESULT%%SMOKE_RESULT%%CAPTURE_RESULT%"=="PASSEDPASSEDPASSED" (
	echo %GREEN%ALL TESTS PASSED%RESET%
	exit /b 0
)
echo %RED%NOT ALL TESTS PASSED. Scroll up to the first error for details.%RESET%
if not defined TEST_ROOT (
	echo %YELLOW%Pass the OpenXR Simulator test folder: test.bat C:\path\to\OpenXR-Simulator-test%RESET%
)
exit /b 1

:print_result
set "RESULT_TEXT=%~2"
set "RESULT_COLOR=%YELLOW%"
if "%RESULT_TEXT%"=="PASSED" set "RESULT_COLOR=%GREEN%"
if "%RESULT_TEXT%"=="FAILED" set "RESULT_COLOR=%RED%"
echo  %~1 %RESULT_COLOR%%RESULT_TEXT%%RESET%
exit /b 0
