@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

rem ============================================================
rem  EzDAWsIt build script
rem  Downloads JUCE + Tracktion Engine (pinned versions), builds
rem  EzDAWsIt.exe, and puts it in the "app" folder.
rem  Everything is logged to build-log.txt
rem ============================================================

set "LOG=%~dp0build-log.txt"
set "JUCE_SHA=7ff644f65d7daa1428c34b9738ebb5c449dc9172"
set "TE_SHA=a5e44996f5f08dfa6af5f5d02d900fb3cfa37422"

echo ============================================================
echo   Building EzDAWsIt
echo   First build takes 15-30 minutes. Later builds are faster.
echo   Details are written to build-log.txt
echo ============================================================
echo.
> "%LOG%" echo === EzDAWsIt build started %date% %time% ===

rem ---------- EzDAWsIt must be closed (Windows can't replace a running .exe) ----------
:checkRunning
tasklist /FI "IMAGENAME eq EzDAWsIt.exe" 2>nul | find /I "EzDAWsIt.exe" >nul
if errorlevel 1 goto :notRunning
echo EzDAWsIt is still open. Please close it, then press any key to continue.
pause >nul
goto :checkRunning
:notRunning

rem ---------- Find Visual Studio's CMake ----------
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novswhere
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
:novswhere
if not defined VSDIR set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    echo ERROR: CMake from Visual Studio was not found. >> "%LOG%"
    echo ERROR: CMake from Visual Studio was not found.
    goto :fail
)
>> "%LOG%" echo Using CMake: %CMAKE%

if not exist "deps" mkdir "deps"

rem ---------- Download JUCE and Tracktion Engine ----------
echo [1/6] Downloading JUCE...
call :fetch "deps\JUCE" "https://github.com/juce-framework/JUCE.git" %JUCE_SHA%
if errorlevel 1 goto :fail

echo [2/6] Downloading Tracktion Engine...
call :fetch "deps\tracktion_engine" "https://github.com/Tracktion/tracktion_engine.git" %TE_SHA%
if errorlevel 1 goto :fail

rem ---------- ASIO SDK (optional, for low-latency audio) ----------
set "ASIO_DIR="
set "ASIO_H="
if exist "deps\asiosdk" goto :findasio
echo       Downloading Steinberg ASIO SDK (optional)...
>> "%LOG%" echo --- ASIO SDK download ---
curl -L --fail --silent --show-error -o "deps\asiosdk.zip" "https://www.steinberg.net/asiosdk" >> "%LOG%" 2>&1
if errorlevel 1 goto :noasio
powershell -NoProfile -Command "Expand-Archive -Force 'deps\asiosdk.zip' 'deps\asiosdk'" >> "%LOG%" 2>&1
:findasio
for /f "delims=" %%f in ('dir /s /b "deps\asiosdk\iasiodrv.h" 2^>nul') do set "ASIO_H=%%f"
if not defined ASIO_H goto :noasio
for %%d in ("!ASIO_H!\..\..") do set "ASIO_DIR=%%~fd"
echo       ASIO SDK found - low-latency ASIO drivers enabled.
>> "%LOG%" echo ASIO SDK: !ASIO_DIR!
goto :asiodone
:noasio
echo       ASIO SDK not available - building with Windows audio (WASAPI) only.
>> "%LOG%" echo ASIO SDK not available
:asiodone

rem ---------- Configure ----------
echo [3/6] Preparing the build...
>> "%LOG%" echo --- CMake configure ---
"%CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64 "-DASIO_SDK_DIR=!ASIO_DIR!" >> "%LOG%" 2>&1
if errorlevel 1 goto :fail

rem ---------- Build ----------
echo [4/6] Compiling EzDAWsIt (this is the long part)...
>> "%LOG%" echo --- Build ---
"%CMAKE%" --build build --config Release --parallel -- /nologo /v:minimal /clp:ErrorsOnly >> "%LOG%" 2>&1
if not errorlevel 1 goto :built
rem A file can be locked for a moment (antivirus scanning the new .exe): wait and try once more
>> "%LOG%" echo --- Build step failed; retrying once in 5 seconds (a file may have been locked briefly) ---
echo Build step failed - retrying once in 5 seconds (often just a briefly locked file)...
timeout /t 5 /nobreak >nul
"%CMAKE%" --build build --config Release -- /nologo /v:minimal /clp:ErrorsOnly >> "%LOG%" 2>&1
if errorlevel 1 goto :fail
:built

if not exist "app" mkdir "app"
copy /y "build\EzDAWsIt_artefacts\Release\EzDAWsIt.exe" "app\EzDAWsIt.exe" >> "%LOG%" 2>&1
if errorlevel 1 (
    >> "%LOG%" echo Copy to app failed; retrying in 5 seconds
    timeout /t 5 /nobreak >nul
    copy /y "build\EzDAWsIt_artefacts\Release\EzDAWsIt.exe" "app\EzDAWsIt.exe" >> "%LOG%" 2>&1
)
if errorlevel 1 (
    >> "%LOG%" echo Could not replace app\EzDAWsIt.exe - is EzDAWsIt open?
    echo Could not replace app\EzDAWsIt.exe - close EzDAWsIt and run build.bat again.
    goto :fail
)
if exist "build\EzDAWsIt_artefacts\Release\EzDAWsIt.pdb" copy /y "build\EzDAWsIt_artefacts\Release\EzDAWsIt.pdb" "app\EzDAWsIt.pdb" >> "%LOG%" 2>&1
rem The licences sit next to the exe, where Help > About finds them
copy /y "LICENSE.txt" "app\LICENSE.txt" >> "%LOG%" 2>&1
copy /y "installer\THIRD-PARTY-NOTICES.txt" "app\THIRD-PARTY-NOTICES.txt" >> "%LOG%" 2>&1
>> "%LOG%" echo === BUILD SUCCEEDED %date% %time% ===

