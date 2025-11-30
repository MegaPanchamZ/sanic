/**
 * VirtualShadowMapsAdvanced.h
 * 
 * Unreal Engine 5-style Virtual Shadow Maps with:
 * - GPU feedback for page allocation
 * - Multi-light support (directional, point, spot)
 * - Clipmap for directional lights
 * - Static/dynamic page caching
 * - LRU-based page eviction
 * - Nanite-native rendering integration
 * 
 * Based on Unreal Engine's VirtualShadowMapArray.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <array>

class VulkanContext;
class ClusterHierarchy;

// ============================================================================
// VSM CONSTANTS (Matching Unreal)
// ============================================================================

namespace VSMConfig {
    constexpr uint32_t PAGE_SIZE = 128;              // 128x128 texels per page
    constexpr uint32_t PAGE_TABLE_SIZE = 128;        // 128x128 pages = 16384 virtual resolution
    constexpr uint32_t VIRTUAL_MAX_RES = 16384;      // Maximum virtual shadow map resolution
    constexpr uint32_t MAX_MIP_LEVELS = 8;           // log2(16384/128) + 1
    constexpr uint32_t MAX_PHYSICAL_PAGES = 4096;    // Physical page pool size
    constexpr uint32_t MAX_SINGLE_PAGE_LIGHTS = 8192;// Single-page shadow maps for distant lights
    constexpr uint32_t MAX_VSM_PER_LIGHT = 6;        // Max faces per light (cubemap)
    constexpr uint32_t CLIPMAP_LEVELS = 12;          // Directional light clipmap levels
    
    // Page flags
    constexpr uint32_t PAGE_FLAG_STATIC = 0x1;       // Static cached page
    constexpr uint32_t PAGE_FLAG_DYNAMIC = 0x2;      // Dynamic page (re-rendered each frame)
    constexpr uint32_t PAGE_FLAG_REQUESTED = 0x4;    // Page was requested this frame
    constexpr uint32_t PAGE_FLAG_ALLOCATED = 0x8;    // Page has physical allocation
}

// ============================================================================
// LIGHT TYPES
// ============================================================================

enum class VSMLightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
    Rect = 3
};

/**
 * Per-light VSM configuration
 */
struct VSMLightInfo {
    VSMLightType type;
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 color;
    float intensity;
    float radius;           // For point/spot lights
    float innerConeAngle;   // For spot lights
    float outerConeAngle;   // For spot lights
    
    // Shadow settings
    bool castShadows = true;
    bool useStaticCache = true;
    float resolutionScale = 1.0f;
    float depthBias = 0.005f;
    float normalBias = 0.02f;
    float maxDistance = 1000.0f;
    
    // Clipmap settings (directional only)
    uint32_t clipmapFirstLevel = 6;
    uint32_t clipmapLastLevel = 18;
};

// ============================================================================
// GPU STRUCTURES
// ============================================================================

/**
 * GPU page table entry
 */
struct alignas(4) FPageTableEntry {
    uint32_t physicalPageIndex : 24;  // Index in physical pool (or 0xFFFFFF if not allocated)
    uint32_t flags : 8;               // PAGE_FLAG_*
};

/**
 * GPU VSM projection data
 */
struct alignas(64) FVSMProjectionData {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 lightDirection;     // xyz = direction, w = type
    glm::vec4 lightPositionRadius; // xyz = position, w = radius
    glm::vec4 clipmapParams;      // x = level, y = resolution, z = worldSize, w = unused
    glm::ivec4 pageOffset;        // Page table offset for this VSM
    float resolutionLodBias;
    float depthBias;
    float normalBias;
    uint32_t flags;
};

/**
 * GPU page request from marking pass
 */
struct alignas(16) FGPUPageRequest {
    uint32_t vsmId;
    uint32_t pageX;
    uint32_t pageY;
    uint32_t mipLevel;
    float priority;
    uint32_t flags;
    uint32_t padding[2];
};

/**
 * Physical page metadata
 */
struct alignas(16) FPhysicalPageMeta {
    uint32_t vsmId;
    uint32_t virtualPageX;
    uint32_t virtualPageY;
    uint32_t mipLevel;
    uint64_t lastUsedFrame;
    uint32_t flags;
    uint32_t padding;
};

/**
 * GPU marking pass uniforms
 */
struct alignas(16) FVSMMarkingUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 invViewProj;
    glm::vec4 cameraPosition;
    glm::vec4 screenParams;       // xy = resolution, zw = 1/resolution
    uint32_t numLights;
    uint32_t frameNumber;
    float pageDilationBorderSize;
    uint32_t markCoarsePages;
};

// ============================================================================
// CLIPMAP DATA
// ============================================================================

/**
 * Per-level clipmap data for directional lights
 */
