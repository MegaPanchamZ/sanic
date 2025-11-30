/**
 * SelectionOutline.h
 * 
 * Renders selection outlines around selected objects.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Sanic {
    class ECSManager;
}

namespace Sanic::Editor {

class Selection;

class SelectionOutline {
public:
    SelectionOutline();
    ~SelectionOutline();
    
    struct InitInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint32_t width = 1920;
        uint32_t height = 1080;
    };
    
    bool initialize(const InitInfo& info);
    void shutdown();
    void resize(uint32_t width, uint32_t height);
    
    // Render selection outlines
    void render(VkCommandBuffer cmd, 
                const Selection& selection,
                const Sanic::ECSManager& ecs,
                const glm::mat4& view,
                const glm::mat4& proj);
    
    // Settings
    void setOutlineColor(const glm::vec4& color) { outlineColor_ = color; }
    void setOutlineWidth(float width) { outlineWidth_ = width; }
    void setHoverColor(const glm::vec4& color) { hoverColor_ = color; }
    
private:
    bool createPipelines();
    void destroyPipelines();
    bool createStencilResources();
    void destroyStencilResources();
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    InitInfo info_;
    bool initialized_ = false;
    
    // Stencil pass pipeline (writes to stencil)
    VkPipeline stencilPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout stencilPipelineLayout_ = VK_NULL_HANDLE;
    
    // Outline pass pipeline (reads stencil, draws outline)
    VkPipeline outlinePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout outlinePipelineLayout_ = VK_NULL_HANDLE;
    
    // Stencil buffer
    VkImage stencilImage_ = VK_NULL_HANDLE;
    VkDeviceMemory stencilMemory_ = VK_NULL_HANDLE;
    VkImageView stencilView_ = VK_NULL_HANDLE;
    
    // Settings
    glm::vec4 outlineColor_ = glm::vec4(1.0f, 0.6f, 0.1f, 1.0f);
    glm::vec4 hoverColor_ = glm::vec4(0.5f, 0.8f, 1.0f, 0.5f);
    float outlineWidth_ = 2.0f;
    
    // Push constants
    struct PushConstants {
        glm::mat4 mvp;
        glm::vec4 color;
        float outlineWidth;
        float padding[3];
    };
};

} // namespace Sanic::Editor
