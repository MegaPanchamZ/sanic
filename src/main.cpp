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
        
        // Assuming running from build directory, source is in ../shaders
        // But we need to handle src/shaders for gbuffer.frag
        auto taskSource = readShaderSource("../shaders/nanite.task");
        auto meshSource = readShaderSource("../shaders/nanite.mesh");
        auto gbufferFragSource = readShaderSource("../src/shaders/gbuffer.frag");
        auto compositionFragSource = readShaderSource("../src/shaders/composition.frag");
        
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
        Renderer renderer(window, physicsSystem);
        
        Input& input = Input::getInstance();
        input.init(window.getHandle());
        
        auto lastTime = std::chrono::high_resolution_clock::now();
        
        while (!window.shouldClose()) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            input.update();
            window.pollEvents();
            
            // Physics update - now with proper delta time clamping
            physicsSystem.update(deltaTime);
            
            renderer.update(deltaTime); // Syncs physics transforms to game objects
            renderer.processInput(deltaTime);
            renderer.drawFrame();
        }
        
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
