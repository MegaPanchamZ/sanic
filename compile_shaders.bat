@echo off
set VULKAN_SDK=C:\Users\Debashish\scoop\apps\vulkan\current
%VULKAN_SDK%\Bin\glslc.exe src/shaders/shader.vert -o src/shaders/vert.spv
%VULKAN_SDK%\Bin\glslc.exe src/shaders/shader.frag -o src/shaders/frag.spv
echo Shaders compiled successfully!
