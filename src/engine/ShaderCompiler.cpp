#include "ShaderCompiler.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifndef GLSLC_PATH
#define GLSLC_PATH "glslc"
#endif

ShaderCompiler::ShaderCompiler() {
}

ShaderCompiler::~ShaderCompiler() {
}

std::string ShaderCompiler::getTempFileName(const std::string& suffix) {
    // Simple temp file generation. In production, use more robust methods.
    static int counter = 0;
    return "temp_shader_" + std::to_string(counter++) + suffix;
}

std::vector<uint32_t> ShaderCompiler::compileShader(const std::string& source, ShaderKind kind, const std::string& sourceName) {
    std::string stageFlag;
    switch (kind) {
        case ShaderKind::Vertex: stageFlag = "vertex"; break;
        case ShaderKind::Fragment: stageFlag = "fragment"; break;
        case ShaderKind::Compute: stageFlag = "compute"; break;
        case ShaderKind::Geometry: stageFlag = "geometry"; break;
        case ShaderKind::TessControl: stageFlag = "tesscontrol"; break;
        case ShaderKind::TessEvaluation: stageFlag = "tesseval"; break;
        case ShaderKind::Task: stageFlag = "task"; break;
        case ShaderKind::Mesh: stageFlag = "mesh"; break;
    }

    std::string inFile = getTempFileName(".glsl");
    std::string outFile = getTempFileName(".spv");

    // Write source to temp file
    {
        std::ofstream out(inFile);
        out << source;
    }

    // Construct command
    // glslc -fshader-stage=<stage> -o <outFile> <inFile>
    std::string command = std::string(GLSLC_PATH) + " --target-env=vulkan1.3 -fshader-stage=" + stageFlag + " -o " + outFile + " " + inFile;

    // Execute
    int ret = std::system(command.c_str());

    std::vector<uint32_t> spirv;
    if (ret != 0) {
        std::cerr << "Shader compilation failed for " << sourceName << std::endl;
    } else {
        // Read output
        std::ifstream in(outFile, std::ios::binary | std::ios::ate);
        if (in.is_open()) {
            size_t fileSize = (size_t)in.tellg();
            in.seekg(0, std::ios::beg);
            spirv.resize(fileSize / sizeof(uint32_t));
            in.read((char*)spirv.data(), fileSize);
        } else {
            std::cerr << "Failed to open compiled SPIR-V file: " << outFile << std::endl;
        }
    }

    // Cleanup
    std::remove(inFile.c_str());
    std::remove(outFile.c_str());

    return spirv;
}
