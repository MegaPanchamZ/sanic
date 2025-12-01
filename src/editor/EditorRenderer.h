/**
 * EditorRenderer.h
 * 
 * Manages offscreen rendering for viewport and provides texture to ImGui.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Sanic::Editor {

class ImGuiBackend;

struct ViewportRenderTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
};

class EditorRenderer {
public:
    struct InitInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        ImGuiBackend* imguiBackend = nullptr;
    };
    
    bool initialize(const InitInfo& info);
    void shutdown();
    
    // Resize viewport render target
    void resize(uint32_t width, uint32_t height);
    
    // Get render target for ImGui display
    VkDescriptorSet getViewportTexture() const { return renderTarget_.descriptorSet; }
    VkImage getViewportImage() const { return renderTarget_.image; }
    VkImageView getViewportImageView() const { return renderTarget_.imageView; }
    
    // Begin/end rendering to viewport
    void beginViewportRender(VkCommandBuffer cmd);
    void endViewportRender(VkCommandBuffer cmd);
    
    // Blit from source image to viewport render target
    void blitToViewport(VkCommandBuffer cmd, VkImage srcImage, VkImageLayout srcLayout,
                        uint32_t srcWidth, uint32_t srcHeight);
    
    // Render pass for viewport
    VkRenderPass getViewportRenderPass() const { return viewportRenderPass_; }
    VkFramebuffer getViewportFramebuffer() const { return viewportFramebuffer_; }
    
    uint32_t getWidth() const { return renderTarget_.width; }
    uint32_t getHeight() const { return renderTarget_.height; }
    
private:
    bool createRenderPass();
    bool createRenderTarget(uint32_t width, uint32_t height);
    void destroyRenderTarget();
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    InitInfo info_;
    bool initialized_ = false;
    
    VkRenderPass viewportRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer viewportFramebuffer_ = VK_NULL_HANDLE;
    
    ViewportRenderTarget renderTarget_;
    
    // Depth buffer
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
};

} // namespace Sanic::Editor
