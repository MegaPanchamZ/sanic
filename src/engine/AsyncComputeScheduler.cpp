/**
 * AsyncComputeScheduler.cpp
 * 
 * Async compute scheduling system implementation
 */

#include "AsyncComputeScheduler.h"
#include "VulkanContext.h"

#include <algorithm>
#include <chrono>

namespace Sanic {

// ============================================================================
// ASYNC COMPUTE SCHEDULER
// ============================================================================

AsyncComputeScheduler::AsyncComputeScheduler(VulkanContext& context)
    : context_(context) {
}

AsyncComputeScheduler::~AsyncComputeScheduler() {
    shutdown();
}

bool AsyncComputeScheduler::initialize(const AsyncComputeConfig& config) {
    config_ = config;
    
    createQueues();
    createCommandPools();
    createSemaphores();
    
    return true;
}

void AsyncComputeScheduler::shutdown() {
    VkDevice device = context_.getDevice();
    
    // Wait for all work to complete
    for (auto& queueState : queues_) {
        if (queueState.queue != VK_NULL_HANDLE && queueState.timelineSemaphore != VK_NULL_HANDLE) {
            VkSemaphoreWaitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &queueState.timelineSemaphore;
            waitInfo.pValues = &queueState.currentValue;
            vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
        }
    }
    
    // Cleanup resources
    for (auto& queueState : queues_) {
        if (queueState.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, queueState.commandPool, nullptr);
            queueState.commandPool = VK_NULL_HANDLE;
        }
        if (queueState.timelineSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, queueState.timelineSemaphore, nullptr);
            queueState.timelineSemaphore = VK_NULL_HANDLE;
        }
    }
    
    tasks_.clear();
}

void AsyncComputeScheduler::setConfig(const AsyncComputeConfig& config) {
    config_ = config;
}

void AsyncComputeScheduler::createQueues() {
    // Get queue family indices from context
    uint32_t graphicsFamily = context_.getGraphicsQueueFamily();
    uint32_t computeFamily = context_.getComputeQueueFamily();
    uint32_t transferFamily = context_.getTransferQueueFamily();
    
    VkDevice device = context_.getDevice();
    
    // Graphics queue (index 0)
    vkGetDeviceQueue(device, graphicsFamily, 0, &queues_[0].queue);
    queues_[0].familyIndex = graphicsFamily;
    
    // Async compute queue (if available and different from graphics)
    if (config_.enableAsyncCompute && computeFamily != graphicsFamily) {
        vkGetDeviceQueue(device, computeFamily, 0, &queues_[1].queue);
        queues_[1].familyIndex = computeFamily;
        hasAsyncComputeQueue_ = true;
    } else {
        // Use graphics queue for compute if no dedicated queue
        queues_[1].queue = queues_[0].queue;
        queues_[1].familyIndex = graphicsFamily;
        hasAsyncComputeQueue_ = false;
    }
    
    // Transfer queue (if available)
    if (config_.enableTransferQueue && transferFamily != graphicsFamily) {
        vkGetDeviceQueue(device, transferFamily, 0, &queues_[2].queue);
        queues_[2].familyIndex = transferFamily;
        hasTransferQueue_ = true;
    } else {
        queues_[2].queue = queues_[0].queue;
        queues_[2].familyIndex = graphicsFamily;
        hasTransferQueue_ = false;
    }
}

void AsyncComputeScheduler::createCommandPools() {
    VkDevice device = context_.getDevice();
    
    for (size_t i = 0; i < queues_.size(); ++i) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queues_[i].familyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &queues_[i].commandPool) != VK_SUCCESS) {
            continue;
        }
        
        // Pre-allocate command buffers
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = queues_[i].commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = config_.commandBuffersPerQueue;
        
        queues_[i].commandBuffers.resize(config_.commandBuffersPerQueue);
        vkAllocateCommandBuffers(device, &allocInfo, queues_[i].commandBuffers.data());
    }
}

void AsyncComputeScheduler::createSemaphores() {
    VkDevice device = context_.getDevice();
    
    for (auto& queueState : queues_) {
        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue = 0;
        
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semInfo.pNext = &typeInfo;
        
        vkCreateSemaphore(device, &semInfo, nullptr, &queueState.timelineSemaphore);
    }
}

