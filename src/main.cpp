#include <iostream>
#include "engine/Window.h"
#include "engine/Renderer.h"
#include "engine/Renderer.h"
#include "engine/Input.h"
#include "engine/PhysicsSystem.h"
#include <chrono>

#include "engine/ShaderCompiler.h"
#include <fstream>
#include <vector>
#include <filesystem>

void writeShader(const std::string& filename, const std::vector<uint32_t>& spirv) {
    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t));
    file.close();
}

std::string readShaderSource(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader source: " + filename);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

int main() {
    try {
        // Compile Nanite Shaders
        ShaderCompiler compiler;
        std::cout << "Compiling Nanite shaders..." << std::endl;
        
        // All shaders are now in the unified ../shaders directory
        auto taskSource = readShaderSource("../shaders/nanite.task");
        auto meshSource = readShaderSource("../shaders/nanite.mesh");
        auto gbufferFragSource = readShaderSource("../shaders/gbuffer.frag");
        auto compositionFragSource = readShaderSource("../shaders/composition.frag");
        
        auto taskSpirv = compiler.compileShader(taskSource, ShaderKind::Task, "nanite.task");
        auto meshSpirv = compiler.compileShader(meshSource, ShaderKind::Mesh, "nanite.mesh");
        auto gbufferFragSpirv = compiler.compileShader(gbufferFragSource, ShaderKind::Fragment, "gbuffer.frag");
        auto compositionFragSpirv = compiler.compileShader(compositionFragSource, ShaderKind::Fragment, "composition.frag");
        
        if (taskSpirv.empty() || meshSpirv.empty() || gbufferFragSpirv.empty() || compositionFragSpirv.empty()) {
            std::cerr << "Failed to compile shaders" << std::endl;
            return -1;
        }

        // Ensure shaders directory exists in build
        std::filesystem::create_directories("shaders");
        
        writeShader("shaders/nanite.task.spv", taskSpirv);
        writeShader("shaders/nanite.mesh.spv", meshSpirv);
        writeShader("shaders/gbuffer.frag.spv", gbufferFragSpirv);
        writeShader("shaders/composition.frag.spv", compositionFragSpirv);
        std::cout << "Nanite shaders compiled successfully." << std::endl;

        // Compile Ray Tracing Shaders
        std::cout << "Compiling Ray Tracing shaders..." << std::endl;
        auto rgenSource = readShaderSource("../shaders/simple.rgen");
        auto rmissSource = readShaderSource("../shaders/simple.rmiss");
        auto rchitSource = readShaderSource("../shaders/simple.rchit");

        auto rgenSpirv = compiler.compileShader(rgenSource, ShaderKind::RayGen, "simple.rgen");
        auto rmissSpirv = compiler.compileShader(rmissSource, ShaderKind::Miss, "simple.rmiss");
        auto rchitSpirv = compiler.compileShader(rchitSource, ShaderKind::ClosestHit, "simple.rchit");

        if (rgenSpirv.empty() || rmissSpirv.empty() || rchitSpirv.empty()) {
            std::cerr << "Failed to compile RT shaders" << std::endl;
            return -1;
        }

        writeShader("shaders/simple.rgen.spv", rgenSpirv);
        writeShader("shaders/simple.rmiss.spv", rmissSpirv);
        writeShader("shaders/simple.rchit.spv", rchitSpirv);
        std::cout << "Ray Tracing shaders compiled successfully." << std::endl;

        Window window(800, 600, "Sanic Engine");
        PhysicsSystem physicsSystem;
        std::cout << "Physics system created" << std::endl;
        Renderer renderer(window, physicsSystem);
        std::cout << "Renderer created" << std::endl;
        
        Input& input = Input::getInstance();
        input.init(window.getHandle());
        
        std::cout << "Starting main loop..." << std::endl;
        std::cout.flush();
        
        auto lastTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;
        
        while (!window.shouldClose()) {
            if (frameCount < 5) { std::cout << "Frame " << frameCount << " starting..." << std::endl; std::cout.flush(); }
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            if (frameCount < 5) { std::cout << "  deltaTime=" << deltaTime << std::endl; std::cout.flush(); }
            
            input.update();
            window.pollEvents();
            
            // Physics update - re-enabled with MSVC build (MinGW had Jolt SIMD issues)
            physicsSystem.update(deltaTime);
            if (frameCount < 5) { std::cout << "  physics updated" << std::endl; std::cout.flush(); }
            
            if (frameCount == 0) { std::cout << "renderer.update()..." << std::endl; std::cout.flush(); }
            renderer.update(deltaTime); // Syncs physics transforms to game objects
            if (frameCount == 0) { std::cout << "renderer.processInput()..." << std::endl; std::cout.flush(); }
            renderer.processInput(deltaTime);
            if (frameCount == 0) { std::cout << "renderer.drawFrame()..." << std::endl; std::cout.flush(); }
            renderer.drawFrame();
            if (frameCount == 0) { std::cout << "Frame complete!" << std::endl; std::cout.flush(); }
            
            frameCount++;
            if (frameCount % 1000 == 0) {
                std::cout << "Frames: " << frameCount << std::endl;
            }
        }
        
        std::cout << "Exited main loop after " << frameCount << " frames" << std::endl;
        
        renderer.waitIdle();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr.flush();
        std::cout.flush();
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!" << std::endl;
        std::cerr.flush();
        return 1;
    }
    
    return 0;
}
