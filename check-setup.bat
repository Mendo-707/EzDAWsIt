@echo off
setlocal
rem DAW setup check - writes setup-report.txt next to this file. Changes nothing on your PC.
set "OUT=%~dp0setup-report.txt"
set "VSCMAKE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

echo Checking your PC for the DAW build tools...
> "%OUT%" echo === DAW setup report %date% %time% ===

>> "%OUT%" echo.
>> "%OUT%" echo [Visual Studio with C++]
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName >> "%OUT%" 2>&1
"%VSWHERE%" -latest -products * -property catalog_productDisplayVersion >> "%OUT%" 2>&1

>> "%OUT%" echo.
>> "%OUT%" echo [CMake bundled with Visual Studio]
"%VSCMAKE%" --version >> "%OUT%" 2>&1

>> "%OUT%" echo.
>> "%OUT%" echo [CMake on PATH]
cmake --version >> "%OUT%" 2>&1

>> "%OUT%" echo.
>> "%OUT%" echo [Git]
git --version >> "%OUT%" 2>&1

>> "%OUT%" echo.
>> "%OUT%" echo [Node.js and npm]
node --version >> "%OUT%" 2>&1
call npm --version >> "%OUT%" 2>&1

>> "%OUT%" echo.
>> "%OUT%" echo [Can reach GitHub to download JUCE and Tracktion Engine]
git ls-remote https://github.com/juce-framework/JUCE HEAD >> "%OUT%" 2>&1
git ls-remote https://github.com/Tracktion/tracktion_engine HEAD >> "%OUT%" 2>&1

>> "%OUT%" echo.
>> "%OUT%" echo [Free space on C: in GB]
powershell -NoProfile -Command "[math]::Round((Get-PSDrive C).Free/1GB,1)" >> "%OUT%" 2>&1

>> "%OUT%" echo.
>> "%OUT%" echo [Audio drivers: ASIO entries in registry]
reg query "HKLM\SOFTWARE\ASIO" >> "%OUT%" 2>&1

>> "%OUT%" echo.
>> "%OUT%" echo === end ===
echo.
echo Done. Results saved to setup-report.txt. You can close this window.
pause
