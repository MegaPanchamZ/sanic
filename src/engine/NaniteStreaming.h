/**
 * NaniteStreaming.h
 * 
 * Unreal Engine Nanite-style geometry streaming system.
 * Implements:
 * - GPU-driven page request generation
 * - LRU-based page pool management
 * - Async I/O for page loading
 * - Fixup system for hierarchy patching
 * 
 * Based on Unreal Engine's FStreamingManager.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <string>

class VulkanContext;

// ============================================================================
// STREAMING CONSTANTS
// ============================================================================

namespace NaniteStreaming {
    // Page configuration (matching Unreal)
    constexpr uint32_t GPU_PAGE_SIZE = 128 * 1024;      // 128KB GPU page
    constexpr uint32_t STREAMING_PAGE_SIZE = 128 * 1024; // 128KB streaming unit
    constexpr uint32_t DEFAULT_POOL_SIZE_MB = 512;       // 512MB default pool
    constexpr uint32_t MAX_PENDING_PAGES = 256;          // Max concurrent page loads
    constexpr uint32_t MAX_PAGES_PER_FRAME = 64;         // Max pages to process per frame
    constexpr uint32_t ROOT_PAGE_SIZE = 64 * 1024;       // 64KB root pages
    
    // Priority thresholds
    constexpr float PRIORITY_CRITICAL = 1.0f;    // Must load immediately
    constexpr float PRIORITY_HIGH = 0.75f;       // Load soon
    constexpr float PRIORITY_NORMAL = 0.5f;      // Standard priority
    constexpr float PRIORITY_LOW = 0.25f;        // Can wait
    constexpr float PRIORITY_PREFETCH = 0.1f;    // Speculative load
}

// ============================================================================
// PAGE IDENTIFICATION
// ============================================================================

/**
 * Unique identifier for a streaming page
 * Combines resource ID and page index
 */
struct FPageKey {
    uint32_t resourceId;  // Which mesh resource
    uint32_t pageIndex;   // Which page within resource
    
    bool operator==(const FPageKey& other) const {
        return resourceId == other.resourceId && pageIndex == other.pageIndex;
    }
    
    uint64_t toUint64() const {
        return (static_cast<uint64_t>(resourceId) << 32) | pageIndex;
    }
    
    static FPageKey fromUint64(uint64_t key) {
        FPageKey result;
        result.resourceId = static_cast<uint32_t>(key >> 32);
        result.pageIndex = static_cast<uint32_t>(key & 0xFFFFFFFF);
        return result;
    }
};

struct FPageKeyHash {
    size_t operator()(const FPageKey& key) const {
        return std::hash<uint64_t>()(key.toUint64());
    }
};

// ============================================================================
// PAGE DATA STRUCTURES
// ============================================================================

/**
 * State of a streaming page
 */
enum class EPageState : uint8_t {
    NotLoaded,      // Page not in GPU memory
    Requested,      // Request submitted, waiting for I/O
    Loading,        // I/O in progress
    Uploading,      // Uploading to GPU
    Resident,       // Page is in GPU memory
    PendingEvict,   // Marked for eviction
};

/**
 * GPU-side page header (at start of each page)
 */
struct alignas(16) FPageHeader {
    uint32_t numClusters;       // Number of clusters in this page
    uint32_t clusterDataOffset; // Offset to cluster data
    uint32_t hierarchyOffset;   // Offset to hierarchy patch data
    uint32_t numFixups;         // Number of hierarchy fixups needed
};

/**
 * Hierarchy fixup entry - patches parent references when page is loaded/unloaded
 */
struct FHierarchyFixup {
    uint32_t hierarchyNodeIndex;  // Node to patch
    uint32_t childSlotIndex;      // Which child slot
    uint32_t targetClusterStart;  // New cluster start (or 0xFFFFFFFF if unloading)
    uint32_t numClusters;         // Number of clusters
};

/**
 * Page request from GPU traversal
 */
struct FPageRequest {
    FPageKey key;
    float priority;         // Higher = more important
    uint32_t frameRequested;
    uint32_t screenPixels;  // Approximate screen coverage
    
    bool operator<(const FPageRequest& other) const {
        return priority < other.priority;  // Max-heap
    }
};

