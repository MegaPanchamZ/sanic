/**
 * main_editor.cpp
 * 
 * Entry point for the Sanic Editor application.
 */

#include "Editor.h"
#include "EditorRenderer.h"
#include "imgui/ImGuiBackend.h"
#include "EditorLayout.h"
#include "core/Selection.h"
#include "core/UndoSystem.h"
#include "core/Commands.h"
#include "core/Shortcuts.h"
#include "viewport/Viewport.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/AssetBrowser.h"
#include "panels/ConsolePanel.h"
#include "panels/Menubar.h"
#include "panels/Toolbar.h"

#include "../engine/VulkanContext.h"
#include "../engine/Renderer.h"
#include "../engine/ECS.h"
#include "../engine/Input.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>
#include <chrono>

using namespace Sanic;
using namespace Sanic::Editor;

// Global editor instance
static std::unique_ptr<Editor> g_editor;
static std::unique_ptr<ImGuiBackend> g_imguiBackend;
static std::unique_ptr<EditorRenderer> g_editorRenderer;

// Window and Vulkan context
static GLFWwindow* g_window = nullptr;
static std::unique_ptr<VulkanContext> g_vulkanContext;
static std::unique_ptr<Renderer> g_renderer;
static std::unique_ptr<ECSManager> g_ecsManager;

// Frame timing
static std::chrono::high_resolution_clock::time_point g_lastFrameTime;
static float g_deltaTime = 0.0f;

bool initWindow(int width, int height, const char* title) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }
    
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    
    g_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!g_window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return false;
    }
    
    // Center window
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    int windowX = (mode->width - width) / 2;
    int windowY = (mode->height - height) / 2;
    glfwSetWindowPos(g_window, windowX, windowY);
    
    return true;
}

bool initVulkan() {
    g_vulkanContext = std::make_unique<VulkanContext>();
    
    // Get required extensions from GLFW
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    
    VulkanContext::InitInfo initInfo;
    initInfo.applicationName = "Sanic Editor";
    initInfo.enableValidation = true;
    
    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
        initInfo.instanceExtensions.push_back(glfwExtensions[i]);
    }
    
    if (!g_vulkanContext->initialize(initInfo)) {
        std::cerr << "Failed to initialize Vulkan context" << std::endl;
        return false;
    }
    
    // Create surface
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(g_vulkanContext->getInstance(), g_window, nullptr, &surface) != VK_SUCCESS) {
        std::cerr << "Failed to create window surface" << std::endl;
        return false;
    }
    
    g_vulkanContext->setSurface(surface);
    g_vulkanContext->createSwapchain(1920, 1080);
    
    return true;
}

bool initRenderer() {
    g_renderer = std::make_unique<Renderer>();
    
    Renderer::InitInfo renderInfo;
    renderInfo.vulkanContext = g_vulkanContext.get();
    
    if (!g_renderer->initialize(renderInfo)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return false;
    }
    
    return true;
}

bool initECS() {
    g_ecsManager = std::make_unique<ECSManager>();
    g_ecsManager->initialize();
    
    return true;
}

