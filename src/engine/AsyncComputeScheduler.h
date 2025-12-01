/**
 * AsyncComputeScheduler.h
 * 
 * Async compute scheduling system for overlapping GPU work.
 * Based on Unreal Engine 5's async compute pipeline architecture.
 * 
 * Features:
 * - Separate compute queue management
 * - Automatic work dependency tracking
 * - Timeline semaphore synchronization
 * - Work stealing between queues
 * - Occupancy-aware scheduling
 * 
 * Reference: Engine/Source/Runtime/RHI/Public/RHIAsyncCompute.h
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>

class VulkanContext;

namespace Sanic {

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class AsyncComputeScheduler;
struct ComputeTask;
struct ComputeFence;

// ============================================================================
// TYPES AND ENUMS
// ============================================================================

using ComputeTaskHandle = uint64_t;
constexpr ComputeTaskHandle INVALID_TASK_HANDLE = 0;

/**
 * Queue type for compute work
 */
enum class ComputeQueueType : uint32_t {
    Graphics = 0,       // Main graphics queue (can also do compute)
    AsyncCompute = 1,   // Dedicated async compute queue
    Transfer = 2,       // Transfer/copy queue
    
    Count = 3
};

/**
 * Task priority for scheduling
 */
enum class ComputePriority : uint32_t {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

/**
 * Task state
 */
enum class TaskState : uint32_t {
    Pending = 0,
    Scheduled = 1,
    Executing = 2,
    Completed = 3,
    Failed = 4
};

/**
 * Resource access type for dependency tracking
 */
enum class ResourceAccess : uint32_t {
    Read = 0,
    Write = 1,
    ReadWrite = 2
};

// ============================================================================
// STRUCTURES
// ============================================================================

/**
 * Resource dependency for a compute task
 */
struct ResourceDependency {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    ResourceAccess access = ResourceAccess::Read;
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkAccessFlags accessFlags = VK_ACCESS_SHADER_READ_BIT;
};

/**
 * Compute task definition
 */
struct ComputeTask {
    ComputeTaskHandle handle = INVALID_TASK_HANDLE;
    std::string name;
    
    // Execution
    std::function<void(VkCommandBuffer)> recordFunc;
    ComputeQueueType preferredQueue = ComputeQueueType::AsyncCompute;
    ComputePriority priority = ComputePriority::Normal;
    
    // Dependencies
    std::vector<ComputeTaskHandle> waitTasks;
    std::vector<ResourceDependency> resourceDeps;
    
    // State
    TaskState state = TaskState::Pending;
    uint64_t submitFrame = 0;
    
    // Timing
    float estimatedDurationMs = 0.0f;
    float actualDurationMs = 0.0f;
    
    // GPU occupancy hints
    uint32_t threadGroupsX = 1;
    uint32_t threadGroupsY = 1;
    uint32_t threadGroupsZ = 1;
    float expectedOccupancy = 1.0f;
};

/**
 * Per-queue state
 */
struct QueueState {
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t familyIndex = 0;
    
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    uint32_t cmdBufferIndex = 0;
    
    // Timeline semaphore for synchronization
    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    uint64_t currentValue = 0;
    
    // Task queue
    std::queue<ComputeTaskHandle> pendingTasks;
    ComputeTaskHandle currentTask = INVALID_TASK_HANDLE;
    
    // Statistics
    uint32_t tasksCompleted = 0;
    float totalExecutionTimeMs = 0.0f;
    float averageLatencyMs = 0.0f;
};

/**
 * Async compute configuration
 */
struct AsyncComputeConfig {
    // Queue configuration
    bool enableAsyncCompute = true;
    bool enableTransferQueue = true;
    
    // Scheduling
    uint32_t maxPendingTasks = 256;
    uint32_t commandBuffersPerQueue = 8;
    
    // Work stealing
    bool enableWorkStealing = true;
    float workStealThreshold = 0.8f;  // Steal when queue is this full
    
    // Occupancy
    bool occupancyAwareScheduling = true;
    float minOccupancyForAsync = 0.5f;
    
    // Batching
    bool enableTaskBatching = true;
    uint32_t maxBatchSize = 4;
    float batchTimeoutMs = 1.0f;
};

// ============================================================================
// ASYNC COMPUTE SCHEDULER
// ============================================================================

/**
 * Main async compute scheduler class
 */
class AsyncComputeScheduler {
public:
    AsyncComputeScheduler(VulkanContext& context);
    ~AsyncComputeScheduler();
    
    bool initialize(const AsyncComputeConfig& config = {});
    void shutdown();
    
    // Configuration
    void setConfig(const AsyncComputeConfig& config);
    const AsyncComputeConfig& getConfig() const { return config_; }
    
    // Task management
    /**
     * Create a new compute task
     */
    ComputeTaskHandle createTask(const std::string& name,
                                std::function<void(VkCommandBuffer)> recordFunc);
    
    /**
     * Set task properties
     */
    void setTaskQueue(ComputeTaskHandle task, ComputeQueueType queue);
    void setTaskPriority(ComputeTaskHandle task, ComputePriority priority);
    void setTaskDependency(ComputeTaskHandle task, ComputeTaskHandle dependency);
    void addResourceDependency(ComputeTaskHandle task, const ResourceDependency& dep);
    void setTaskDispatchSize(ComputeTaskHandle task, uint32_t x, uint32_t y, uint32_t z);
    void setExpectedDuration(ComputeTaskHandle task, float milliseconds);
    
    /**
     * Submit task for execution
     */
    void submitTask(ComputeTaskHandle task);
    
    /**
     * Wait for a specific task to complete
     */
    void waitForTask(ComputeTaskHandle task, uint64_t timeoutNs = UINT64_MAX);
    