rem ---------- Self-tests (never touch your real songs) ----------
echo [5/6] Running self-tests...
call "%~dp0test.bat" nopause
if errorlevel 1 goto :testfail

rem ---------- Installer + source package (only from a build that passed its tests) ----------
echo [6/6] Making the installer...
call :installer
if errorlevel 1 (set "INSTALLER_MSG=The installer was NOT made - see build-log.txt.") else (set "INSTALLER_MSG=The installer is in the dist folder.")

echo.
echo ============================================================
echo   SUCCESS! Built and all self-tests passed.
echo   EzDAWsIt.exe is in the "app" folder.
echo   !INSTALLER_MSG!
echo ============================================================
echo Press any key to start EzDAWsIt...
pause >nul
start "" "app\EzDAWsIt.exe"
exit /b 0

:testfail
echo.
echo ============================================================
echo   Built, but SELF-TESTS FAILED. Tell Claude - it will read
echo   test-results.txt and build-log.txt
echo ============================================================
pause
exit /b 1

:fail
>> "%LOG%" echo === BUILD FAILED %date% %time% ===
echo.
echo ============================================================
echo   Build failed. Tell Claude - it will read build-log.txt
echo ============================================================
pause
exit /b 1

rem ---------- installer: dist\EzDAWsIt-Setup-<version>.exe and dist\EzDAWsIt-<version>-source.zip ----------
:installer
>> "%LOG%" echo --- Installer ---
set "VERSION="
for /f "tokens=4 delims=() " %%v in ('findstr /b /c:"project(EzDAWsIt VERSION" CMakeLists.txt') do set "VERSION=%%v"
if not defined VERSION (
    >> "%LOG%" echo Could not read the version from CMakeLists.txt
    exit /b 1
)
>> "%LOG%" echo Version !VERSION!
call :findiscc
if not defined ISCC (
    echo       Inno Setup is not installed - installing it with winget...
    >> "%LOG%" echo Inno Setup not found; trying winget
    winget install --id JRSoftware.InnoSetup -e --silent --accept-package-agreements --accept-source-agreements >> "%LOG%" 2>&1
    call :findiscc
)
if not defined ISCC (
    >> "%LOG%" echo Inno Setup 6 is needed for the installer: https://jrsoftware.org/isdl.php
    echo       Inno Setup 6 is needed to make the installer: https://jrsoftware.org/isdl.php
    exit /b 1
)
if not exist "dist" mkdir "dist"
"!ISCC!" /Q "/DAppVersion=!VERSION!" "installer\EzDAWsIt.iss" >> "%LOG%" 2>&1
if errorlevel 1 (
    >> "%LOG%" echo Inno Setup failed
    exit /b 1
)
rem The GPL source that goes with the installer (JUCE and Tracktion are fetched by build.bat at pinned commits)
if exist "dist\EzDAWsIt-!VERSION!-source.zip" del "dist\EzDAWsIt-!VERSION!-source.zip"
powershell -NoProfile -Command "Compress-Archive -Path 'Source','Resources','installer','tools','docs','CMakeLists.txt','build.bat','test.bat','LICENSE.txt','README.md','AGENTS.md' -DestinationPath 'dist\EzDAWsIt-!VERSION!-source.zip'" >> "%LOG%" 2>&1
if errorlevel 1 (
    >> "%LOG%" echo Could not make the source package
    exit /b 1
)
>> "%LOG%" echo === INSTALLER MADE: dist\EzDAWsIt-Setup-!VERSION!.exe ===
echo       dist\EzDAWsIt-Setup-!VERSION!.exe
exit /b 0

:findiscc
set "ISCC="
for %%p in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe") do (
    if not defined ISCC if exist "%%~p" set "ISCC=%%~p"
)
exit /b 0

rem ---------- fetch <folder> <url> <commit> ----------
:fetch
set "DIR=%~1"
if exist "%DIR%\.ezdawsit-%~3" (
    echo       already downloaded.
    exit /b 0
)
if exist "%DIR%" rmdir /s /q "%DIR%"
>> "%LOG%" echo --- Fetching %~2 at %~3 ---
git init -q "%DIR%" >> "%LOG%" 2>&1 || exit /b 1
git -C "%DIR%" remote add origin %~2 >> "%LOG%" 2>&1 || exit /b 1
git -C "%DIR%" fetch --depth 1 origin %~3 >> "%LOG%" 2>&1 || exit /b 1
git -C "%DIR%" checkout -q FETCH_HEAD >> "%LOG%" 2>&1 || exit /b 1
type nul > "%DIR%\.ezdawsit-%~3"
exit /b 0