struct FClipmapLevel {
    glm::mat4 viewToClip;
    glm::vec3 worldCenter;          // Snapped world center
    float worldSize;                // World-space size of this level
    glm::ivec2 pageOffset;          // Page table offset
    glm::ivec2 cornerOffset;        // Page-aligned corner offset
    float resolution;
    uint32_t vsmIndex;              // Index of this level's VSM
};

/**
 * Complete clipmap for a directional light
 */
struct FDirectionalLightClipmap {
    std::vector<FClipmapLevel> levels;
    glm::vec3 lightDirection;
    uint32_t firstLevel;
    uint32_t lastLevel;
    
    void updateForCamera(const glm::vec3& cameraPos, const glm::vec3& lightDir);
    glm::mat4 computeLevelViewProj(uint32_t level, const glm::vec3& center) const;
};

// ============================================================================
// PAGE CACHE ENTRY
// ============================================================================

/**
 * Per-page cache entry for tracking
 */
struct FVSMCacheEntry {
    uint32_t physicalPageIndex = UINT32_MAX;
    uint64_t lastUsedFrame = 0;
    uint32_t lastRenderedFrame = 0;
    uint32_t flags = 0;
    bool isStatic = false;
    bool needsRender = true;
    
    // For invalidation tracking
    std::vector<uint32_t> renderedPrimitiveIds;
};

// ============================================================================
// VIRTUAL SHADOW MAP ARRAY
// ============================================================================

/**
 * VirtualShadowMapArray - Manages all virtual shadow maps
 * 
 * Handles:
 * - Page table management
 * - Physical page allocation
 * - GPU feedback processing
 * - Multi-light support
 * - Cache management
 */
class VirtualShadowMapArray {
public:
    VirtualShadowMapArray(VulkanContext& context);
    ~VirtualShadowMapArray();
    
    /**
     * Initialize VSM system
     * @param maxPhysicalPages Maximum physical pages to allocate
     */
    void initialize(uint32_t maxPhysicalPages = VSMConfig::MAX_PHYSICAL_PAGES);
    void shutdown();
    
    // ========================================================================
    // LIGHT MANAGEMENT
    // ========================================================================
    
    /**
     * Add a light that casts virtual shadows
     * @return Light index
     */
    uint32_t addLight(const VSMLightInfo& light);
    void removeLight(uint32_t lightIndex);
    void updateLight(uint32_t lightIndex, const VSMLightInfo& light);
    
    /**
     * Get the number of VSMs for a light (1 for spot, 6 for point, N for directional clipmap)
     */
    uint32_t getVSMCountForLight(uint32_t lightIndex) const;
    
    // ========================================================================
    // FRAME UPDATE
    // ========================================================================
    
    /**
     * Begin frame - setup for new frame
     */
    void beginFrame(VkCommandBuffer cmd, const glm::mat4& viewMatrix, 
                   const glm::mat4& projMatrix, const glm::vec3& cameraPos);
    
    /**
     * Mark pages - run GPU page marking pass
     */
    void markPages(VkCommandBuffer cmd, VkImageView gbufferDepth, 
                  VkImageView gbufferNormal);
    
    /**
     * Process GPU feedback - read requests from previous frame
     */
    void processFeedback(VkCommandBuffer cmd);
    
    /**
     * Allocate pages - process requests and allocate physical pages
     */
    void allocatePages();
    
    /**
     * Render shadows - render to allocated pages
     */
    void renderShadows(VkCommandBuffer cmd, 
                      const std::function<void(VkCommandBuffer, uint32_t vsmIndex, 
                                               const FVSMProjectionData&)>& renderFunc);
    
    /**
     * End frame - finalize and prepare for next frame
     */
    void endFrame(VkCommandBuffer cmd);
    
    // ========================================================================
    // RESOURCE ACCESS
    // ========================================================================
    
    VkImage getPhysicalPagePool() const { return physicalPagePool; }
    VkImageView getPhysicalPagePoolView() const { return physicalPagePoolView; }
    
    VkBuffer getPageTableBuffer() const { return pageTableBuffer; }
    VkDeviceAddress getPageTableAddress() const { return pageTableBufferAddress; }
    
    VkBuffer getProjectionDataBuffer() const { return projectionDataBuffer; }
    VkDeviceAddress getProjectionDataAddress() const { return projectionDataBufferAddress; }
    
    /**
     * Get sampling data for shader
     */
    struct SamplingData {
        VkImageView physicalPagePoolView;
        VkBuffer pageTableBuffer;
        VkBuffer projectionDataBuffer;
        VkSampler shadowSampler;
        uint32_t numVSMs;
    };
    SamplingData getSamplingData() const;
    
    // ========================================================================
    // STATISTICS
    // ========================================================================
    
