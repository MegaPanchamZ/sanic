#pragma once

/**
 * VirtualTextureSystem.h
 * 
 * Runtime Virtual Texture (RVT) streaming system.
 * Based on UE5's virtual texturing implementation.
 * 
 * Features:
 * - Streaming virtual textures for large landscapes
 * - Page-based caching with LRU eviction
 * - Feedback buffer for page requests
 * - Transcoding from various formats
 * - Mip-chain support
 */

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace Kinetic {

// Forward declarations
class VulkanRenderer;
class ComputePipeline;
class GraphicsPipeline;
class DescriptorSet;
class Buffer;
class Image;

/**
 * Virtual texture page identifier
 */
struct VTPageId {
    uint32_t vtIndex = 0;    // Virtual texture index
    uint32_t mipLevel = 0;
    uint32_t pageX = 0;
    uint32_t pageY = 0;
    
    bool operator==(const VTPageId& other) const {
        return vtIndex == other.vtIndex && mipLevel == other.mipLevel &&
               pageX == other.pageX && pageY == other.pageY;
    }
};

/**
 * Hash function for VTPageId
 */
struct VTPageIdHash {
    size_t operator()(const VTPageId& id) const {
        return std::hash<uint64_t>()(
            (static_cast<uint64_t>(id.vtIndex) << 48) |
            (static_cast<uint64_t>(id.mipLevel) << 32) |
            (static_cast<uint64_t>(id.pageX) << 16) |
            static_cast<uint64_t>(id.pageY)
        );
    }
};

/**
 * Physical page in the cache
 */
struct PhysicalPage {
    uint32_t physicalX = 0;
    uint32_t physicalY = 0;
    VTPageId virtualPage;
    uint64_t lastUsedFrame = 0;
    bool valid = false;
};

/**
 * Virtual texture configuration
 */
struct VirtualTextureConfig {
    uint32_t virtualWidth = 16384;    // Total virtual texture size
    uint32_t virtualHeight = 16384;
    uint32_t pageSize = 128;          // Physical page size (without padding)
    uint32_t pagePadding = 4;         // Border padding for filtering
    uint32_t maxMipLevels = 8;
    
    // Physical cache size
    uint32_t physicalCacheWidth = 4096;
    uint32_t physicalCacheHeight = 4096;
    
    // Feedback buffer resolution (usually lower than render resolution)
    uint32_t feedbackWidth = 256;
    uint32_t feedbackHeight = 256;
    
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

/**
 * Page request from feedback buffer
 */
struct PageRequest {
    VTPageId pageId;
    uint32_t priority = 0;  // Higher = more urgent
};

/**
 * Page data source interface
 */
class IVTPageProvider {
public:
    virtual ~IVTPageProvider() = default;
    
    // Load page data (called on streaming thread)
    virtual bool loadPage(const VTPageId& pageId, void* outData, size_t dataSize) = 0;
    
    // Get page data size
    virtual size_t getPageDataSize() const = 0;
    
    // Check if page exists
    virtual bool pageExists(const VTPageId& pageId) const = 0;
};

/**
 * File-based page provider (loads from disk tiles)
 */
class FileVTPageProvider : public IVTPageProvider {
public:
    FileVTPageProvider(const std::string& basePath, const VirtualTextureConfig& config);
    
    bool loadPage(const VTPageId& pageId, void* outData, size_t dataSize) override;
    size_t getPageDataSize() const override;
    bool pageExists(const VTPageId& pageId) const override;
    
private:
    std::string m_basePath;
    VirtualTextureConfig m_config;
};

/**
 * Procedural page provider (generates pages on-the-fly)
 */
class ProceduralVTPageProvider : public IVTPageProvider {
public:
    using GeneratorFunc = std::function<void(const VTPageId&, void*, size_t)>;
    
    ProceduralVTPageProvider(GeneratorFunc generator, const VirtualTextureConfig& config);
    
    bool loadPage(const VTPageId& pageId, void* outData, size_t dataSize) override;
    size_t getPageDataSize() const override;
    bool pageExists(const VTPageId& pageId) const override;
    
private:
    GeneratorFunc m_generator;
    VirtualTextureConfig m_config;
};

/**
 * Virtual texture instance
 */
struct VirtualTexture {
    uint32_t id = 0;
    VirtualTextureConfig config;
    std::unique_ptr<IVTPageProvider> pageProvider;
    
    // World mapping
    glm::vec2 worldOrigin = glm::vec2(0.0f);
    glm::vec2 worldSize = glm::vec2(1000.0f);
    
    bool enabled = true;
};

/**
 * Virtual texture streaming system
 */
class VirtualTextureSystem {
public:
    VirtualTextureSystem();
    ~VirtualTextureSystem();
    
    // Initialization
    bool initialize(VulkanRenderer* renderer, const VirtualTextureConfig& defaultConfig);
    void shutdown();
    
    // Virtual texture management
    uint32_t createVirtualTexture(const VirtualTextureConfig& config,
                                   std::unique_ptr<IVTPageProvider> pageProvider);
    void destroyVirtualTexture(uint32_t id);
    VirtualTexture* getVirtualTexture(uint32_t id);
    
    // World mapping
    void setWorldMapping(uint32_t vtId, const glm::vec2& origin, const glm::vec2& size);
    
    // Per-frame update
    void beginFrame(uint64_t frameNumber);
    void processRequests(VkCommandBuffer cmd);  // Process feedback and upload pages
    void endFrame();
    
    // Feedback rendering
    void renderFeedback(VkCommandBuffer cmd,
                        VkRenderPass renderPass,
                        const glm::mat4& viewProjection,
                        const glm::vec3& cameraPos);
    
    // Shader resources
    VkImageView getPhysicalCacheView() const;
    VkImageView getPageTableView() const;
    VkSampler getPhysicalCacheSampler() const;
    
    // For shaders to sample
    struct VTShaderParams {
        glm::vec2 virtualSize;
        glm::vec2 physicalPageSize;
        glm::vec2 tilePadding;
        float maxMipLevel;
        float mipBias;
        uint32_t vtIndex;
        glm::vec2 worldOrigin;
        glm::vec2 worldSize;
    };
    VTShaderParams getShaderParams(uint32_t vtId) const;
    
    // Statistics
    struct Stats {
        uint32_t requestedPages = 0;
        uint32_t uploadedPages = 0;
        uint32_t evictedPages = 0;
        uint32_t cacheHits = 0;
        uint32_t cacheMisses = 0;
        float cacheUtilization = 0.0f;
    };
    const Stats& getStats() const { return m_stats; }
    
    // Debug
    void drawDebugUI();
    void visualizePageTable(VkCommandBuffer cmd, VkImageView output);
    
private:
    void createPhysicalCache();
    void createPageTable();
    void createFeedbackBuffer();
    void createPipelines();
    void createDescriptorSets();
    
    void readFeedbackBuffer();
    void processPageRequests();
    void uploadPendingPages(VkCommandBuffer cmd);
    void updatePageTable(VkCommandBuffer cmd);
    
    PhysicalPage* allocatePage();
    void evictLRUPage();
    
    void streamingThreadFunc();
    
    VulkanRenderer* m_renderer = nullptr;
    VirtualTextureConfig m_defaultConfig;
    
    // Virtual textures
    std::vector<std::unique_ptr<VirtualTexture>> m_virtualTextures;
    uint32_t m_nextVTId = 1;
    
    // Physical cache (texture atlas)
    std::unique_ptr<Image> m_physicalCache;
    std::vector<PhysicalPage> m_physicalPages;
    uint32_t m_physicalPagesX = 0;
    uint32_t m_physicalPagesY = 0;
    std::vector<uint32_t> m_freePages;  // Indices of free pages
    
    VkSampler m_cacheSampler = VK_NULL_HANDLE;
    
    // Page table (indirection texture)
    std::unique_ptr<Image> m_pageTable;  // R16G16B16A16_UINT format
    std::unique_ptr<Buffer> m_pageTableStaging;
    
    // Feedback buffer
    std::unique_ptr<Image> m_feedbackBuffer;  // R16G16B16A16_UINT format
    std::unique_ptr<Buffer> m_feedbackReadback;
    
    // Mapping from virtual pages to physical pages
    std::unordered_map<VTPageId, PhysicalPage*, VTPageIdHash> m_pageMapping;
    
    // Page request queue
    std::queue<PageRequest> m_pendingRequests;
    std::mutex m_requestMutex;
    
    // Loaded pages waiting for GPU upload
    struct LoadedPage {
        VTPageId pageId;
        std::vector<uint8_t> data;
    };
    std::queue<LoadedPage> m_loadedPages;
    std::mutex m_loadedMutex;
    
    // Staging buffer for page uploads
    std::unique_ptr<Buffer> m_uploadStaging;
    
    // Streaming thread
    std::thread m_streamingThread;
    std::atomic<bool> m_streamingThreadRunning{false};
    
    // Pipelines
    std::unique_ptr<GraphicsPipeline> m_feedbackPipeline;
    std::unique_ptr<ComputePipeline> m_pageTableUpdatePipeline;
    
    // Descriptor sets
    std::unique_ptr<DescriptorSet> m_feedbackDescSet;
    std::unique_ptr<DescriptorSet> m_vtSampleDescSet;
    
    // Frame tracking
    uint64_t m_currentFrame = 0;
    
    // Statistics
    Stats m_stats;
};

/**
 * Helper to create landscape virtual texture
 */
class LandscapeVirtualTexture {
public:
    LandscapeVirtualTexture();
    
    bool initialize(VirtualTextureSystem* vtSystem,
                    const glm::vec2& worldOrigin,
                    const glm::vec2& worldSize,
                    uint32_t resolution);
    
    void shutdown();
    
    // Set source layers (will be composited into VT)
    void setHeightmap(Image* heightmap);
    void setWeightmap(Image* weightmap);
    void addMaterialLayer(uint32_t index, Image* baseColor, Image* normal, Image* orm);
    
    // Force regeneration of a region
    void invalidateRegion(const glm::vec2& min, const glm::vec2& max);
    
    uint32_t getVirtualTextureId() const { return m_vtId; }
    
private:
    VirtualTextureSystem* m_vtSystem = nullptr;
    uint32_t m_vtId = 0;
    
    // Source textures for compositing
    Image* m_heightmap = nullptr;
    Image* m_weightmap = nullptr;
    
    struct MaterialLayer {
        Image* baseColor = nullptr;
        Image* normal = nullptr;
        Image* orm = nullptr;
    };
    std::vector<MaterialLayer> m_materialLayers;
};

} // namespace Kinetic
