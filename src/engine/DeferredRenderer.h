#pragma once
#include "VulkanContext.h"
#include "GameObject.h"
#include <vector>
#include <memory>

class DeferredRenderer {
public:
    DeferredRenderer(VulkanContext& context, 
                     uint32_t width, uint32_t height, 
                     VkFormat swapchainFormat,
                     VkDescriptorSetLayout sceneDescriptorSetLayout,
                     VkDescriptorPool descriptorPool);
    ~DeferredRenderer();

    void createFramebuffers(const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView);
    
    void updateCompositionDescriptorSet(VkBuffer uniformBuffer, VkDeviceSize uboSize,
                                        VkImageView shadowView, VkSampler shadowSampler,
                                        VkImageView envView, VkSampler envSampler);

    void render(VkCommandBuffer cmd, uint32_t imageIndex, 
                const std::vector<GameObject>& gameObjects);

    VkRenderPass getRenderPass() const { return renderPass; }
    
    // G-Buffer accessors for SSR
    VkImageView getPositionImageView() const { return position.view; }
    VkImageView getNormalImageView() const { return normal.view; }
    VkImageView getAlbedoImageView() const { return albedo.view; }
    VkImageView getPBRImageView() const { return pbr.view; }
    VkImageView getDepthImageView() const { return depthView; }
    VkImageView getSceneColorImageView() const { return sceneColorView; }
    VkSampler getGBufferSampler() const { return gBufferSampler; }

private:
    struct GBufferAttachment {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkFormat format;
    };

    struct PushConstantData {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        uint64_t meshletBufferAddress;
        uint64_t meshletVerticesAddress;
        uint64_t meshletTrianglesAddress;
        uint64_t vertexBufferAddress;
        uint32_t meshletCount;
    };

    VulkanContext& context;
    uint32_t width, height;
    VkFormat swapchainFormat;
    VkDescriptorSetLayout sceneDescriptorSetLayout;
    VkDescriptorPool descriptorPool;

    // Resources
    GBufferAttachment position, normal, albedo, pbr;
    VkImageView depthView = VK_NULL_HANDLE;  // External reference (not owned)
    VkImageView sceneColorView = VK_NULL_HANDLE;  // Previous frame color for SSR
    VkSampler gBufferSampler = VK_NULL_HANDLE;
    
    VkRenderPass renderPass;
    std::vector<VkFramebuffer> framebuffers;

    // Pipelines
    VkPipelineLayout meshPipelineLayout;
    VkPipeline meshPipeline;
    
    VkPipelineLayout compositionPipelineLayout;
    VkPipeline compositionPipeline;
    VkDescriptorSetLayout compositionDescriptorSetLayout;
    VkDescriptorSet compositionDescriptorSet;

    // Mesh Shader Function Pointer
    PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT;

    void createRenderPass();
    void createGBufferResources();
    void createPipelines();
    void createCompositionDescriptorSetLayout();
    void loadMeshShaderFunctions();

    void createGBufferAttachment(GBufferAttachment& attachment, VkFormat format, VkImageUsageFlags usage);
    
    // Helpers
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
    
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
};