    struct Stats {
        uint32_t totalVSMs;
        uint32_t totalPages;
        uint32_t allocatedPages;
        uint32_t requestedThisFrame;
        uint32_t allocatedThisFrame;
        uint32_t evictedThisFrame;
        uint32_t renderedThisFrame;
        uint32_t cachedPages;
        float pagePoolUtilization;
    };
    Stats getStats() const;
    
private:
    VulkanContext& context;
    
    // ========================================================================
    // PHYSICAL PAGE POOL
    // ========================================================================
    
    VkImage physicalPagePool = VK_NULL_HANDLE;
    VkDeviceMemory physicalPagePoolMemory = VK_NULL_HANDLE;
    VkImageView physicalPagePoolView = VK_NULL_HANDLE;
    std::vector<VkImageView> physicalPageSliceViews;  // Per-page views for rendering
    
    uint32_t maxPhysicalPages = 0;
    std::vector<bool> pageAllocated;
    std::vector<uint32_t> freePageList;
    std::vector<FPhysicalPageMeta> pageMetadata;
    
    // LRU tracking
    std::vector<uint32_t> lruList;
    
    // ========================================================================
    // PAGE TABLE
    // ========================================================================
    
    VkBuffer pageTableBuffer = VK_NULL_HANDLE;
    VkDeviceMemory pageTableMemory = VK_NULL_HANDLE;
    VkDeviceAddress pageTableBufferAddress = 0;
    void* pageTableMapped = nullptr;
    
    // ========================================================================
    // PROJECTION DATA
    // ========================================================================
    
    VkBuffer projectionDataBuffer = VK_NULL_HANDLE;
    VkDeviceMemory projectionDataMemory = VK_NULL_HANDLE;
    VkDeviceAddress projectionDataBufferAddress = 0;
    void* projectionDataMapped = nullptr;
    
    std::vector<FVSMProjectionData> projectionData;
    
    // ========================================================================
    // GPU FEEDBACK
    // ========================================================================
    
    VkBuffer requestBuffer = VK_NULL_HANDLE;
    VkDeviceMemory requestMemory = VK_NULL_HANDLE;
    
    VkBuffer requestReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory requestReadbackMemory = VK_NULL_HANDLE;
    void* requestReadbackMapped = nullptr;
    
    VkBuffer counterBuffer = VK_NULL_HANDLE;
    VkDeviceMemory counterMemory = VK_NULL_HANDLE;
    
    // ========================================================================
    // PIPELINES
    // ========================================================================
    
    VkPipeline markingPipeline = VK_NULL_HANDLE;
    VkPipelineLayout markingPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout markingDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet markingDescriptorSet = VK_NULL_HANDLE;
    
    VkPipeline coarseMarkingPipeline = VK_NULL_HANDLE;  // For fallback mips
    
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> pageFramebuffers;
    
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkSampler depthSampler = VK_NULL_HANDLE;
    
    // ========================================================================
    // LIGHTS AND VSMs
    // ========================================================================
    
    std::vector<VSMLightInfo> lights;
    std::vector<uint32_t> lightToVSMOffset;  // Starting VSM index for each light
    
    std::vector<std::unique_ptr<FDirectionalLightClipmap>> directionalClipmaps;
    
    // Cache entries per (vsmIndex, pageX, pageY, mipLevel)
    std::unordered_map<uint64_t, FVSMCacheEntry> pageCache;
    
    // ========================================================================
    // FRAME STATE
    // ========================================================================
    
    uint64_t currentFrame = 0;
    glm::vec3 lastCameraPos;
    glm::mat4 lastViewMatrix;
    glm::mat4 lastProjMatrix;
    
    // Per-frame stats
    uint32_t pagesRequestedThisFrame = 0;
    uint32_t pagesAllocatedThisFrame = 0;
    uint32_t pagesEvictedThisFrame = 0;
    uint32_t pagesRenderedThisFrame = 0;
    
    // ========================================================================
    // INTERNAL METHODS
    // ========================================================================
    
    void createPhysicalPagePool();
    void createPageTable();
    void createProjectionDataBuffer();
    void createRequestBuffers();
    void createPipelines();
    void createShadowRenderPass();
    void createSamplers();
    
    uint32_t allocatePage();
    void freePage(uint32_t pageIndex);
    void evictLRU(uint32_t count);
    void updateLRU(uint32_t pageIndex);
    
    void updateProjectionData();
    void updatePageTable(uint32_t vsmIndex, uint32_t pageX, uint32_t pageY, 
                        uint32_t mipLevel, uint32_t physicalPage);
    
    uint64_t makeCacheKey(uint32_t vsmIndex, uint32_t pageX, uint32_t pageY, uint32_t mipLevel) const;
    
    void processPageRequests(const std::vector<FGPUPageRequest>& requests);
    
    // Buffer helpers
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkBuffer& buffer,
                     VkDeviceMemory& memory);
    void createImage(uint32_t width, uint32_t height, uint32_t layers, VkFormat format,
                    VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
};

