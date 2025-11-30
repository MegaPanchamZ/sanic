@echo off
REM Build script for Sanic Engine using MSVC
REM This sets up the Visual Studio environment and builds with Ninja

echo Setting up Visual Studio 2022 environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

echo.
echo Configuring CMake with MSVC...
cd /d F:\Dev\meme\sanic

if not exist build_msvc mkdir build_msvc
cd build_msvc

cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo Building with Ninja...
ninja -j2

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Build complete! Executable at: build_msvc\SanicEngine.exe
pause