ComputeTaskHandle AsyncComputeScheduler::createTask(const std::string& name,
                                                    std::function<void(VkCommandBuffer)> recordFunc) {
    std::lock_guard<std::mutex> lock(taskMutex_);
    
    ComputeTaskHandle handle = nextTaskHandle_++;
    
    auto task = std::make_unique<ComputeTask>();
    task->handle = handle;
    task->name = name;
    task->recordFunc = std::move(recordFunc);
    task->state = TaskState::Pending;
    
    tasks_[handle] = std::move(task);
    
    return handle;
}

void AsyncComputeScheduler::setTaskQueue(ComputeTaskHandle handle, ComputeQueueType queue) {
    if (auto* task = getTask(handle)) {
        task->preferredQueue = queue;
    }
}

void AsyncComputeScheduler::setTaskPriority(ComputeTaskHandle handle, ComputePriority priority) {
    if (auto* task = getTask(handle)) {
        task->priority = priority;
    }
}

void AsyncComputeScheduler::setTaskDependency(ComputeTaskHandle handle, ComputeTaskHandle dependency) {
    if (auto* task = getTask(handle)) {
        task->waitTasks.push_back(dependency);
    }
}

void AsyncComputeScheduler::addResourceDependency(ComputeTaskHandle handle, const ResourceDependency& dep) {
    if (auto* task = getTask(handle)) {
        task->resourceDeps.push_back(dep);
    }
}

void AsyncComputeScheduler::setTaskDispatchSize(ComputeTaskHandle handle, uint32_t x, uint32_t y, uint32_t z) {
    if (auto* task = getTask(handle)) {
        task->threadGroupsX = x;
        task->threadGroupsY = y;
        task->threadGroupsZ = z;
    }
}

void AsyncComputeScheduler::setExpectedDuration(ComputeTaskHandle handle, float milliseconds) {
    if (auto* task = getTask(handle)) {
        task->estimatedDurationMs = milliseconds;
    }
}

void AsyncComputeScheduler::submitTask(ComputeTaskHandle handle) {
    auto* task = getTask(handle);
    if (!task) return;
    
    task->submitFrame = currentFrame_;
    stats_.tasksSubmitted++;
    
    // Check if task can execute immediately
    if (canExecute(*task)) {
        scheduleTask(*task);
    } else {
        // Queue for later
        ComputeQueueType queue = selectQueue(*task);
        queues_[static_cast<size_t>(queue)].pendingTasks.push(handle);
    }
    
    frameTasksSubmitted_.push_back(handle);
}

void AsyncComputeScheduler::waitForTask(ComputeTaskHandle handle, uint64_t timeoutNs) {
    const auto* task = getTask(handle);
    if (!task) return;
    
    if (task->state == TaskState::Completed) return;
    
    // Wait on the timeline semaphore
    // TODO: Track which semaphore value corresponds to which task
    VkDevice device = context_.getDevice();
    
    for (auto& queueState : queues_) {
        if (queueState.timelineSemaphore != VK_NULL_HANDLE) {
            VkSemaphoreWaitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &queueState.timelineSemaphore;
            waitInfo.pValues = &queueState.currentValue;
            vkWaitSemaphores(device, &waitInfo, timeoutNs);
        }
    }
}

bool AsyncComputeScheduler::isTaskComplete(ComputeTaskHandle handle) const {
    const auto* task = getTask(handle);
    return task && task->state == TaskState::Completed;
}

TaskState AsyncComputeScheduler::getTaskState(ComputeTaskHandle handle) const {
    const auto* task = getTask(handle);
    return task ? task->state : TaskState::Failed;
}

void AsyncComputeScheduler::beginFrame() {
    currentFrame_++;
    frameTasksSubmitted_.clear();
    
    // Process any pending cross-queue syncs
    pendingSyncs_.clear();
    
    // Try to execute pending tasks
    processPendingTasks();
}

void AsyncComputeScheduler::flush() {
    // Submit all pending tasks
    processPendingTasks();
    
    // Try work stealing if enabled
    if (config_.enableWorkStealing) {
        tryWorkStealing();
    }
}

void AsyncComputeScheduler::endFrame() {
    // Wait for frame tasks to complete
    for (ComputeTaskHandle handle : frameTasksSubmitted_) {
        waitForTask(handle);
    }
    
    updateStats();
}

