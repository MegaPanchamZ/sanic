#pragma once
#include "VulkanContext.h"
#include <vector>

// Hierarchical Z-Buffer (Hi-Z) for efficient screen-space ray marching
// Generates min-depth mipmaps of the depth buffer

class HiZBuffer {
public:
    HiZBuffer(VulkanContext& context, uint32_t width, uint32_t height, VkDescriptorPool descriptorPool);
    ~HiZBuffer();
    
    // Generate the depth pyramid from the depth buffer
    void generate(VkCommandBuffer cmd, VkImageView depthView, VkSampler depthSampler);
    
    // Resize when window changes
    void resize(uint32_t width, uint32_t height);
    
    // Get the Hi-Z pyramid for sampling
    VkImageView getPyramidView() const { return pyramidView; }
    VkImage getPyramidImage() const { return pyramidImage; }
    VkSampler getPyramidSampler() const { return pyramidSampler; }
    uint32_t getMipLevels() const { return mipLevels; }
    
private:
    VulkanContext& context;
    uint32_t width, height;
    uint32_t mipLevels;
    VkDescriptorPool descriptorPool;
    
    // Depth pyramid storage
    VkImage pyramidImage = VK_NULL_HANDLE;
    VkDeviceMemory pyramidMemory = VK_NULL_HANDLE;
    VkImageView pyramidView = VK_NULL_HANDLE;  // View for all mips
    std::vector<VkImageView> mipViews;         // Per-mip views for writing
    VkSampler pyramidSampler = VK_NULL_HANDLE;
    
    // Compute pipeline
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;  // One per mip level
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    
    struct PushConstants {
        int inputWidth, inputHeight;
        int outputWidth, outputHeight;
        int mipLevel;
        int padding[3];
    };
    
    void createPyramidImage();
    void createSampler();
    void createDescriptorSetLayout();
    void createComputePipeline();
    void createDescriptorSets();
    
    void destroyResources();
    
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
    
    uint32_t calculateMipLevels(uint32_t w, uint32_t h);
};
