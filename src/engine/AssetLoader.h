/**
 * AssetLoader.h
 * 
 * Runtime asset loading and streaming system.
 * Loads .sanic_mesh files and streams cluster pages on-demand.
 * 
 * Features:
 * - Async file I/O using background threads
 * - Page-based streaming for large assets
 * - LRU cache for loaded pages
 * - Priority-based loading (based on screen-space size)
 * - DirectStorage support on Windows (future)
 */

#pragma once

#include "SanicAssetFormat.h"
#include "VulkanContext.h"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <atomic>
#include <list>
#include <functional>

namespace Sanic {

// ============================================================================
// LOADED ASSET DATA
// ============================================================================

// Runtime representation of a loaded asset
struct LoadedAsset {
    AssetHeader header;
    
    // Geometry (always loaded)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    VkDeviceAddress vertexBufferAddress = 0;
    VkDeviceAddress indexBufferAddress = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    
    // Nanite data
    VkBuffer clusterBuffer = VK_NULL_HANDLE;
    VkBuffer hierarchyBuffer = VK_NULL_HANDLE;
    VkBuffer meshletBuffer = VK_NULL_HANDLE;
    VkBuffer meshletVerticesBuffer = VK_NULL_HANDLE;
    VkBuffer meshletTrianglesBuffer = VK_NULL_HANDLE;
    VkDeviceMemory naniteMemory = VK_NULL_HANDLE;
    VkDeviceAddress clusterBufferAddress = 0;
    VkDeviceAddress hierarchyBufferAddress = 0;
    VkDeviceAddress meshletBufferAddress = 0;
    uint32_t clusterCount = 0;
    uint32_t hierarchyNodeCount = 0;
    uint32_t meshletCount = 0;
    
    // Lumen data
    VkImage sdfVolume = VK_NULL_HANDLE;
    VkImageView sdfVolumeView = VK_NULL_HANDLE;
    VkDeviceMemory sdfMemory = VK_NULL_HANDLE;
    glm::ivec3 sdfResolution;
    float sdfVoxelSize = 0.0f;
    
    VkBuffer surfaceCardBuffer = VK_NULL_HANDLE;
    VkDeviceMemory surfaceCardMemory = VK_NULL_HANDLE;
    uint32_t surfaceCardCount = 0;
    
    // Page streaming state
    std::vector<StreamingPage> pageStates;
    uint32_t residentPageCount = 0;
    
    // Reference counting
    std::atomic<uint32_t> refCount{0};
    
    // Source info
    std::string filePath;
    uint64_t fileSize = 0;
    
    bool isFullyLoaded() const {
        return residentPageCount == pageStates.size();
    }
};

// ============================================================================
// LOADING REQUEST
// ============================================================================

enum class LoadPriority : uint8_t {
    Background = 0,
    Normal = 1,
    High = 2,
    Critical = 3      // Needed for current frame
};

struct LoadRequest {
    std::string filePath;
    LoadPriority priority = LoadPriority::Normal;
    bool loadGeometry = true;
    bool loadNanite = true;
    bool loadLumen = true;
    bool loadPhysics = false;
    
    // Callback when loading completes
    std::function<void(LoadedAsset*, bool success)> onComplete;
    
    // For page streaming
    uint32_t assetId = 0;
    std::vector<uint32_t> pagesToLoad;
};

// ============================================================================
// STREAMING CONFIGURATION
// ============================================================================

struct StreamingConfig {
    // Memory budget
    uint64_t maxGpuMemoryBytes = 512 * 1024 * 1024;  // 512MB for assets
    uint64_t maxCpuMemoryBytes = 256 * 1024 * 1024;  // 256MB staging
    
    // Streaming parameters
    uint32_t maxConcurrentLoads = 4;
    uint32_t maxPagesPerFrame = 8;
    uint32_t pageRetentionFrames = 60;   // Frames before evicting unused pages
    
    // I/O settings
    uint32_t ioThreadCount = 2;
    uint32_t readBufferSize = 256 * 1024;  // 256KB read buffer
    bool useDirectStorage = false;
};

// ============================================================================
// ASSET LOADER
// ============================================================================

class AssetLoader {
public:
    AssetLoader();
    ~AssetLoader();
    
    // Initialize with Vulkan context
    bool initialize(VulkanContext* context, const StreamingConfig& config = {});
    void shutdown();
    
