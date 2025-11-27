@echo off
echo === Sanic Engine Shader Compiler ===

REM Try to find glslc in common locations
where glslc >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set GLSLC=glslc
    goto :compile
)

if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    set GLSLC=%VULKAN_SDK%\Bin\glslc.exe
    goto :compile
)

if exist "C:\VulkanSDK" (
    for /d %%i in (C:\VulkanSDK\*) do (
        if exist "%%i\Bin\glslc.exe" (
            set GLSLC=%%i\Bin\glslc.exe
            goto :compile
        )
    )
)

echo ERROR: glslc not found! Please install Vulkan SDK or set VULKAN_SDK environment variable.
exit /b 1

:compile
echo Using: %GLSLC%
echo.

REM Create output directories
if not exist "build\shaders" mkdir build\shaders

REM Main shaders
echo Compiling main vertex shader...
%GLSLC% src/shaders/shader.vert -o src/shaders/vert.spv
%GLSLC% src/shaders/shader.vert -o build/shaders/shader.vert.spv

echo Compiling main fragment shader...
%GLSLC% src/shaders/shader.frag -o src/shaders/frag.spv
%GLSLC% src/shaders/shader.frag -o build/shaders/shader.frag.spv

REM Shadow shaders
echo Compiling shadow vertex shader...
%GLSLC% src/shaders/shadow.vert -o build/shaders/shadow.vert.spv

echo Compiling shadow fragment shader...
%GLSLC% src/shaders/shadow.frag -o build/shaders/shadow.frag.spv

REM Skybox shaders
echo Compiling skybox vertex shader...
%GLSLC% src/shaders/skybox.vert -o build/shaders/skybox.vert.spv

echo Compiling skybox fragment shader...
%GLSLC% src/shaders/skybox.frag -o build/shaders/skybox.frag.spv

echo.
echo === All shaders compiled successfully! ===
echo Output locations:
echo   - src/shaders/*.spv (legacy paths)
echo   - build/shaders/*.spv (build directory)
