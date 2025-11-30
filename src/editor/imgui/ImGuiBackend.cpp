/**
 * ImGuiBackend.cpp
 * 
 * ImGui Vulkan/GLFW backend implementation.
 */

#include "ImGuiBackend.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace Sanic::Editor {

bool ImGuiBackend::initialize(const InitInfo& info) {
    info_ = info;
    
    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    
    // When viewports are enabled, tweak WindowRounding/WindowBg
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    
    setupStyle();
    loadFonts();
    
    // Initialize GLFW backend
    if (!ImGui_ImplGlfw_InitForVulkan(info.window, true)) {
        return false;
    }
    
    // Create descriptor pool if not provided
    if (info.descriptorPool == VK_NULL_HANDLE) {
        if (!createDescriptorPool()) {
            return false;
        }
        ownsDescriptorPool_ = true;
    } else {
        imguiDescriptorPool_ = info.descriptorPool;
        ownsDescriptorPool_ = false;
    }
    
    // Initialize Vulkan backend
    ImGui_ImplVulkan_InitInfo vulkanInfo = {};
    vulkanInfo.Instance = info.instance;
    vulkanInfo.PhysicalDevice = info.physicalDevice;
    vulkanInfo.Device = info.device;
    vulkanInfo.QueueFamily = info.queueFamily;
    vulkanInfo.Queue = info.queue;
    vulkanInfo.DescriptorPool = imguiDescriptorPool_;
    vulkanInfo.RenderPass = info.renderPass;
    vulkanInfo.MinImageCount = info.imageCount;
    vulkanInfo.ImageCount = info.imageCount;
    vulkanInfo.MSAASamples = info.msaaSamples;
    
    if (!ImGui_ImplVulkan_Init(&vulkanInfo)) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void ImGuiBackend::shutdown() {
    if (!initialized_) return;
    
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    
    if (ownsDescriptorPool_ && imguiDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(info_.device, imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = VK_NULL_HANDLE;
    }
    
    ImGui::DestroyContext();
    
    initialized_ = false;
}

void ImGuiBackend::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiBackend::endFrame() {
    ImGui::Render();
    
    // Update and render additional platform windows
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImGuiBackend::render(VkCommandBuffer commandBuffer) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiBackend::rebuildFonts() {
    ImGui_ImplVulkan_DestroyFontsTexture();
    ImGui_ImplVulkan_CreateFontsTexture();
}

VkDescriptorSet ImGuiBackend::addTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout) {
    return ImGui_ImplVulkan_AddTexture(sampler, imageView, imageLayout);
}

void ImGuiBackend::removeTexture(VkDescriptorSet textureSet) {
    ImGui_ImplVulkan_RemoveTexture(textureSet);
}

bool ImGuiBackend::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]);
    poolInfo.pPoolSizes = poolSizes;
    
    if (vkCreateDescriptorPool(info_.device, &poolInfo, nullptr, &imguiDescriptorPool_) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

void ImGuiBackend::setupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Dark theme
    ImGui::StyleColorsDark();
    
    // Customize colors
    ImVec4* colors = style.Colors;
    
    // Background colors
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    
    // Borders
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    // Frame backgrounds
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    
    // Title bar
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    
    // Menu bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    
    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.38f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.40f, 0.43f, 1.00f);
    
    // Check mark
    colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    
    // Slider
    colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.80f, 1.00f, 1.00f);
    
    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.50f, 0.80f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.45f, 0.75f, 1.00f);
    
    // Headers
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.50f, 0.80f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.45f, 0.75f, 1.00f);
    
    // Separator
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    
    // Resize grip
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.30f, 0.33f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    
    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.50f, 0.80f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    
    // Docking
    colors[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.50f, 0.80f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    
    // Table
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    
    // Text
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.50f, 0.80f, 0.50f);
    
    // Style adjustments
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;
    
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
}

void ImGuiBackend::loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Default font
    io.Fonts->AddFontDefault();
    
    // TODO: Load custom fonts
    // io.Fonts->AddFontFromFileTTF("fonts/Roboto-Regular.ttf", 15.0f);
    
    // Build font atlas
    // This will be uploaded to GPU in ImGui_ImplVulkan_CreateFontsTexture
}

} // namespace Sanic::Editor
