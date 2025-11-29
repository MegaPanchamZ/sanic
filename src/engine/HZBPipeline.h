#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

class VulkanContext;

/**
 * HZBPipeline
 * ===========
 * Hierarchical Z-Buffer generation and management for occlusion culling.
 * 
 * Features:
 * - Generates mip chain from depth buffer
 * - Supports two-pass occlusion culling (Main + Post)
 * - Uses compute shaders for maximum performance
 * - Reversed-Z aware (1.0 = near, 0.0 = far)
 * 
 * Usage in Nanite-style pipeline:
 * 1. Main pass: Render with previous frame's HZB for early-Z rejection
 * 2. Generate new HZB from depth buffer
 * 3. Post pass: Re-test clusters that were culled in main pass
 */
class HZBPipeline {
public:
    struct Config {
        uint32_t maxWidth;
        uint32_t maxHeight;
        VkFormat format;
        
        Config() 
            : maxWidth(4096)
            , maxHeight(4096)
            , format(VK_FORMAT_R32_SFLOAT)
        {}
    };
    
    HZBPipeline(VulkanContext& context, const Config& config = Config{});
    ~HZBPipeline();
    
    // Non-copyable
    HZBPipeline(const HZBPipeline&) = delete;
    HZBPipeline& operator=(const HZBPipeline&) = delete;
    
    /**
     * Generate HZB mip chain from depth buffer.
     * @param cmd Command buffer to record into
     * @param depthImage Source depth image
     * @param depthView Source depth image view
     * @param width Depth buffer width
     * @param height Depth buffer height
     */
    void generateHZB(VkCommandBuffer cmd, VkImage depthImage, VkImageView depthView, 
                     uint32_t width, uint32_t height);
    
    /**
     * Get HZB texture for sampling in culling shaders.
     */
    VkImageView getHZBView() const { return hzbImageView; }
    VkSampler getHZBSampler() const { return hzbSampler; }
    
    /**
     * Get HZB dimensions at a specific mip level.
     */
    glm::uvec2 getMipSize(uint32_t mipLevel) const;
    uint32_t getMipLevels() const { return hzbMipLevels; }
    
    /**
     * Get current HZB size.
     */
    glm::uvec2 getSize() const { return glm::uvec2(currentWidth, currentHeight); }

private:
    void createHZBImage(uint32_t width, uint32_t height);
    void createPipeline();
    void createSampler();
    void updateDescriptorSets(VkImageView depthView);
    void destroyResources();
    
    VulkanContext& context;
    Config config;
    
    // HZB image and views
    VkImage hzbImage = VK_NULL_HANDLE;
    VkDeviceMemory hzbMemory = VK_NULL_HANDLE;
    VkImageView hzbImageView = VK_NULL_HANDLE;  // View of all mips
    std::vector<VkImageView> hzbMipViews;       // Views per mip level
    VkSampler hzbSampler = VK_NULL_HANDLE;
    
    // Pipeline resources
    VkPipeline hzbGeneratePipeline = VK_NULL_HANDLE;
    VkPipelineLayout hzbPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;  // Per mip pass
    
    // Current state
    uint32_t currentWidth = 0;
    uint32_t currentHeight = 0;
    uint32_t hzbMipLevels = 0;
    
    // Push constants for HZB generation
    struct HZBPushConstants {
        glm::vec2 srcSize;
        glm::vec2 dstSize;
        int32_t srcMipLevel;
        int32_t isFirstPass;
        int32_t padding[2];
    };
};
