/**
 * SurfaceCache.h
 * 
 * Lumen-style surface cache for indirect lighting.
 * Implements mesh card capture and radiance caching.
 * 
 * Turn 16-18: Surface cache atlas and card system
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <string>

class VulkanContext;

// Mesh card - 6-sided capture of mesh surface
struct MeshCard {
    glm::vec3 center;
    float extent;           // Half-size of card
    
    glm::vec3 normal;       // Card facing direction
    uint32_t atlasOffset;   // Offset in atlas texture
    
    glm::vec2 atlasSize;    // Size in atlas (pixels)
    uint32_t meshId;
    uint32_t cardIndex;     // 0-5 for 6 sides
    
    // Bounds for ray intersection
    glm::vec3 boundsMin;
    float pad0;
    glm::vec3 boundsMax;
    float pad1;
};

// GPU-side card data
struct GPUMeshCard {
    glm::vec4 centerExtent;     // xyz = center, w = extent
    glm::vec4 normalAtlas;      // xyz = normal, w = packed atlas offset
    glm::vec4 atlasSizeMeshId;  // xy = size, z = meshId, w = cardIndex
    glm::vec4 boundsMin;
    glm::vec4 boundsMax;
};

// Surface cache tile - portion of atlas
struct SurfaceCacheTile {
    uint32_t x, y;          // Position in atlas
    uint32_t width, height; // Size in pixels
    uint32_t cardIndex;     // Which card this tile belongs to
    uint32_t mipLevel;
    bool valid;
    bool needsUpdate;
    uint32_t lastUsedFrame;
    uint32_t priority;      // For eviction decisions
};

// Surface cache page for virtual texturing
struct SurfaceCachePage {
    uint32_t physicalX, physicalY;
    uint32_t virtualX, virtualY;
    uint32_t cardIndex;
    bool resident;
    uint32_t lastAccessFrame;
};

// Configuration
struct SurfaceCacheConfig {
    uint32_t atlasWidth = 4096;
    uint32_t atlasHeight = 4096;
    uint32_t cardResolution = 128;      // Default card resolution
    uint32_t maxCards = 8192;
    uint32_t maxMeshes = 1024;
    uint32_t pageSize = 128;            // Virtual texture page size
    float updateBudgetMs = 2.0f;        // Time budget per frame
    bool useVirtualTexturing = true;
};

class SurfaceCache {
public:
    SurfaceCache() = default;
    ~SurfaceCache();
    
    bool initialize(VulkanContext* context, const SurfaceCacheConfig& config = SurfaceCacheConfig{});
    void cleanup();
    
    /**
     * Register a mesh and generate its cards
     * @param meshId Unique mesh identifier
     * @param boundsMin Mesh AABB minimum
     * @param boundsMax Mesh AABB maximum
     * @param transform World transform
     * @return First card index for this mesh
     */
    uint32_t registerMesh(uint32_t meshId,
                          const glm::vec3& boundsMin,
                          const glm::vec3& boundsMax,
                          const glm::mat4& transform);
    
    /**
     * Update mesh transform (invalidates cards)
     */
    void updateMeshTransform(uint32_t meshId, const glm::mat4& transform);
    
    /**
     * Mark cards as needing update (e.g., light changed)
     */
    void invalidateCards(uint32_t meshId);
    void invalidateAllCards();
    
    /**
     * Capture cards that need updating
     * Uses compute shader to render direct lighting to cards
     */
    void captureCards(VkCommandBuffer cmd,
                      VkBuffer lightBuffer,
                      uint32_t lightCount,
                      VkImageView shadowMap,
                      const glm::mat4& lightViewProj);
    
    /**
     * Update radiance on cards (for GI)
     */
    void updateRadiance(VkCommandBuffer cmd,
                        VkImageView irradianceProbes,
                        VkBuffer probeBuffer);
    
    /**
     * Sample surface cache for a world position
     * Used by screen probes for indirect lighting
     */
    // This is done in shader, but we provide buffer access
    
    /**
     * Get atlas image view for shader binding
     */
    VkImageView getRadianceAtlasView() const { return radianceAtlasView_; }
    VkImageView getNormalAtlasView() const { return normalAtlasView_; }
    VkImageView getDepthAtlasView() const { return depthAtlasView_; }
    
    /**
     * Get card buffer for shader binding
     */
    VkBuffer getCardBuffer() const { return cardBuffer_; }
    VkDeviceAddress getCardBufferAddress() const { return cardBufferAddr_; }
    
    /**
     * Get page table for virtual texturing
     */
    VkBuffer getPageTableBuffer() const { return pageTableBuffer_; }
    
    /**
     * Debug: Get statistics
     */
    struct Stats {
        uint32_t totalCards;
        uint32_t validCards;
        uint32_t pendingUpdates;
        uint32_t atlasUsedPixels;
        float atlasUtilization;
    };
    Stats getStats() const;
    
private:
    bool createAtlasTextures();
    bool createBuffers();
    bool createDescriptorSets();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    // Generate 6 cards for a mesh (±X, ±Y, ±Z)
    void generateCardsForMesh(uint32_t meshId,
                             const glm::vec3& center,
                             const glm::vec3& extents,
                             const glm::mat4& transform);
    
    // Allocate space in atlas for a card
    bool allocateAtlasTile(uint32_t width, uint32_t height,
                           uint32_t& outX, uint32_t& outY);
    
    // Free atlas space
    void freeAtlasTile(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    
    VulkanContext* context_ = nullptr;
    SurfaceCacheConfig config_;
    
    // Atlas textures
    VkImage radianceAtlas_ = VK_NULL_HANDLE;      // RGB radiance
    VkImageView radianceAtlasView_ = VK_NULL_HANDLE;
    VkDeviceMemory radianceAtlasMemory_ = VK_NULL_HANDLE;
    
    VkImage normalAtlas_ = VK_NULL_HANDLE;        // World-space normals
    VkImageView normalAtlasView_ = VK_NULL_HANDLE;
    VkDeviceMemory normalAtlasMemory_ = VK_NULL_HANDLE;
    
    VkImage depthAtlas_ = VK_NULL_HANDLE;         // Depth from card view
    VkImageView depthAtlasView_ = VK_NULL_HANDLE;
    VkDeviceMemory depthAtlasMemory_ = VK_NULL_HANDLE;
    
    // Card data
    std::vector<MeshCard> cards_;
    std::unordered_map<uint32_t, std::vector<uint32_t>> meshToCards_;
    
    VkBuffer cardBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory cardMemory_ = VK_NULL_HANDLE;
    VkDeviceAddress cardBufferAddr_ = 0;
    
    // Atlas allocation tracking (simple row-based allocator)
    struct AtlasRow {
        uint32_t y;
        uint32_t height;
        uint32_t usedWidth;
    };
    std::vector<AtlasRow> atlasRows_;
    
    // Virtual texturing
    VkBuffer pageTableBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory pageTableMemory_ = VK_NULL_HANDLE;
    std::vector<SurfaceCachePage> pages_;
    
    // Update queue
    std::vector<uint32_t> pendingUpdates_;
    
    // Pipelines
    VkPipeline cardCapturePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout cardCaptureLayout_ = VK_NULL_HANDLE;
    VkPipeline radianceUpdatePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout radianceUpdateLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorSetLayout atlasDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet atlasDescSet_ = VK_NULL_HANDLE;
    
    // Samplers
    VkSampler atlasSampler_ = VK_NULL_HANDLE;
    
    bool initialized_ = false;
};