/**
 * Pending page load
 */
struct FPendingPage {
    FPageKey key;
    std::vector<uint8_t> cpuData;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    uint32_t gpuPageIndex = UINT32_MAX;
    float priority;
    std::atomic<EPageState> state{EPageState::NotLoaded};
};

/**
 * Resident page in GPU pool
 */
struct FResidentPage {
    FPageKey key;
    uint32_t gpuPageIndex;      // Index in physical page pool
    uint32_t lruPosition;       // Position in LRU list
    uint64_t lastUsedFrame;
    uint32_t numClusters;
    float priority;
    std::vector<FHierarchyFixup> fixups;  // Stored for unload
};

// ============================================================================
// STREAMING RESOURCE
// ============================================================================

/**
 * Per-resource streaming data
 */
class FStreamingResource {
public:
    uint32_t resourceId;
    std::string sourcePath;
    
    // Page table
    uint32_t numPages;
    std::vector<uint64_t> pageOffsets;  // File offsets for each page
    std::vector<uint32_t> pageSizes;    // Compressed sizes
    
    // Root pages (always resident)
    uint32_t numRootPages;
    std::vector<uint32_t> rootPageIndices;
    
    // Hierarchy info
    uint32_t numHierarchyNodes;
    uint32_t numClusters;
    
    // GPU buffer references
    VkBuffer hierarchyBuffer = VK_NULL_HANDLE;
    VkBuffer clusterBuffer = VK_NULL_HANDLE;
    VkDeviceAddress hierarchyBufferAddress = 0;
    VkDeviceAddress clusterBufferAddress = 0;
};

// ============================================================================
// STREAMING MANAGER
// ============================================================================

/**
 * NaniteStreamingManager - Manages geometry page streaming
 * 
 * Responsibilities:
 * - Process GPU page requests
 * - Manage physical page pool
 * - Handle async I/O
 * - Apply hierarchy fixups
 * - LRU eviction
 */
class NaniteStreamingManager {
public:
    NaniteStreamingManager(VulkanContext& context);
    ~NaniteStreamingManager();
    
    /**
     * Initialize streaming system
     * @param poolSizeMB Physical page pool size in MB
     */
    void initialize(uint32_t poolSizeMB = NaniteStreaming::DEFAULT_POOL_SIZE_MB);
    void shutdown();
    
    /**
     * Register a streaming resource
     * @param path Path to the streaming data file
     * @return Resource ID
     */
    uint32_t registerResource(const std::string& path);
    void unregisterResource(uint32_t resourceId);
    
    /**
     * Begin frame - read GPU requests from previous frame
     */
    void beginFrame(VkCommandBuffer cmd, uint64_t frameNumber);
    
    /**
     * Process streaming - called each frame
     * Handles I/O, uploads, and fixups
     */
    void update(VkCommandBuffer cmd);
    
    /**
     * End frame - prepare request buffer for next frame
     */
    void endFrame(VkCommandBuffer cmd);
    
    // ========================================================================
    // RESOURCE ACCESS
    // ========================================================================
    
    VkBuffer getPagePoolBuffer() const { return pagePoolBuffer; }
    VkDeviceAddress getPagePoolAddress() const { return pagePoolBufferAddress; }
    
    VkBuffer getPageTableBuffer() const { return pageTableBuffer; }
    VkDeviceAddress getPageTableAddress() const { return pageTableBufferAddress; }
    
    VkBuffer getRequestBuffer() const { return requestBuffer; }
    
    FStreamingResource* getResource(uint32_t resourceId);
    
    // ========================================================================
    // STATISTICS
    // ========================================================================
    
    struct Stats {
        uint32_t totalPages;
        uint32_t residentPages;
        uint32_t pendingPages;
        uint32_t evictedThisFrame;
        uint32_t loadedThisFrame;
        uint64_t totalBytesStreamed;
        uint64_t poolSizeBytes;
        float poolUtilization;
    };
    
    Stats getStats() const;
    
private:
    VulkanContext& context;
    
    // ========================================================================
    // PHYSICAL PAGE POOL
    // ========================================================================
    
