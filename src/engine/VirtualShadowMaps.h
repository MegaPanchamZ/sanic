/**
 * VirtualShadowMaps.h
 * 
 * Virtual Shadow Maps (VSM) implementation for high-resolution shadows.
 * Uses clipmap-based virtual texture with page streaming.
 * 
 * Turn 37-39: Shadow system
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_set>

class VulkanContext;

// Virtual page - 128x128 tile in shadow atlas
struct ShadowPage {
    uint32_t pageX;
    uint32_t pageY;
    uint32_t mipLevel;
    uint32_t lightIndex;
    
    glm::mat4 lightViewProj;
    float minDepth;
    float maxDepth;
    
    bool resident;
    bool dirty;
    uint32_t lastUsedFrame;
};

// GPU page table entry
struct alignas(16) GPUPageTableEntry {
    uint32_t physicalX;     // Physical atlas X
    uint32_t physicalY;     // Physical atlas Y
    uint32_t flags;         // Resident, dirty, etc.
    uint32_t pad;
};

// Light shadow info
struct ShadowLightInfo {
    glm::mat4 viewProj;
    glm::vec4 position;     // xyz = pos, w = type (0=dir, 1=point, 2=spot)
    glm::vec4 direction;    // xyz = dir, w = range
    glm::vec4 color;        // xyz = color, w = intensity
    glm::vec4 shadowParams; // x = bias, y = normalBias, z = softness, w = enabled
};

// Clipmap level for directional light
struct ShadowClipMapLevel {
    glm::mat4 viewProj;
    glm::vec3 center;
    float texelSize;
    uint32_t resolution;
    uint32_t pageTableOffset;
    bool needsUpdate;
};

struct VSMConfig {
    // Virtual texture
    uint32_t virtualResolution = 16384;     // Virtual shadow map resolution
    uint32_t physicalAtlasSize = 8192;      // Physical atlas size
    uint32_t pageSize = 128;                // Tile size
    uint32_t maxResidentPages = 4096;       // Max cached pages
    
    // Clipmap (directional light)
    uint32_t clipMapLevels = 6;
    float clipMapBaseExtent = 10.0f;        // Base level world extent
    float clipMapScale = 2.0f;              // Scale between levels
    
    // Quality
    uint32_t maxLights = 16;
    float depthBias = 0.001f;
    float normalBias = 0.01f;
    float softShadowRadius = 0.02f;
    uint32_t pcfSamples = 16;
    
    // Formats
    VkFormat shadowFormat = VK_FORMAT_D32_SFLOAT;
    VkFormat pageTableFormat = VK_FORMAT_R32G32B32A32_UINT;
};

class VirtualShadowMaps {
public:
    VirtualShadowMaps() = default;
    ~VirtualShadowMaps();
    
    bool initialize(VulkanContext* context, const VSMConfig& config = {});
    void cleanup();
    
    // Update shadow maps for frame
    void update(VkCommandBuffer cmd,
                const glm::vec3& cameraPos,
                const glm::mat4& cameraViewProj,
                const std::vector<ShadowLightInfo>& lights);
    
    // Mark visible pages from depth buffer
    void markVisiblePages(VkCommandBuffer cmd,
                          VkImageView depthBuffer,
                          VkImageView normalBuffer,
                          const glm::mat4& invViewProj);
    
    // Render shadow pages
    void renderPages(VkCommandBuffer cmd,
                     VkBuffer vertexBuffer,
                     VkBuffer indexBuffer,
                     VkBuffer drawCommands,
                     uint32_t drawCount);
    
    // Sample shadows in lighting pass
    void bindForSampling(VkCommandBuffer cmd,
                         VkPipelineLayout layout,
                         uint32_t setIndex);
    
    // Get shadow factor at world position
    float getShadowFactor(const glm::vec3& worldPos,
                          const glm::vec3& normal,
                          uint32_t lightIndex) const;
    
    // Accessors
    VkImageView getShadowAtlasView() const { return shadowAtlasView_; }
    VkBuffer getPageTableBuffer() const { return pageTableBuffer_; }
    VkBuffer getLightBuffer() const { return lightBuffer_; }
    
    const VSMConfig& getConfig() const { return config_; }
    
private:
    bool createShadowAtlas();
    bool createPageTable();
    bool createPipelines();
    bool createClipMaps();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    void updateClipMaps(const glm::vec3& cameraPos);
    void streamPages(VkCommandBuffer cmd);
    void evictOldPages();
    
    uint64_t getPageHash(uint32_t lightIndex, uint32_t level, uint32_t x, uint32_t y) const;
    
    VulkanContext* context_ = nullptr;
    bool initialized_ = false;
    
    VSMConfig config_;
    uint32_t frameIndex_ = 0;
    
    // Shadow atlas (physical pages)
    VkImage shadowAtlas_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowAtlasMemory_ = VK_NULL_HANDLE;
    VkImageView shadowAtlasView_ = VK_NULL_HANDLE;
    
    // Depth buffer for rendering
    VkImage depthBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    
    // Page table (virtual -> physical mapping)
    VkBuffer pageTableBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory pageTableMemory_ = VK_NULL_HANDLE;
    
    // Page request buffer (from marking pass)
    VkBuffer pageRequestBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory pageRequestMemory_ = VK_NULL_HANDLE;
    
    // Light data buffer
    VkBuffer lightBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lightMemory_ = VK_NULL_HANDLE;
    
    // Clipmap levels for directional light
    std::vector<ShadowClipMapLevel> clipMapLevels_;
    
    // Page management
    std::vector<ShadowPage> residentPages_;
    std::unordered_set<uint64_t> residentPageHashes_;
    std::vector<uint32_t> freePageSlots_;
    
    // Pipelines
    VkPipeline markPagesPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout markPagesLayout_ = VK_NULL_HANDLE;
    
    VkPipeline renderShadowPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout renderShadowLayout_ = VK_NULL_HANDLE;
    
    VkPipeline sampleShadowPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout sampleShadowLayout_ = VK_NULL_HANDLE;
    
    // Render pass for shadow rendering
    VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    VkSampler comparisonSampler_ = VK_NULL_HANDLE;
};
