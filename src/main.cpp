#include <iostream>
#include "engine/Window.h"
#include "engine/Renderer.h"
#include "engine/Input.h"
#include "engine/PhysicsSystem.h"
#include "editor/Editor.h"
#include <chrono>

#include "engine/ShaderCompiler.h"
#include <fstream>
#include <vector>
#include <filesystem>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

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

        Window window(1600, 900, "Sanic Engine - Editor");
        PhysicsSystem physicsSystem;
        std::cout << "Physics system created" << std::endl;
        Renderer renderer(window, physicsSystem);
        std::cout << "Renderer created" << std::endl;
        
        // Initialize ImGui context first
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        
        // Initialize ImGui GLFW backend
        ImGui_ImplGlfw_InitForVulkan(window.getHandle(), true);
        
        // Create and initialize the Editor
        Sanic::Editor::Editor editor;
        if (!editor.initialize(&renderer.getVulkanContext(), nullptr)) {
            std::cerr << "Failed to initialize editor!" << std::endl;
            return -1;
        }
        
        // Initialize ImGui Vulkan backend (skips context creation since we did it above)
        if (!editor.initializeImGui(renderer.getRenderPass(), renderer.getSwapchainImageCount())) {
            std::cerr << "Failed to initialize ImGui!" << std::endl;
            return -1;
        }
        
        std::cout << "Editor initialized" << std::endl;
        
        Input& input = Input::getInstance();
        input.init(window.getHandle());
        
        std::cout << "Starting main loop..." << std::endl;
        
        auto lastTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;
        
        while (!window.shouldClose()) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            input.update();
            window.pollEvents();
            
            // Start ImGui frame
            editor.beginFrame();
            
            // Physics update - only when editor is in Play mode
            if (editor.isPlaying()) {
                physicsSystem.update(deltaTime);
            }
            
            renderer.update(deltaTime);
            
            // Update editor UI
            editor.update(deltaTime);
            
            // Finalize ImGui frame - must be called before renderer tries to get draw data
            ImGui::Render();
            
            // Only process camera input when not interacting with ImGui
            if (!ImGui::GetIO().WantCaptureMouse && !ImGui::GetIO().WantCaptureKeyboard) {
                renderer.processInput(deltaTime);
            }
            
            renderer.drawFrame();
            
            // End ImGui frame (handles multi-viewport)
            editor.endFrame();
            
            frameCount++;
        }
        
        std::cout << "Exited main loop after " << frameCount << " frames" << std::endl;
        
        // Shutdown editor
        editor.shutdownImGui();
        editor.shutdown();
        
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
