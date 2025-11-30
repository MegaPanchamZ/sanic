/**
 * TextureStreamer.h
 * 
 * Virtual texture streaming system for efficient GPU memory usage.
 * Implements mip-level streaming with GPU feedback buffer analysis.
 * 
 * Key Features:
 * - Feedback-driven mip streaming
 * - Async texture loading with priority queue
 * - Memory budget management
 * - GPU residency tracking
 * - Tile-based virtual texturing support
 * 
 * Architecture:
 * 1. GPU writes requested mip levels to feedback buffer
 * 2. CPU reads feedback and schedules async loads
 * 3. Streaming threads load and upload textures
 * 4. GPU residency map updated for shader access
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
#include <condition_variable>
#include <functional>
#include <string>
#include <memory>

namespace Sanic {

// Forward declarations (VulkanContext is in global namespace)
}  // namespace Sanic temporarily closed

class VulkanContext;  // Forward declare in global namespace

namespace Sanic {  // Reopen namespace

/**
 * Mip level residency status
 */
enum class MipResidency : uint8_t {
    NotLoaded = 0,      // Mip not in GPU memory
    Loading = 1,        // Currently being loaded
    Resident = 2,       // Fully loaded and usable
    PendingEvict = 3    // Marked for eviction
};

/**
 * Streaming priority (higher = more important)
 */
enum class StreamPriority : uint8_t {
    Low = 0,            // Background loading
    Normal = 1,         // Standard priority
    High = 2,           // Visible geometry
    Critical = 3        // On-screen, low mip
};

/**
 * Texture streaming request
 */
struct StreamRequest {
    uint32_t textureId;     // Texture handle
    uint32_t mipLevel;      // Requested mip level
    StreamPriority priority;
    float screenCoverage;   // Approximate screen coverage (for prioritization)
    uint64_t frameRequested;
    
    bool operator<(const StreamRequest& other) const {
        // Higher priority first, then larger screen coverage
        if (priority != other.priority) return priority < other.priority;
        return screenCoverage < other.screenCoverage;
    }
};

/**
 * GPU feedback buffer entry (matches shader output)
 */
struct alignas(4) FeedbackEntry {
    uint16_t textureId;     // Which texture
    uint8_t requestedMip;   // Desired mip level
    uint8_t padding;
};

/**
 * Per-texture streaming state
 */
struct TextureStreamState {
    std::string path;                           // Source file path
    VkImage image = VK_NULL_HANDLE;             // GPU image
    VkImageView view = VK_NULL_HANDLE;          // Full image view
    VkDeviceMemory memory = VK_NULL_HANDLE;     // GPU memory
    
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    
    std::vector<MipResidency> mipResidency;     // Per-mip residency status
    uint32_t lowestResidentMip = UINT32_MAX;    // Lowest (highest quality) resident mip
    uint64_t lastAccessFrame = 0;               // For LRU eviction
    uint64_t memoryUsage = 0;                   // Bytes used on GPU
    
    bool isFullyResident() const {
        return lowestResidentMip == 0;
    }
};

/**
 * Streaming configuration
 */
struct StreamingConfig {
    // Memory budget
    uint64_t gpuMemoryBudget = 512 * 1024 * 1024;   // 512 MB default
    uint64_t stagingBufferSize = 64 * 1024 * 1024;  // 64 MB staging
    
    // Feedback buffer
    uint32_t feedbackBufferSize = 1024 * 1024;      // 1M entries
    uint32_t feedbackDownsample = 4;                // Sample every Nth pixel
    
    // Streaming behavior
    uint32_t maxConcurrentLoads = 4;
    uint32_t mipsToPreload = 2;                     // Always keep N lowest mips resident
    uint32_t framesBeforeEvict = 120;               // 2 seconds at 60fps
    float priorityBoostOnScreen = 2.0f;
    
    // Quality
    uint32_t maxAnisotropy = 16;
    bool generateMipmaps = true;
};

/**
 * Virtual texture streaming system
 */
class TextureStreamer {
public:
    TextureStreamer() = default;
    ~TextureStreamer();
    
    // Non-copyable
    TextureStreamer(const TextureStreamer&) = delete;
    TextureStreamer& operator=(const TextureStreamer&) = delete;
    
    /**
     * Initialize the streaming system
     */
    bool initialize(VulkanContext* context, const StreamingConfig& config = {});
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Register a texture for streaming
     * Returns texture ID for bindless access
     */
    uint32_t registerTexture(const std::string& path);
    
    /**
     * Unregister a texture
     */
    void unregisterTexture(uint32_t textureId);
    
    /**
     * Begin frame - reset feedback collection
     */
    void beginFrame(uint64_t frameNumber);
    
    /**
     * Process feedback buffer and update streaming
     * Call after rendering, before present
     */
    void update(VkCommandBuffer cmd);
    
