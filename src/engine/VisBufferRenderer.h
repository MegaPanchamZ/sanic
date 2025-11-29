#pragma once
#include "VulkanContext.h"
#include "GameObject.h"
#include <vector>
#include <memory>

class VisBufferRenderer {
public:
    VisBufferRenderer(VulkanContext& context, 
                      uint32_t width, uint32_t height, 
                      VkFormat swapchainFormat,
                      VkDescriptorSetLayout sceneDescriptorSetLayout,
                      VkDescriptorPool descriptorPool);
    ~VisBufferRenderer();

    void createFramebuffers(const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView);
    
    void render(VkCommandBuffer cmd, uint32_t imageIndex, 
                const std::vector<GameObject>& gameObjects);

    void updateComputeDescriptorSet(VkBuffer uniformBuffer, VkDeviceSize uboSize);

    VkRenderPass getRenderPass() const { return renderPass; }
    
    // Visibility Buffer Accessor
    VkImageView getVisBufferImageView() const { return visBuffer.view; }
    VkImage getVisBufferImage() const { return visBuffer.image; }

private:
    struct VisBufferAttachment {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkFormat format;
    };

    VulkanContext& context;
    uint32_t width, height;
    VkFormat swapchainFormat;
    VkDescriptorSetLayout sceneDescriptorSetLayout;
    VkDescriptorPool descriptorPool;

    // Resources
    VisBufferAttachment visBuffer; // R64_UINT
    VkImageView depthView = VK_NULL_HANDLE;  // External reference (not owned)
    
    VkRenderPass renderPass;
    std::vector<VkFramebuffer> framebuffers;

    // Pipelines
    VkPipelineLayout meshPipelineLayout;
    VkPipeline meshPipeline;
    
    VkPipelineLayout materialPipelineLayout;
    VkPipeline materialPipeline;

    // Compute Pipelines (Software Rasterizer)
    VkPipelineLayout swRasterizePipelineLayout;
    VkPipeline swRasterizePipeline;
    
    VkDescriptorSetLayout computeDescriptorSetLayout;
    VkDescriptorSet computeDescriptorSet;

    // Mesh Shader Function Pointer
    PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT;

    void createRenderPass();
    void createVisBufferResources();
    void createPipelines();
    void createComputeDescriptorSetLayout();
    void createComputeDescriptorSet();
    void loadMeshShaderFunctions();

    void createVisBufferAttachment(VisBufferAttachment& attachment, VkFormat format, VkImageUsageFlags usage);
    
    // Helpers
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
    
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
};
