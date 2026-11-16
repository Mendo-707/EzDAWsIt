@echo off
setlocal
cd /d "%~dp0"
rem Runs EzDAWsIt's self-tests. Results: test-results.txt (exit code 0 = all passed)
rem Usage: test.bat            (pauses at the end)
rem        test.bat nopause    (used by build.bat)

set "RESULTS=%~dp0test-results.txt"
if exist "%RESULTS%" del "%RESULTS%"

if not exist "app\EzDAWsIt.exe" (
    echo app\EzDAWsIt.exe not found - run build.bat first.
    > "%RESULTS%" echo RESULT: NOT RUN - app\EzDAWsIt.exe not found
    if /i not "%~1"=="nopause" pause
    exit /b 1
)

start "" /wait "app\EzDAWsIt.exe" --run-tests "%RESULTS%"
set "CODE=%errorlevel%"

if not exist "%RESULTS%" (
    > "%RESULTS%" echo RESULT: TESTS CRASHED - no results were written. Exit code %CODE%. See app\test-run\Logs
    set "CODE=1"
)

findstr /b "RESULT:" "%RESULTS%"
if /i not "%~1"=="nopause" pause
exit /b %CODE%
