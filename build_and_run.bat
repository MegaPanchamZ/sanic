@echo off
:: build_and_run.bat - Development build script for SanicEngine
:: This script configures, builds, compiles shaders, copies assets, and runs the engine.

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
%GLSLC% %SHADER_SRC%\skybox.vert -o %SHADER_OUT%\skybox.vert.spv
%GLSLC% %SHADER_SRC%\skybox.frag -o %SHADER_OUT%\skybox.frag.spv
%GLSLC% %SHADER_SRC%\shadow.vert -o %SHADER_OUT%\shadow.vert.spv
%GLSLC% %SHADER_SRC%\shadow.frag -o %SHADER_OUT%\shadow.frag.spv

:: Compile ray tracing shaders
set RT_SHADER_SRC=..\shaders
%GLSLC% --target-env=vulkan1.2 %RT_SHADER_SRC%\simple.rgen -o %SHADER_OUT%\simple.rgen.spv
%GLSLC% --target-env=vulkan1.2 %RT_SHADER_SRC%\simple.rmiss -o %SHADER_OUT%\simple.rmiss.spv
%GLSLC% --target-env=vulkan1.2 %RT_SHADER_SRC%\shadow.rmiss -o %SHADER_OUT%\shadow.rmiss.spv
%GLSLC% --target-env=vulkan1.2 %RT_SHADER_SRC%\simple.rchit -o %SHADER_OUT%\simple.rchit.spv

:: Compile Nanite/VisBuffer shaders
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\nanite.task -o %SHADER_OUT%\nanite.task.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\nanite.mesh -o %SHADER_OUT%\nanite.mesh.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\visbuffer.task -o %SHADER_OUT%\visbuffer.task.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\visbuffer.mesh -o %SHADER_OUT%\visbuffer.mesh.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\visbuffer.frag -o %SHADER_OUT%\visbuffer.frag.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\visbuffer.comp -o %SHADER_OUT%\visbuffer.comp.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\material_classify.comp -o %SHADER_OUT%\material_classify.comp.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\cull_meshlets.comp -o %SHADER_OUT%\cull_meshlets.comp.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\vsm_marking.comp -o %SHADER_OUT%\vsm_marking.comp.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\vsm_clear.comp -o %SHADER_OUT%\vsm_clear.comp.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\vsm.task -o %SHADER_OUT%\vsm.task.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\vsm.mesh -o %SHADER_OUT%\vsm.mesh.spv
%GLSLC% --target-env=vulkan1.3 %RT_SHADER_SRC%\vsm.frag -o %SHADER_OUT%\vsm.frag.spv

:: Copy assets (e.g., textures) to the build output directory
set ASSETS_SRC=..\assets
set ASSETS_DST=assets
if not exist %ASSETS_DST% (
    mkdir %ASSETS_DST%
)
copy /Y %ASSETS_SRC%\* %ASSETS_DST%\

:: Run the engine executable
SanicEngine.exe