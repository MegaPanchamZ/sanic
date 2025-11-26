#!/usr/bin/env bash
# build_and_run.bat - Development build script for SanicEngine
# This script configures, builds, compiles shaders, copies assets, and runs the engine.

:: Ensure we are in the project root directory
cd /d "%~dp0"

:: Set Vulkan SDK path if not already set (adjust if needed)
if not defined VULKAN_SDK (
    set VULKAN_SDK=C:\Users\Debashish\scoop\apps\vulkan\current
)

:: Create and enter build directory
if not exist build (
    mkdir build
)
cd build

:: Run CMake configuration (Ninja generator) and build
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Debug ..
if %errorlevel% neq 0 (
    echo CMake configuration failed.
    exit /b %errorlevel%
)

cmake --build . --config Debug
if %errorlevel% neq 0 (
    echo Build failed.
    exit /b %errorlevel%
)

:: Compile shaders to SPIR-V using glslc (provided by Vulkan SDK)
set GLSLC=%VULKAN_SDK%\Bin\glslc.exe
if not exist "%GLSLC%" (
    echo glslc not found at %GLSLC%. Ensure Vulkan SDK is installed.
    exit /b 1
)

:: Shader source directory
set SHADER_SRC=..\src\shaders
set SHADER_OUT=shaders
if not exist %SHADER_OUT% (
    mkdir %SHADER_OUT%
)

%GLSLC% %SHADER_SRC%\shader.vert -o %SHADER_OUT%\shader.vert.spv
%GLSLC% %SHADER_SRC%\shader.frag -o %SHADER_OUT%\shader.frag.spv

:: Copy assets (e.g., textures) to the build output directory
set ASSETS_SRC=..\assets
set ASSETS_DST=assets
if not exist %ASSETS_DST% (
    mkdir %ASSETS_DST%
)
copy /Y %ASSETS_SRC%\* %ASSETS_DST%\

:: Run the engine executable
SanicEngine.exe