    // Synchronous loading (blocks until complete)
    LoadedAsset* loadSync(const std::string& filePath);
    
    // Asynchronous loading
    void loadAsync(const LoadRequest& request);
    
    // Unload an asset
    void unload(LoadedAsset* asset);
    
    // Page streaming
    void requestPages(LoadedAsset* asset, const std::vector<uint32_t>& pageIndices, LoadPriority priority);
    void updateStreaming(float deltaTime);
    
    // Cache management
    void setMemoryBudget(uint64_t gpuBytes, uint64_t cpuBytes);
    void trimCache(uint64_t targetSize);
    void clearCache();
    
    // Get loaded asset by path (returns nullptr if not loaded)
    LoadedAsset* getAsset(const std::string& filePath);
    
    // Statistics
    struct Stats {
        uint64_t gpuMemoryUsed;
        uint64_t cpuMemoryUsed;
        uint32_t assetsLoaded;
        uint32_t pagesResident;
        uint32_t pagesStreaming;
        uint32_t loadRequestsPending;
        float averageLoadTimeMs;
    };
    Stats getStats() const;
    
private:
    // Internal loading functions
    bool loadHeader(const std::string& filePath, AssetHeader& outHeader);
    bool loadGeometrySection(LoadedAsset* asset, const std::vector<uint8_t>& data);
    bool loadNaniteSection(LoadedAsset* asset, const std::vector<uint8_t>& data);
    bool loadLumenSection(LoadedAsset* asset, const std::vector<uint8_t>& data);
    bool loadPhysicsSection(LoadedAsset* asset, const std::vector<uint8_t>& data);
    
    // GPU buffer creation
    VkBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory& memory);
    void uploadToBuffer(VkBuffer buffer, const void* data, VkDeviceSize size);
    VkImage createImage3D(uint32_t width, uint32_t height, uint32_t depth, VkFormat format, VkDeviceMemory& memory);
    void uploadToImage3D(VkImage image, const void* data, uint32_t width, uint32_t height, uint32_t depth, VkFormat format);
    
    // Staging buffer management
    struct StagingBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
        bool inUse = false;
    };
    StagingBuffer* acquireStagingBuffer(VkDeviceSize size);
    void releaseStagingBuffer(StagingBuffer* buffer);
    
    // I/O thread worker
    void ioThreadFunc();
    void processLoadRequest(const LoadRequest& request);
    
    // LRU cache management
    void touchPage(LoadedAsset* asset, uint32_t pageIndex);
    void evictLRUPages(uint32_t count);
    
    VulkanContext* context_ = nullptr;
    StreamingConfig config_;
    bool initialized_ = false;
    
    // Asset cache
    std::unordered_map<std::string, std::unique_ptr<LoadedAsset>> assetCache_;
    std::mutex cacheMutex_;
    
    // Load request queue
    std::priority_queue<LoadRequest, std::vector<LoadRequest>, 
        std::function<bool(const LoadRequest&, const LoadRequest&)>> loadQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // I/O threads
    std::vector<std::thread> ioThreads_;
    std::atomic<bool> shutdownRequested_{false};
    
    // Staging buffers
    std::vector<StagingBuffer> stagingBuffers_;
    std::mutex stagingMutex_;
    
    // Command pool for transfers
    VkCommandPool transferCommandPool_ = VK_NULL_HANDLE;
    VkQueue transferQueue_ = VK_NULL_HANDLE;
    
    // LRU tracking
    struct PageLRU {
        LoadedAsset* asset;
        uint32_t pageIndex;
        uint32_t frameLastUsed;
    };
    std::list<PageLRU> lruList_;
    std::unordered_map<uint64_t, std::list<PageLRU>::iterator> lruMap_;
    std::mutex lruMutex_;
    uint32_t currentFrame_ = 0;
    
    // Statistics
    mutable std::mutex statsMutex_;
    uint64_t gpuMemoryUsed_ = 0;
    uint64_t cpuMemoryUsed_ = 0;
    std::atomic<uint32_t> pendingRequests_{0};
    double totalLoadTime_ = 0.0;
    uint32_t loadCount_ = 0;
};

// ============================================================================
// CONVENIENCE FUNCTIONS
// ============================================================================

// Check if a file is a valid .sanic_mesh
bool isValidSanicMesh(const std::string& filePath);

// Get asset info without fully loading
bool getAssetInfo(const std::string& filePath, AssetHeader& outHeader);

} // namespace Sanic
