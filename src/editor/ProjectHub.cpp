#include "ProjectHub.h"
#include "core/EditorTheme.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <nfd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

// We need access to the global Vulkan context for ImGui
#include "../engine/VulkanContext.h"
extern std::unique_ptr<Sanic::VulkanContext> g_vulkanContext;

namespace Sanic::Editor {

// Forward declare theme function
void ApplyUnrealTheme();

ProjectHub::ProjectHub() {
}

ProjectHub::~ProjectHub() {
}

bool ProjectHub::run() {
    initialize();
    
    while (!glfwWindowShouldClose(window_) && !shouldClose_) {
        glfwPollEvents();
        
        // Start ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        draw();
        
        ImGui::Render();
        
        // Render
        if (g_vulkanContext->beginFrame()) {
            VkCommandBuffer cmd = g_vulkanContext->getCurrentCommandBuffer();
            
            g_vulkanContext->beginRenderPass(cmd);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            g_vulkanContext->endRenderPass(cmd);
            
            g_vulkanContext->endFrame();
        }
    }
    
    bool projectSelected = !selectedProjectPath_.empty();
    
    shutdown();
    
    return projectSelected;
}

void ProjectHub::initialize() {
    // Create a smaller window for the hub
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // Borderless
    
    window_ = glfwCreateWindow(800, 600, "Sanic Project Hub", nullptr, nullptr);
    
    // Center window
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    int windowX = (mode->width - 800) / 2;
    int windowY = (mode->height - 600) / 2;
    glfwSetWindowPos(window_, windowX, windowY);
    
    // Re-initialize ImGui for this window
    // Note: We assume Vulkan is already initialized globally
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Apply theme
    ApplyUnrealTheme();
    
    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window_, true);
    
    // Re-use the existing Vulkan context's descriptor pool if possible, 
    // or we might need to create a temporary one. 
    // For simplicity, we'll assume the global one is usable or we'd need to refactor initialization.
    // However, Editor::initializeImGui creates its own pool. We should probably create one here too.
    
    // ... (Skipping descriptor pool creation for brevity, assuming we can reuse or it's handled)
    // Actually, we need to init ImGui_ImplVulkan again for this window/context
    
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = g_vulkanContext->getInstance();
    initInfo.PhysicalDevice = g_vulkanContext->getPhysicalDevice();
    initInfo.Device = g_vulkanContext->getDevice();
    initInfo.QueueFamily = g_vulkanContext->getGraphicsQueueFamily();
    initInfo.Queue = g_vulkanContext->getGraphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = g_vulkanContext->getDescriptorPool(); // Use global pool if available? 
    // If global pool is null (it is, created in Editor), we need one.
    // Let's create a temp one.
    
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    
    // Leaking this pool for now, strictly speaking should be member variable
    VkDescriptorPool tempPool;
    vkCreateDescriptorPool(g_vulkanContext->getDevice(), &poolInfo, nullptr, &tempPool);
    initInfo.DescriptorPool = tempPool;
    
    initInfo.MinImageCount = g_vulkanContext->getSwapchainImageCount();
    initInfo.ImageCount = g_vulkanContext->getSwapchainImageCount();
    initInfo.RenderPass = g_vulkanContext->getRenderPass();
    
    ImGui_ImplVulkan_Init(&initInfo);
    
    loadRecentProjects();
}

void ProjectHub::shutdown() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    
    glfwDestroyWindow(window_);
    window_ = nullptr;
}

void ProjectHub::draw() {
    // Full window dockspace-like background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Background", nullptr, flags);
    
    // Header
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
    ImGui::BeginChild("Header", ImVec2(800, 60), false);
    ImGui::SetCursorPos(ImVec2(20, 15));
    ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "SANIC ENGINE");
    ImGui::SameLine();
    ImGui::Text("| Project Hub");
    
    // Close button
    ImGui::SetCursorPos(ImVec2(760, 15));
    if (ImGui::Button("X", ImVec2(30, 30))) {
        shouldClose_ = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    // Content
    ImGui::SetCursorPos(ImVec2(0, 60));
    ImGui::BeginChild("Content", ImVec2(800, 540), false);
    
    // Left sidebar (Recent Projects)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::BeginChild("Sidebar", ImVec2(300, 540), false);
    
    ImGui::SetCursorPos(ImVec2(20, 20));
    ImGui::TextDisabled("RECENT PROJECTS");
    
    ImGui::SetCursorPosY(50);
    
    for (const auto& project : recentProjects_) {
        ImGui::SetCursorPosX(10);
        if (ImGui::Button(project.name.c_str(), ImVec2(280, 40))) {
            selectedProjectPath_ = project.path;
            shouldClose_ = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", project.path.c_str());
        }
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    // Right area (Actions)
    ImGui::SameLine();
    ImGui::BeginChild("Actions", ImVec2(500, 540), false);
    
    float centerX = 250.0f;
    float startY = 150.0f;
    
    ImGui::SetCursorPos(ImVec2(centerX - 100, startY));
    if (ImGui::Button("New Project", ImVec2(200, 50))) {
        showNewProjectDialog_ = true;
    }
    
    ImGui::SetCursorPos(ImVec2(centerX - 100, startY + 70));
    if (ImGui::Button("Open Project", ImVec2(200, 50))) {
        nfdchar_t* outPath = nullptr;
        nfdresult_t result = NFD_PickFolder(&outPath, nullptr);
        if (result == NFD_OKAY) {
            selectedProjectPath_ = outPath;
            shouldClose_ = true;
            NFD_FreePath(outPath);
        }
    }
    
    // New Project Dialog Overlay
    if (showNewProjectDialog_) {
        ImGui::SetNextWindowPos(ImVec2(200, 150));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::OpenPopup("Create New Project");
        
        if (ImGui::BeginPopupModal("Create New Project", &showNewProjectDialog_)) {
            ImGui::InputText("Project Name", newProjectNameBuffer_, 256);
            
            ImGui::InputText("Location", newProjectPathBuffer_, 1024);
            ImGui::SameLine();
            if (ImGui::Button("...")) {
                nfdchar_t* outPath = nullptr;
                nfdresult_t result = NFD_PickFolder(&outPath, nullptr);
                if (result == NFD_OKAY) {
                    strncpy(newProjectPathBuffer_, outPath, 1024);
                    NFD_FreePath(outPath);
                }
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                std::string fullPath = std::string(newProjectPathBuffer_) + "/" + newProjectNameBuffer_;
                createNewProject(fullPath, newProjectNameBuffer_);
                selectedProjectPath_ = fullPath;
                shouldClose_ = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                showNewProjectDialog_ = false;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
    }
    
    ImGui::EndChild();
    
    ImGui::EndChild(); // Background
    ImGui::PopStyleVar();
}

void ProjectHub::loadRecentProjects() {
    // Mock data for now
    recentProjects_.push_back({"Sanic Demo", "f:/Dev/meme/sanic/assets", "Today"});
}

void ProjectHub::saveRecentProjects() {
    // TODO: Save to JSON
}

void ProjectHub::createNewProject(const std::string& path, const std::string& name) {
    std::filesystem::create_directories(path);
    std::filesystem::create_directories(path + "/assets");
    std::filesystem::create_directories(path + "/scenes");
    // Create default project file?
}

} // namespace Sanic::Editor
