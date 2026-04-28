@echo off
pushd "%~dp0"

if not exist "build" mkdir build

set "FOUND_VS=0"

if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
    set "FOUND_VS=1"
)

if "%FOUND_VS%"=="0" (
    echo.
    echo ERROR: Could not find Visual Studio installation.
    echo Please run this script from an x86 Native Tools command prompt.
    goto :end
)

echo.
echo Building MW4MouseFix2Simple (dinput8.dll)...

echo.
cl.exe /nologo /O2 /LD /EHsc ^
    src\main.cpp ^
    /Fo"build\\" ^
    /Fe"build\dinput8.dll" ^
    /link /DEF:src\exports.def ^
    /MACHINE:X86 ^
    user32.lib ole32.lib dinput8.lib dxguid.lib

if errorlevel 1 goto :end

echo.
echo Build complete! Output: build\dinput8.dll

echo.
:end
popd