    VkBuffer pagePoolBuffer = VK_NULL_HANDLE;
    VkDeviceMemory pagePoolMemory = VK_NULL_HANDLE;
    VkDeviceAddress pagePoolBufferAddress = 0;
    
    uint32_t poolSizePages;
    std::vector<bool> pageAllocated;
    std::vector<uint32_t> freePageList;
    
    // LRU tracking
    std::vector<FResidentPage*> lruList;
    std::unordered_map<FPageKey, std::unique_ptr<FResidentPage>, FPageKeyHash> residentPages;
    
    // ========================================================================
    // PAGE TABLE
    // ========================================================================
    
    // GPU page table: maps (resourceId, pageIndex) -> gpuPageIndex
    VkBuffer pageTableBuffer = VK_NULL_HANDLE;
    VkDeviceMemory pageTableMemory = VK_NULL_HANDLE;
    VkDeviceAddress pageTableBufferAddress = 0;
    void* pageTableMapped = nullptr;
    
    // ========================================================================
    // REQUEST HANDLING
    // ========================================================================
    
    // GPU -> CPU request buffer
    VkBuffer requestBuffer = VK_NULL_HANDLE;
    VkDeviceMemory requestBufferMemory = VK_NULL_HANDLE;
    void* requestBufferMapped = nullptr;
    
    // Request readback buffer
    VkBuffer requestReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory requestReadbackMemory = VK_NULL_HANDLE;
    void* requestReadbackMapped = nullptr;
    
    // Pending requests (priority queue)
    std::priority_queue<FPageRequest> pendingRequests;
    std::unordered_set<uint64_t> requestedPages;  // Dedup
    
    // ========================================================================
    // ASYNC I/O
    // ========================================================================
    
    std::vector<std::unique_ptr<FPendingPage>> pendingPages;
    std::mutex pendingMutex;
    std::thread ioThread;
    std::atomic<bool> ioThreadRunning{false};
    std::condition_variable ioCondition;
    
    // Staging buffer pool
    struct StagingBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        bool inUse = false;
    };
    std::vector<StagingBuffer> stagingBufferPool;
    
    // ========================================================================
    // RESOURCES
    // ========================================================================
    
    std::unordered_map<uint32_t, std::unique_ptr<FStreamingResource>> resources;
    uint32_t nextResourceId = 1;
    
    // ========================================================================
    // FRAME STATE
    // ========================================================================
    
    uint64_t currentFrame = 0;
    uint32_t pagesLoadedThisFrame = 0;
    uint32_t pagesEvictedThisFrame = 0;
    uint64_t totalBytesStreamed = 0;
    
    // ========================================================================
    // INTERNAL METHODS
    // ========================================================================
    
    void createPagePool(uint32_t numPages);
    void createPageTable();
    void createRequestBuffers();
    void createStagingBufferPool();
    
    void processGPURequests();
    void submitIORequests();
    void processCompletedLoads(VkCommandBuffer cmd);
    void evictPages(uint32_t numToEvict);
    
    uint32_t allocatePage();
    void freePage(uint32_t pageIndex);
    
    void updatePageTable(FPageKey key, uint32_t gpuPageIndex);
    void applyFixups(VkCommandBuffer cmd, FResidentPage* page, bool isLoading);
    
    void ioThreadFunc();
    void loadPageFromDisk(FPendingPage* page);
    
    StagingBuffer* acquireStagingBuffer();
    void releaseStagingBuffer(StagingBuffer* buffer);
    
    void updateLRU(FResidentPage* page);
    FResidentPage* getLRUPage();
    
    // Buffer helpers
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, 
                     VkMemoryPropertyFlags properties, VkBuffer& buffer, 
                     VkDeviceMemory& memory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

// ============================================================================
// GPU REQUEST STRUCTURES
// ============================================================================

/**
 * GPU-side request structure (written by culling shader)
 */
struct alignas(16) FGPUPageRequest {
    uint32_t resourceId;
    uint32_t pageIndex;
    float priority;
    uint32_t screenPixels;
};

/**
 * GPU request buffer header
 */
struct alignas(16) FGPURequestHeader {
    uint32_t numRequests;
    uint32_t maxRequests;
    uint32_t overflow;      // Set if buffer overflowed
    uint32_t frameNumber;
};