void AsyncComputeScheduler::insertCrossQueueBarrier(ComputeQueueType srcQueue, 
                                                    ComputeQueueType dstQueue,
                                                    VkBuffer buffer) {
    CrossQueueSync sync;
    sync.srcQueue = srcQueue;
    sync.dstQueue = dstQueue;
    sync.buffer = buffer;
    pendingSyncs_.push_back(sync);
    stats_.crossQueueSyncs++;
}

void AsyncComputeScheduler::insertCrossQueueBarrier(ComputeQueueType srcQueue, 
                                                    ComputeQueueType dstQueue,
                                                    VkImage image,
                                                    VkImageLayout oldLayout,
                                                    VkImageLayout newLayout) {
    CrossQueueSync sync;
    sync.srcQueue = srcQueue;
    sync.dstQueue = dstQueue;
    sync.image = image;
    sync.oldLayout = oldLayout;
    sync.newLayout = newLayout;
    pendingSyncs_.push_back(sync);
    stats_.crossQueueSyncs++;
}

VkSemaphore AsyncComputeScheduler::getQueueSemaphore(ComputeQueueType queue) {
    return queues_[static_cast<size_t>(queue)].timelineSemaphore;
}

uint64_t AsyncComputeScheduler::getQueueSemaphoreValue(ComputeQueueType queue) {
    return queues_[static_cast<size_t>(queue)].currentValue;
}

void AsyncComputeScheduler::resetStats() {
    stats_ = Stats{};
}

VkCommandBuffer AsyncComputeScheduler::allocateCommandBuffer(ComputeQueueType queue) {
    QueueState& queueState = queues_[static_cast<size_t>(queue)];
    
    uint32_t index = queueState.cmdBufferIndex;
    queueState.cmdBufferIndex = (queueState.cmdBufferIndex + 1) % queueState.commandBuffers.size();
    
    VkCommandBuffer cmd = queueState.commandBuffers[index];
    vkResetCommandBuffer(cmd, 0);
    
    return cmd;
}