    /**
     * Check if task is complete
     */
    bool isTaskComplete(ComputeTaskHandle task) const;
    
    /**
     * Get task state
     */
    TaskState getTaskState(ComputeTaskHandle task) const;
    
    // Frame management
    /**
     * Begin frame - reset per-frame state
     */
    void beginFrame();
    
    /**
     * Flush all pending work
     */
    void flush();
    
    /**
     * End frame - wait for completion
     */
    void endFrame();
    
    // Synchronization
    /**
     * Insert a barrier between graphics and async compute
     */
    void insertCrossQueueBarrier(ComputeQueueType srcQueue, 
                                 ComputeQueueType dstQueue,
                                 VkBuffer buffer);
    
    void insertCrossQueueBarrier(ComputeQueueType srcQueue, 
                                 ComputeQueueType dstQueue,
                                 VkImage image,
                                 VkImageLayout oldLayout,
                                 VkImageLayout newLayout);
    
    /**
     * Get a semaphore to signal/wait on
     */
    VkSemaphore getQueueSemaphore(ComputeQueueType queue);
    uint64_t getQueueSemaphoreValue(ComputeQueueType queue);
    
    // Statistics
    struct Stats {
        uint32_t tasksSubmitted = 0;
        uint32_t tasksCompleted = 0;
        uint32_t tasksFailed = 0;
        
        float asyncComputeUtilization = 0.0f;
        float graphicsComputeUtilization = 0.0f;
        
        uint32_t crossQueueSyncs = 0;
        uint32_t workStealCount = 0;
        
        float averageTaskLatencyMs = 0.0f;
        float peakTaskLatencyMs = 0.0f;
    };
    
    const Stats& getStats() const { return stats_; }
    void resetStats();
    
    // Debug
    void debugPrint() const;
    
private:
    void createQueues();
    void createCommandPools();
    void createSemaphores();
    
    VkCommandBuffer allocateCommandBuffer(ComputeQueueType queue);
    void submitCommandBuffer(ComputeQueueType queue, VkCommandBuffer cmd,
                            const std::vector<VkSemaphore>& waitSemaphores,
                            const std::vector<uint64_t>& waitValues,
                            VkSemaphore signalSemaphore,
                            uint64_t signalValue);
    
    ComputeTask* getTask(ComputeTaskHandle handle);
    const ComputeTask* getTask(ComputeTaskHandle handle) const;
    
    ComputeQueueType selectQueue(const ComputeTask& task);
    bool canExecute(const ComputeTask& task) const;
    void scheduleTask(ComputeTask& task);
    void executeTask(ComputeTask& task, ComputeQueueType queue);
    
    void tryWorkStealing();
    void processPendingTasks();
    void updateStats();
    
    VulkanContext& context_;
    AsyncComputeConfig config_;
    
    // Queues
    std::array<QueueState, static_cast<size_t>(ComputeQueueType::Count)> queues_;
    bool hasAsyncComputeQueue_ = false;
    bool hasTransferQueue_ = false;
    
    // Tasks
    std::unordered_map<ComputeTaskHandle, std::unique_ptr<ComputeTask>> tasks_;
    std::atomic<ComputeTaskHandle> nextTaskHandle_{1};
    std::mutex taskMutex_;
    
    // Frame state
    uint64_t currentFrame_ = 0;
    std::vector<ComputeTaskHandle> frameTasksSubmitted_;
    
    // Cross-queue synchronization
    struct CrossQueueSync {
        ComputeQueueType srcQueue;
        ComputeQueueType dstQueue;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };
    std::vector<CrossQueueSync> pendingSyncs_;
    
    // Statistics
    Stats stats_;
};

// ============================================================================
// COMPUTE TASK BUILDER (FLUENT API)
// ============================================================================

/**
 * Helper class for building compute tasks with fluent API
 */
class ComputeTaskBuilder {
public:
    ComputeTaskBuilder(AsyncComputeScheduler& scheduler, const std::string& name);
    
    ComputeTaskBuilder& record(std::function<void(VkCommandBuffer)> func);
    ComputeTaskBuilder& onQueue(ComputeQueueType queue);
    ComputeTaskBuilder& withPriority(ComputePriority priority);
    ComputeTaskBuilder& dependsOn(ComputeTaskHandle task);
    ComputeTaskBuilder& readsBuffer(VkBuffer buffer, VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    ComputeTaskBuilder& writesBuffer(VkBuffer buffer, VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    ComputeTaskBuilder& readsImage(VkImage image, VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    ComputeTaskBuilder& writesImage(VkImage image, VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    ComputeTaskBuilder& dispatchSize(uint32_t x, uint32_t y, uint32_t z);
    ComputeTaskBuilder& estimatedDuration(float ms);
    
    ComputeTaskHandle submit();
    
private:
    AsyncComputeScheduler& scheduler_;
    ComputeTaskHandle handle_;
};

// ============================================================================
// SCOPED ASYNC COMPUTE
// ============================================================================

/**
 * RAII helper for async compute regions
 */
class ScopedAsyncCompute {
public:
    ScopedAsyncCompute(AsyncComputeScheduler& scheduler);
    ~ScopedAsyncCompute();
    
    ComputeTaskBuilder task(const std::string& name);
    
    // Deleted copy/move
    ScopedAsyncCompute(const ScopedAsyncCompute&) = delete;
    ScopedAsyncCompute& operator=(const ScopedAsyncCompute&) = delete;
    
private:
    AsyncComputeScheduler& scheduler_;
    std::vector<ComputeTaskHandle> submittedTasks_;
};

} // namespace Sanic
