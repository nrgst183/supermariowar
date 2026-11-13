@echo off
rem ---------------------------------------------------------------------------
rem Generates a Visual Studio solution for Super Mario War in build\vs and
rem opens it.
rem
rem  - Uses the newest installed Visual Studio (VS 2022 -> smw.sln,
rem    VS 2026 -> smw.slnx). To pick one, pass a generator, for example:
rem        generate-vs-solution.bat -G "Visual Studio 17 2022"
rem  - Any other arguments are passed to CMake as well, for example:
rem        generate-vs-solution.bat -DNO_NETWORK=ON
rem  - SDL3, SDL3_image, SDL3_mixer, enet, toml11 and zlib are downloaded and
rem    built automatically on first configure (needs git and internet access).
rem    To use prebuilt libraries instead, pass -DCMAKE_PREFIX_PATH=<dir>.
rem  - Set SMW_NO_OPEN=1 to only generate the solution without opening it.
rem
rem The solution is generated from CMake (the CMakeLists.txt files are the
rem source of truth), so re-run this script after adding or removing files.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
cd /d "%~dp0"

rem --- Find CMake: PATH first, then the copy bundled with Visual Studio ------
set "CMAKE_EXE=cmake"
where cmake >nul 2>nul && goto :have_cmake

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_cmake
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :no_cmake
set "CMAKE_EXE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" goto :no_cmake

:have_cmake
echo Using CMake: %CMAKE_EXE%
"%CMAKE_EXE%" --version | findstr /b /c:"cmake version"

rem --- Game data (git submodule) ---------------------------------------------
if exist "data\gfx" goto :have_data
where git >nul 2>nul || goto :no_data
echo Fetching the game data submodule...
git submodule update --init --recursive || goto :no_data
:have_data

rem --- Configure ---------------------------------------------------------------
"%CMAKE_EXE%" -S . -B build\vs -A x64 %*
if errorlevel 1 goto :failed

echo.
if exist "build\vs\smw.slnx" (set "SOLUTION=build\vs\smw.slnx") else (set "SOLUTION=build\vs\smw.sln")
echo Solution generated: %SOLUTION%
echo Build the "smw" project (it is the startup project) and press F5 to play.

if defined CI goto :done
if "%SMW_NO_OPEN%"=="1" goto :done
start "" "%SOLUTION%"
goto :done

:no_cmake
echo ERROR: CMake was not found.
echo Install CMake 3.25 or newer (4.2 or newer for Visual Studio 2026) from
echo https://cmake.org/download/ or install the "C++ CMake tools for Windows"
echo component of Visual Studio, then run this script again.
exit /b 1

:no_data
echo ERROR: The game data folder is missing. Run this in a git shell:
echo     git submodule update --init --recursive
exit /b 1

:failed
echo.
echo ERROR: CMake configuration failed, see the messages above.
echo If no Visual Studio with the "Desktop development with C++" workload is
echo installed, CMake cannot create a Visual Studio solution.
exit /b 1

:done
endlocal
