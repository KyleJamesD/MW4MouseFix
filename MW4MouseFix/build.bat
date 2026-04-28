@echo off
echo ============================================
echo  MW4 Mouse Fix - Build Script
echo ============================================
echo.

:: Try to find Visual Studio
set "FOUND_VS=0"

:: VS 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
    goto :build
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
    goto :build
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
    goto :build
)

:: VS 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
    goto :build
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
    goto :build
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
    goto :build
)

if "%FOUND_VS%"=="0" (
    echo ERROR: Could not find Visual Studio installation!
    echo Please install Visual Studio with C++ Desktop Development workload
    echo Or run this from a "x86 Native Tools Command Prompt"
    pause
    exit /b 1
)

:build
echo.
echo Building 32-bit dinput8.dll...
echo.

if not exist "build" mkdir build

cl.exe /nologo /O2 /LD /EHsc /DUNICODE /D_UNICODE ^
    /Fo"build\\" ^
    src\main.cpp ^
    /Fe"build\dinput8.dll" ^
    /link /DEF:src\exports.def ^
    /MACHINE:X86 ^
    user32.lib ole32.lib dxguid.lib dinput8.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED!
    pause
    exit /b 1
)

echo.
echo ============================================
echo  BUILD SUCCESSFUL!
echo ============================================
echo.
echo Output: build\dinput8.dll
echo.
echo To install:
echo   1. Copy build\dinput8.dll to your MW4 game folder
echo   2. Copy mousefix.ini to your MW4 game folder
echo   3. Run the game!
echo.
pause