bool initEditor() {
    // Initialize ImGui backend
    g_imguiBackend = std::make_unique<ImGuiBackend>();
    
    ImGuiBackend::InitInfo imguiInfo;
    imguiInfo.window = g_window;
    imguiInfo.instance = g_vulkanContext->getInstance();
    imguiInfo.physicalDevice = g_vulkanContext->getPhysicalDevice();
    imguiInfo.device = g_vulkanContext->getDevice();
    imguiInfo.queueFamily = g_vulkanContext->getGraphicsQueueFamily();
    imguiInfo.queue = g_vulkanContext->getGraphicsQueue();
    imguiInfo.renderPass = g_vulkanContext->getRenderPass();
    imguiInfo.imageCount = g_vulkanContext->getSwapchainImageCount();
    
    if (!g_imguiBackend->initialize(imguiInfo)) {
        std::cerr << "Failed to initialize ImGui backend" << std::endl;
        return false;
    }
    
    // Initialize editor renderer
    g_editorRenderer = std::make_unique<EditorRenderer>();
    
    EditorRenderer::InitInfo editorRendererInfo;
    editorRendererInfo.device = g_vulkanContext->getDevice();
    editorRendererInfo.physicalDevice = g_vulkanContext->getPhysicalDevice();
    editorRendererInfo.commandPool = g_vulkanContext->getCommandPool();
    editorRendererInfo.graphicsQueue = g_vulkanContext->getGraphicsQueue();
    editorRendererInfo.imguiBackend = g_imguiBackend.get();
    
    if (!g_editorRenderer->initialize(editorRendererInfo)) {
        std::cerr << "Failed to initialize editor renderer" << std::endl;
        return false;
    }
    
    // Initialize main editor
    g_editor = std::make_unique<Editor>();
    
    Editor::InitInfo editorInfo;
    editorInfo.window = g_window;
    editorInfo.vulkanContext = g_vulkanContext.get();
    editorInfo.renderer = g_renderer.get();
    editorInfo.ecsManager = g_ecsManager.get();
    
    if (!g_editor->initialize(editorInfo)) {
        std::cerr << "Failed to initialize editor" << std::endl;
        return false;
    }
    
    // Create and register panels
    auto menubar = std::make_unique<Menubar>();
    auto toolbar = std::make_unique<Toolbar>();
    auto viewport = std::make_unique<Viewport>();
    auto hierarchy = std::make_unique<HierarchyPanel>();
    auto inspector = std::make_unique<InspectorPanel>();
    auto assetBrowser = std::make_unique<AssetBrowser>();
    auto console = std::make_unique<ConsolePanel>();
    
    g_editor->registerWindow(std::move(menubar));
    g_editor->registerWindow(std::move(toolbar));
    g_editor->registerWindow(std::move(viewport));
    g_editor->registerWindow(std::move(hierarchy));
    g_editor->registerWindow(std::move(inspector));
    g_editor->registerWindow(std::move(assetBrowser));
    g_editor->registerWindow(std::move(console));
    
    // Setup default layout
    g_editor->getLayout()->setupDefaultLayout(LayoutPreset::Default);
    
    // Log startup message
    ConsolePanel::logInfo("Sanic Editor started", "Editor");
    ConsolePanel::logInfo("Vulkan initialized with ray tracing support", "Renderer");
    
    return true;
}

void shutdown() {
    // Wait for GPU to finish
    if (g_vulkanContext) {
        vkDeviceWaitIdle(g_vulkanContext->getDevice());
    }
    
    g_editor.reset();
    g_editorRenderer.reset();
    g_imguiBackend.reset();
    g_renderer.reset();
    g_ecsManager.reset();
    g_vulkanContext.reset();
    
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    
    glfwTerminate();
}

void mainLoop() {
    g_lastFrameTime = std::chrono::high_resolution_clock::now();
    
    while (!glfwWindowShouldClose(g_window)) {
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        g_deltaTime = std::chrono::duration<float>(currentTime - g_lastFrameTime).count();
        g_lastFrameTime = currentTime;
        
        glfwPollEvents();
        
        // Handle window resize
        int width, height;
        glfwGetFramebufferSize(g_window, &width, &height);
        if (width > 0 && height > 0) {
            // Update swapchain if needed
        }
        
        // Begin ImGui frame
        g_imguiBackend->beginFrame();
        
        // Update and draw editor
        g_editor->update(g_deltaTime);
        g_editor->draw();
        
        // End ImGui frame
        g_imguiBackend->endFrame();
        
        // Render
        if (g_vulkanContext->beginFrame()) {
            VkCommandBuffer cmd = g_vulkanContext->getCurrentCommandBuffer();
            
            // Render viewport scene
            g_editorRenderer->beginViewportRender(cmd);
            // TODO: Actual scene rendering here
            g_editorRenderer->endViewportRender(cmd);
            
            // Begin swapchain render pass
            g_vulkanContext->beginRenderPass(cmd);
            
            // Render ImGui
            g_imguiBackend->render(cmd);
            
            g_vulkanContext->endRenderPass(cmd);
            g_vulkanContext->endFrame();
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Sanic Editor v0.1" << std::endl;
    std::cout << "=================" << std::endl;
    
    // Initialize window
    if (!initWindow(1920, 1080, "Sanic Editor")) {
        return -1;
    }
    
    // Initialize Vulkan
    if (!initVulkan()) {
        shutdown();
        return -1;
    }
    
    // Initialize renderer
    if (!initRenderer()) {
        shutdown();
        return -1;
    }
    
    // Initialize ECS
    if (!initECS()) {
        shutdown();
        return -1;
    }
    
    // Initialize editor
    if (!initEditor()) {
        shutdown();
        return -1;
    }
    
    std::cout << "Editor initialized successfully" << std::endl;
    
    // Run main loop
    mainLoop();
    
    // Cleanup
    shutdown();
    
    std::cout << "Editor shutdown complete" << std::endl;
    
    return 0;
}