    /**
     * Get feedback buffer for shader binding
     */
    VkBuffer getFeedbackBuffer() const { return feedbackBuffer_; }
    VkDeviceAddress getFeedbackBufferAddress() const { return feedbackBufferAddress_; }
    
    /**
     * Get residency buffer for shader binding
     * Contains per-texture lowest resident mip level
     */
    VkBuffer getResidencyBuffer() const { return residencyBuffer_; }
    VkDeviceAddress getResidencyBufferAddress() const { return residencyBufferAddress_; }
    
    /**
     * Get sampler for streaming textures
     */
    VkSampler getSampler() const { return sampler_; }
    
    /**
     * Get image view for a texture
     */
    VkImageView getTextureView(uint32_t textureId) const;
    
    /**
     * Get current memory usage
     */
    uint64_t getCurrentMemoryUsage() const { return currentMemoryUsage_.load(); }
    uint64_t getMemoryBudget() const { return config_.gpuMemoryBudget; }
    
    /**
     * Get streaming statistics
     */
    struct Statistics {
        uint64_t texturesRegistered;
        uint64_t texturesFullyResident;
        uint64_t pendingRequests;
        uint64_t loadsThisFrame;
        uint64_t evictionsThisFrame;
        uint64_t gpuMemoryUsed;
        uint64_t gpuMemoryBudget;
    };
    Statistics getStatistics() const;
    
    /**
     * Force-load a texture to specific mip level
     * Useful for UI textures or skyboxes
     */
    void forceLoad(uint32_t textureId, uint32_t targetMip = 0);
    
    /**
     * Request immediate eviction to free memory
     */
    void requestEviction(uint64_t bytesToFree);
    
private:
    // Internal methods
    bool createFeedbackBuffer();
    bool createResidencyBuffer();
    bool createStagingBuffer();
    bool createSampler();
    
    void processFeedback();
    void processRequestQueue();
    void performEviction();
    void updateResidencyBuffer(VkCommandBuffer cmd);
    
    void loadMipLevel(uint32_t textureId, uint32_t mipLevel);
    void uploadMipLevel(uint32_t textureId, uint32_t mipLevel, const void* data, size_t size);
    void createGPUImage(uint32_t textureId, TextureStreamState& state);
    
    void streamingThreadFunc();
    
    // Context
    VulkanContext* context_ = nullptr;
    StreamingConfig config_;
    
    // Feedback buffer (GPU writes, CPU reads)
    VkBuffer feedbackBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory feedbackMemory_ = VK_NULL_HANDLE;
    VkDeviceAddress feedbackBufferAddress_ = 0;
    void* feedbackMapped_ = nullptr;
    
    // Feedback counter buffer
    VkBuffer feedbackCounterBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory feedbackCounterMemory_ = VK_NULL_HANDLE;
    
    // Residency buffer (CPU writes, GPU reads)
    VkBuffer residencyBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory residencyMemory_ = VK_NULL_HANDLE;
    VkDeviceAddress residencyBufferAddress_ = 0;
    void* residencyMapped_ = nullptr;
    
    // Staging buffer for uploads
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    void* stagingMapped_ = nullptr;
    std::atomic<uint64_t> stagingOffset_{0};
    
    // Sampler
    VkSampler sampler_ = VK_NULL_HANDLE;
    
    // Texture storage
    std::unordered_map<uint32_t, TextureStreamState> textures_;
    std::mutex texturesMutex_;
    uint32_t nextTextureId_ = 1;
    
    // Request queue
    std::priority_queue<StreamRequest> requestQueue_;
    std::mutex requestMutex_;
    
    // Pending loads (being processed by streaming thread)
    std::unordered_set<uint64_t> pendingLoads_; // (textureId << 32) | mipLevel
    std::mutex pendingMutex_;
    
    // Memory tracking
    std::atomic<uint64_t> currentMemoryUsage_{0};
    
    // Streaming thread
    std::vector<std::thread> streamingThreads_;
    std::atomic<bool> shutdownRequested_{false};
    std::condition_variable streamingCondition_;
    std::mutex streamingMutex_;
    
    // Frame tracking
    uint64_t currentFrame_ = 0;
    
    // Statistics
    std::atomic<uint64_t> loadsThisFrame_{0};
    std::atomic<uint64_t> evictionsThisFrame_{0};
    
    bool initialized_ = false;
};

/**
 * Feedback writing shader code (GLSL snippet for inclusion)
 * 
 * Usage in material evaluation shader:
 * 
 * layout(buffer_reference, std430) buffer FeedbackBuffer {
 *     uint counter;
 *     FeedbackEntry entries[];
 * };
 * 
 * void writeFeedback(uint textureId, float mipLevel) {
 *     uint idx = atomicAdd(feedbackBuffer.counter, 1);
 *     if (idx < maxFeedbackEntries) {
 *         feedbackBuffer.entries[idx].textureId = uint16_t(textureId);
 *         feedbackBuffer.entries[idx].requestedMip = uint8_t(mipLevel);
 *     }
 * }
 */

} // namespace Sanic