void AsyncComputeScheduler::submitCommandBuffer(ComputeQueueType queue, VkCommandBuffer cmd,
                                                const std::vector<VkSemaphore>& waitSemaphores,
                                                const std::vector<uint64_t>& waitValues,
                                                VkSemaphore signalSemaphore,
                                                uint64_t signalValue) {
    QueueState& queueState = queues_[static_cast<size_t>(queue)];
    
    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
    timelineInfo.pWaitSemaphoreValues = waitValues.empty() ? nullptr : waitValues.data();
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;
    
    std::vector<VkPipelineStageFlags> waitStages(waitSemaphores.size(), 
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.empty() ? nullptr : waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;
    
    vkQueueSubmit(queueState.queue, 1, &submitInfo, VK_NULL_HANDLE);
}

ComputeTask* AsyncComputeScheduler::getTask(ComputeTaskHandle handle) {
    auto it = tasks_.find(handle);
    return it != tasks_.end() ? it->second.get() : nullptr;
}

const ComputeTask* AsyncComputeScheduler::getTask(ComputeTaskHandle handle) const {
    auto it = tasks_.find(handle);
    return it != tasks_.end() ? it->second.get() : nullptr;
}

ComputeQueueType AsyncComputeScheduler::selectQueue(const ComputeTask& task) {
    // If async compute isn't available or desired, use graphics
    if (!hasAsyncComputeQueue_ || !config_.enableAsyncCompute) {
        return ComputeQueueType::Graphics;
    }
    
    // Respect task preference
    if (task.preferredQueue == ComputeQueueType::AsyncCompute) {
        // Check occupancy if enabled
        if (config_.occupancyAwareScheduling) {
            if (task.expectedOccupancy >= config_.minOccupancyForAsync) {
                return ComputeQueueType::AsyncCompute;
            }
        } else {
            return ComputeQueueType::AsyncCompute;
        }
    }
    
    return task.preferredQueue;
}

bool AsyncComputeScheduler::canExecute(const ComputeTask& task) const {
    // Check task dependencies
    for (ComputeTaskHandle depHandle : task.waitTasks) {
        const auto* depTask = getTask(depHandle);
        if (depTask && depTask->state != TaskState::Completed) {
            return false;
        }
    }
    
    return true;
}

void AsyncComputeScheduler::scheduleTask(ComputeTask& task) {
    ComputeQueueType queue = selectQueue(task);
    executeTask(task, queue);
}

void AsyncComputeScheduler::executeTask(ComputeTask& task, ComputeQueueType queue) {
    task.state = TaskState::Executing;
    
    QueueState& queueState = queues_[static_cast<size_t>(queue)];
    
    // Allocate and record command buffer
    VkCommandBuffer cmd = allocateCommandBuffer(queue);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Record the task's commands
    if (task.recordFunc) {
        task.recordFunc(cmd);
    }
    
    vkEndCommandBuffer(cmd);
    
    // Build wait semaphores from dependencies
    std::vector<VkSemaphore> waitSemaphores;
    std::vector<uint64_t> waitValues;
    
    for (ComputeTaskHandle depHandle : task.waitTasks) {
        // TODO: Track semaphore values per task
    }
    
    // Submit
    uint64_t signalValue = ++queueState.currentValue;
    submitCommandBuffer(queue, cmd, waitSemaphores, waitValues, 
                       queueState.timelineSemaphore, signalValue);
    
    task.state = TaskState::Completed;
    stats_.tasksCompleted++;
    queueState.tasksCompleted++;
}

void AsyncComputeScheduler::tryWorkStealing() {
    // If async compute queue is idle and graphics queue is busy,
    // steal some work
    QueueState& asyncQueue = queues_[static_cast<size_t>(ComputeQueueType::AsyncCompute)];
    QueueState& graphicsQueue = queues_[static_cast<size_t>(ComputeQueueType::Graphics)];
    
    if (!hasAsyncComputeQueue_) return;
    
    size_t graphicsQueueSize = graphicsQueue.pendingTasks.size();
    size_t asyncQueueSize = asyncQueue.pendingTasks.size();
    
    // Steal if graphics queue is loaded and async is idle
    if (graphicsQueueSize > 2 && asyncQueueSize == 0) {
        // Move task from graphics to async
        if (!graphicsQueue.pendingTasks.empty()) {
            ComputeTaskHandle handle = graphicsQueue.pendingTasks.front();
            graphicsQueue.pendingTasks.pop();
            asyncQueue.pendingTasks.push(handle);
            stats_.workStealCount++;
        }
    }
}

void AsyncComputeScheduler::processPendingTasks() {
    for (size_t i = 0; i < queues_.size(); ++i) {
        QueueState& queueState = queues_[i];
        
        while (!queueState.pendingTasks.empty()) {
            ComputeTaskHandle handle = queueState.pendingTasks.front();
            auto* task = getTask(handle);
            
            if (task && canExecute(*task)) {
                queueState.pendingTasks.pop();
                executeTask(*task, static_cast<ComputeQueueType>(i));
            } else {
                break;  // Can't execute, wait for dependencies
            }
        }
    }
}

void AsyncComputeScheduler::updateStats() {
    // Calculate utilization
    float totalGraphicsTime = queues_[0].totalExecutionTimeMs;
    float totalAsyncTime = queues_[1].totalExecutionTimeMs;
    float frameTime = 16.67f;  // Assume 60fps for now
    
    stats_.graphicsComputeUtilization = totalGraphicsTime / frameTime;
    stats_.asyncComputeUtilization = totalAsyncTime / frameTime;
    
    // Calculate average latency
    uint32_t totalCompleted = 0;
    float totalLatency = 0.0f;
    
    for (const auto& pair : tasks_) {
        if (pair.second->state == TaskState::Completed) {
            totalCompleted++;
            totalLatency += pair.second->actualDurationMs;
            stats_.peakTaskLatencyMs = std::max(stats_.peakTaskLatencyMs, 
                                                 pair.second->actualDurationMs);
        }
    }
    
    if (totalCompleted > 0) {
        stats_.averageTaskLatencyMs = totalLatency / totalCompleted;
    }
}

void AsyncComputeScheduler::debugPrint() const {
    printf("=== Async Compute Scheduler ===\n");
    printf("Has Async Compute Queue: %s\n", hasAsyncComputeQueue_ ? "yes" : "no");
    printf("Has Transfer Queue: %s\n", hasTransferQueue_ ? "yes" : "no");
    printf("\nStats:\n");
    printf("  Tasks Submitted: %u\n", stats_.tasksSubmitted);
    printf("  Tasks Completed: %u\n", stats_.tasksCompleted);
    printf("  Tasks Failed: %u\n", stats_.tasksFailed);
    printf("  Cross-Queue Syncs: %u\n", stats_.crossQueueSyncs);
    printf("  Work Steals: %u\n", stats_.workStealCount);
    printf("  Avg Latency: %.3f ms\n", stats_.averageTaskLatencyMs);
    printf("  Peak Latency: %.3f ms\n", stats_.peakTaskLatencyMs);
    printf("  Graphics Compute Util: %.1f%%\n", stats_.graphicsComputeUtilization * 100.0f);
    printf("  Async Compute Util: %.1f%%\n", stats_.asyncComputeUtilization * 100.0f);
}

// ============================================================================
// COMPUTE TASK BUILDER
// ============================================================================

ComputeTaskBuilder::ComputeTaskBuilder(AsyncComputeScheduler& scheduler, const std::string& name)
    : scheduler_(scheduler)
    , handle_(scheduler.createTask(name, nullptr)) {
}

ComputeTaskBuilder& ComputeTaskBuilder::record(std::function<void(VkCommandBuffer)> func) {
    // We need to update the task's record function
    // This requires access to the task internals - simplified for now
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::onQueue(ComputeQueueType queue) {
    scheduler_.setTaskQueue(handle_, queue);
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::withPriority(ComputePriority priority) {
    scheduler_.setTaskPriority(handle_, priority);
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::dependsOn(ComputeTaskHandle task) {
    scheduler_.setTaskDependency(handle_, task);
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::readsBuffer(VkBuffer buffer, VkPipelineStageFlags stage) {
    ResourceDependency dep;
    dep.buffer = buffer;
    dep.access = ResourceAccess::Read;
    dep.stage = stage;
    dep.accessFlags = VK_ACCESS_SHADER_READ_BIT;
    scheduler_.addResourceDependency(handle_, dep);
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::writesBuffer(VkBuffer buffer, VkPipelineStageFlags stage) {
    ResourceDependency dep;
    dep.buffer = buffer;
    dep.access = ResourceAccess::Write;
    dep.stage = stage;
    dep.accessFlags = VK_ACCESS_SHADER_WRITE_BIT;
    scheduler_.addResourceDependency(handle_, dep);
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::readsImage(VkImage image, VkPipelineStageFlags stage) {
    ResourceDependency dep;
    dep.image = image;
    dep.access = ResourceAccess::Read;
    dep.stage = stage;
    dep.accessFlags = VK_ACCESS_SHADER_READ_BIT;
    scheduler_.addResourceDependency(handle_, dep);
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::writesImage(VkImage image, VkPipelineStageFlags stage) {
    ResourceDependency dep;
    dep.image = image;
    dep.access = ResourceAccess::Write;
    dep.stage = stage;
    dep.accessFlags = VK_ACCESS_SHADER_WRITE_BIT;
    scheduler_.addResourceDependency(handle_, dep);
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::dispatchSize(uint32_t x, uint32_t y, uint32_t z) {
    scheduler_.setTaskDispatchSize(handle_, x, y, z);
    return *this;
}

ComputeTaskBuilder& ComputeTaskBuilder::estimatedDuration(float ms) {
    scheduler_.setExpectedDuration(handle_, ms);
    return *this;
}

ComputeTaskHandle ComputeTaskBuilder::submit() {
    scheduler_.submitTask(handle_);
    return handle_;
}

// ============================================================================
// SCOPED ASYNC COMPUTE
// ============================================================================

ScopedAsyncCompute::ScopedAsyncCompute(AsyncComputeScheduler& scheduler)
    : scheduler_(scheduler) {
}

ScopedAsyncCompute::~ScopedAsyncCompute() {
    // Wait for all submitted tasks
    for (ComputeTaskHandle handle : submittedTasks_) {
        scheduler_.waitForTask(handle);
    }
}

ComputeTaskBuilder ScopedAsyncCompute::task(const std::string& name) {
    return ComputeTaskBuilder(scheduler_, name);
}

} // namespace Sanic
