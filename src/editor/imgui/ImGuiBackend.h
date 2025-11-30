/**
 * ImGuiBackend.h
 * 
 * ImGui Vulkan/GLFW backend integration.
 */

#pragma once

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace Sanic::Editor {

class ImGuiBackend {
public:
    struct InitInfo {
        GLFWwindow* window = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t queueFamily = 0;
        VkQueue queue = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint32_t imageCount = 2;
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    };
    
    bool initialize(const InitInfo& info);
    void shutdown();
    
    void beginFrame();
    void endFrame();
    void render(VkCommandBuffer commandBuffer);
    
    // Font management
    void rebuildFonts();
    
    // Texture helpers for viewport rendering
    VkDescriptorSet addTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);
    void removeTexture(VkDescriptorSet textureSet);
    
    bool isInitialized() const { return initialized_; }
    
private:
    bool createDescriptorPool();
    void setupStyle();
    void loadFonts();
    
    InitInfo info_;
    bool initialized_ = false;
    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;
    bool ownsDescriptorPool_ = false;
};

} // namespace Sanic::Editor
