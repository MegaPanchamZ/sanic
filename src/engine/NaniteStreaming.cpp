/**
 * NaniteStreaming.cpp
 * 
 * Implementation of Nanite-style geometry streaming.
 */

#include "NaniteStreaming.h"
#include "VulkanContext.h"
#include <fstream>
#include <algorithm>
#include <cstring>

// ============================================================================
// NANITE STREAMING MANAGER
// ============================================================================

NaniteStreamingManager::NaniteStreamingManager(VulkanContext& context) 
    : context(context) {
}

NaniteStreamingManager::~NaniteStreamingManager() {
    shutdown();
}

void NaniteStreamingManager::initialize(uint32_t poolSizeMB) {
    uint32_t poolSizeBytes = poolSizeMB * 1024 * 1024;
    uint32_t numPages = poolSizeBytes / NaniteStreaming::GPU_PAGE_SIZE;
    
    createPagePool(numPages);
    createPageTable();
    createRequestBuffers();
    createStagingBufferPool();
    
    // Start I/O thread
    ioThreadRunning = true;
    ioThread = std::thread(&NaniteStreamingManager::ioThreadFunc, this);
}

void NaniteStreamingManager::shutdown() {
    // Stop I/O thread
    ioThreadRunning = false;
    ioCondition.notify_all();
    if (ioThread.joinable()) {
        ioThread.join();
    }
    
    // Cleanup Vulkan resources
    VkDevice device = context.getDevice();
    
    if (pagePoolBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, pagePoolBuffer, nullptr);
        vkFreeMemory(device, pagePoolMemory, nullptr);
    }
    
    if (pageTableBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, pageTableBuffer, nullptr);
        vkFreeMemory(device, pageTableMemory, nullptr);
    }
    
    if (requestBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, requestBuffer, nullptr);
        vkFreeMemory(device, requestBufferMemory, nullptr);
    }
    
    if (requestReadbackBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, requestReadbackBuffer, nullptr);
        vkFreeMemory(device, requestReadbackMemory, nullptr);
    }
    
    for (auto& staging : stagingBufferPool) {
        if (staging.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, staging.buffer, nullptr);
            vkFreeMemory(device, staging.memory, nullptr);
        }
    }
}

void NaniteStreamingManager::createPagePool(uint32_t numPages) {
    poolSizePages = numPages;
    pageAllocated.resize(numPages, false);
    
    // Initialize free list
    freePageList.reserve(numPages);
    for (uint32_t i = 0; i < numPages; ++i) {
        freePageList.push_back(i);
    }
    
    // Create GPU buffer for page pool
    VkDeviceSize poolSize = static_cast<VkDeviceSize>(numPages) * NaniteStreaming::GPU_PAGE_SIZE;
    
    createBuffer(poolSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        pagePoolBuffer, pagePoolMemory);
    
    // Get device address
    VkBufferDeviceAddressInfo addrInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addrInfo.buffer = pagePoolBuffer;
    pagePoolBufferAddress = vkGetBufferDeviceAddress(context.getDevice(), &addrInfo);
}

void NaniteStreamingManager::createPageTable() {
    // Page table: allows 64K resources x 1K pages each = 64M entries
    // Each entry is 4 bytes (gpu page index or 0xFFFFFFFF for not loaded)
    constexpr uint32_t MAX_RESOURCES = 65536;
    constexpr uint32_t MAX_PAGES_PER_RESOURCE = 1024;
    VkDeviceSize tableSize = MAX_RESOURCES * MAX_PAGES_PER_RESOURCE * sizeof(uint32_t);
    
    createBuffer(tableSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        pageTableBuffer, pageTableMemory);
    
    vkMapMemory(context.getDevice(), pageTableMemory, 0, tableSize, 0, &pageTableMapped);
    
    // Initialize all entries to "not loaded"
    std::memset(pageTableMapped, 0xFF, tableSize);
    
    VkBufferDeviceAddressInfo addrInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addrInfo.buffer = pageTableBuffer;
    pageTableBufferAddress = vkGetBufferDeviceAddress(context.getDevice(), &addrInfo);
}

void NaniteStreamingManager::createRequestBuffers() {
    // GPU write buffer for page requests
    constexpr uint32_t MAX_REQUESTS = 16384;
    VkDeviceSize bufferSize = sizeof(FGPURequestHeader) + MAX_REQUESTS * sizeof(FGPUPageRequest);
    
    createBuffer(bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        requestBuffer, requestBufferMemory);
    
    // CPU readback buffer
    createBuffer(bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        requestReadbackBuffer, requestReadbackMemory);
    
    vkMapMemory(context.getDevice(), requestReadbackMemory, 0, bufferSize, 0, &requestReadbackMapped);
}

void NaniteStreamingManager::createStagingBufferPool() {
    // Create a pool of staging buffers for async uploads
    constexpr uint32_t NUM_STAGING_BUFFERS = 16;
    stagingBufferPool.resize(NUM_STAGING_BUFFERS);
    
    for (auto& staging : stagingBufferPool) {
        createBuffer(NaniteStreaming::STREAMING_PAGE_SIZE,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging.buffer, staging.memory);
        
        vkMapMemory(context.getDevice(), staging.memory, 0, 
                   NaniteStreaming::STREAMING_PAGE_SIZE, 0, &staging.mapped);
        staging.inUse = false;
    }
}

uint32_t NaniteStreamingManager::registerResource(const std::string& path) {
    auto resource = std::make_unique<FStreamingResource>();
    resource->resourceId = nextResourceId++;
    resource->sourcePath = path;
    
    // Load resource header from file
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return 0;
    }
    
    // Read header (simplified - real implementation would have proper file format)
    file.read(reinterpret_cast<char*>(&resource->numPages), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&resource->numRootPages), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&resource->numHierarchyNodes), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&resource->numClusters), sizeof(uint32_t));
    
    // Read page offsets
    resource->pageOffsets.resize(resource->numPages);
    resource->pageSizes.resize(resource->numPages);
    file.read(reinterpret_cast<char*>(resource->pageOffsets.data()), 
              resource->numPages * sizeof(uint64_t));
    file.read(reinterpret_cast<char*>(resource->pageSizes.data()), 
              resource->numPages * sizeof(uint32_t));
    
    // Read root page indices
    resource->rootPageIndices.resize(resource->numRootPages);
    file.read(reinterpret_cast<char*>(resource->rootPageIndices.data()), 
              resource->numRootPages * sizeof(uint32_t));
    
    uint32_t id = resource->resourceId;
    resources[id] = std::move(resource);
    
    // Load root pages immediately
    for (uint32_t rootIdx : resources[id]->rootPageIndices) {
        FPageRequest req;
        req.key.resourceId = id;
        req.key.pageIndex = rootIdx;
        req.priority = NaniteStreaming::PRIORITY_CRITICAL;
        req.frameRequested = static_cast<uint32_t>(currentFrame);
        req.screenPixels = UINT32_MAX;
        pendingRequests.push(req);
    }
    
    return id;
}

void NaniteStreamingManager::unregisterResource(uint32_t resourceId) {
    // Evict all pages for this resource
    std::vector<FPageKey> toEvict;
    for (const auto& [key, page] : residentPages) {
        if (key.resourceId == resourceId) {
            toEvict.push_back(key);
        }
    }
    
    for (const auto& key : toEvict) {
        // Free the GPU page
        auto it = residentPages.find(key);
        if (it != residentPages.end()) {
            freePage(it->second->gpuPageIndex);
            updatePageTable(key, UINT32_MAX);
            residentPages.erase(it);
        }
    }
    
    resources.erase(resourceId);
}

void NaniteStreamingManager::beginFrame(VkCommandBuffer cmd, uint64_t frameNumber) {
    currentFrame = frameNumber;
    pagesLoadedThisFrame = 0;
    pagesEvictedThisFrame = 0;
    
    // Copy request buffer to readback buffer
    VkBufferCopy copyRegion = {};
    copyRegion.size = sizeof(FGPURequestHeader) + 16384 * sizeof(FGPUPageRequest);
    vkCmdCopyBuffer(cmd, requestBuffer, requestReadbackBuffer, 1, &copyRegion);
    
    // Insert barrier for readback
    VkMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    
    VkDependencyInfo depInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.memoryBarrierCount = 1;
    depInfo.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

void NaniteStreamingManager::update(VkCommandBuffer cmd) {
    // Process GPU requests from previous frame
    processGPURequests();
    
    // Submit new I/O requests
    submitIORequests();
    
    // Process completed loads
    processCompletedLoads(cmd);
}

void NaniteStreamingManager::endFrame(VkCommandBuffer cmd) {
    // Clear request buffer for next frame
    FGPURequestHeader clearHeader = {};
    clearHeader.maxRequests = 16384;
    clearHeader.frameNumber = static_cast<uint32_t>(currentFrame + 1);
    
    vkCmdUpdateBuffer(cmd, requestBuffer, 0, sizeof(FGPURequestHeader), &clearHeader);
}

void NaniteStreamingManager::processGPURequests() {
    // Read request header
    const FGPURequestHeader* header = static_cast<const FGPURequestHeader*>(requestReadbackMapped);
    const FGPUPageRequest* requests = reinterpret_cast<const FGPUPageRequest*>(
        static_cast<const uint8_t*>(requestReadbackMapped) + sizeof(FGPURequestHeader));
    
    uint32_t numRequests = std::min(header->numRequests, header->maxRequests);
    
    // Add requests to priority queue
    for (uint32_t i = 0; i < numRequests; ++i) {
        const FGPUPageRequest& gpuReq = requests[i];
        
        FPageKey key;
        key.resourceId = gpuReq.resourceId;
        key.pageIndex = gpuReq.pageIndex;
        
        // Skip if already resident or requested
        if (residentPages.count(key) > 0) continue;
        if (requestedPages.count(key.toUint64()) > 0) continue;
        
        FPageRequest req;
        req.key = key;
        req.priority = gpuReq.priority;
        req.frameRequested = static_cast<uint32_t>(currentFrame);
        req.screenPixels = gpuReq.screenPixels;
        
        pendingRequests.push(req);
        requestedPages.insert(key.toUint64());
    }
}

void NaniteStreamingManager::submitIORequests() {
    std::lock_guard<std::mutex> lock(pendingMutex);
    
    // Submit up to MAX_PENDING_PAGES requests to I/O thread
    while (!pendingRequests.empty() && 
           pendingPages.size() < NaniteStreaming::MAX_PENDING_PAGES) {
        
        FPageRequest req = pendingRequests.top();
        pendingRequests.pop();
        
        // Create pending page
        auto pending = std::make_unique<FPendingPage>();
        pending->key = req.key;
        pending->priority = req.priority;
        pending->state = EPageState::Requested;
        
        pendingPages.push_back(std::move(pending));
    }
    
    // Wake I/O thread
    ioCondition.notify_one();
}

void NaniteStreamingManager::processCompletedLoads(VkCommandBuffer cmd) {
    std::lock_guard<std::mutex> lock(pendingMutex);
    
    uint32_t pagesProcessed = 0;
    
    for (auto it = pendingPages.begin(); it != pendingPages.end() && 
         pagesProcessed < NaniteStreaming::MAX_PAGES_PER_FRAME;) {
        
        FPendingPage* pending = it->get();
        
        if (pending->state == EPageState::Loading) {
            // Data is loaded, need to upload to GPU
            
            // Allocate GPU page
            uint32_t gpuPageIndex = allocatePage();
            if (gpuPageIndex == UINT32_MAX) {
                // Need to evict
                evictPages(1);
                gpuPageIndex = allocatePage();
                if (gpuPageIndex == UINT32_MAX) {
                    ++it;
                    continue;  // Still no space
                }
            }
            
            pending->gpuPageIndex = gpuPageIndex;
            
            // Get staging buffer
            StagingBuffer* staging = acquireStagingBuffer();
            if (!staging) {
                freePage(gpuPageIndex);
                ++it;
                continue;
            }
            
            // Copy to staging
            std::memcpy(staging->mapped, pending->cpuData.data(), pending->cpuData.size());
            
            // Copy to GPU
            VkBufferCopy copyRegion = {};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = static_cast<VkDeviceSize>(gpuPageIndex) * NaniteStreaming::GPU_PAGE_SIZE;
            copyRegion.size = pending->cpuData.size();
            vkCmdCopyBuffer(cmd, staging->buffer, pagePoolBuffer, 1, &copyRegion);
            
            releaseStagingBuffer(staging);
            
            // Create resident page entry
            auto resident = std::make_unique<FResidentPage>();
            resident->key = pending->key;
            resident->gpuPageIndex = gpuPageIndex;
            resident->lastUsedFrame = currentFrame;
            resident->priority = pending->priority;
            
            // Parse fixups from page data
            const FPageHeader* header = reinterpret_cast<const FPageHeader*>(pending->cpuData.data());
            resident->numClusters = header->numClusters;
            
            if (header->numFixups > 0) {
                const FHierarchyFixup* fixups = reinterpret_cast<const FHierarchyFixup*>(
                    pending->cpuData.data() + header->hierarchyOffset);
                resident->fixups.assign(fixups, fixups + header->numFixups);
            }
            
            // Update page table
            updatePageTable(pending->key, gpuPageIndex);
            
            // Apply fixups (patch hierarchy)
            applyFixups(cmd, resident.get(), true);
            
            // Add to resident map
            residentPages[pending->key] = std::move(resident);
            
            // Cleanup
            requestedPages.erase(pending->key.toUint64());
            pending->cpuData.clear();
            
            pagesLoadedThisFrame++;
            pagesProcessed++;
            totalBytesStreamed += copyRegion.size;
            
            it = pendingPages.erase(it);
        } else {
            ++it;
        }
    }
}

void NaniteStreamingManager::evictPages(uint32_t numToEvict) {
    for (uint32_t i = 0; i < numToEvict && !lruList.empty(); ++i) {
        FResidentPage* page = getLRUPage();
        if (!page) break;
        
        // Don't evict root pages
        FStreamingResource* resource = getResource(page->key.resourceId);
        if (resource) {
            bool isRoot = std::find(resource->rootPageIndices.begin(), 
                                   resource->rootPageIndices.end(),
                                   page->key.pageIndex) != resource->rootPageIndices.end();
            if (isRoot) continue;
        }
        
        // Free the GPU page
        freePage(page->gpuPageIndex);
        
        // Update page table
        updatePageTable(page->key, UINT32_MAX);
        
        // Remove from resident map
        residentPages.erase(page->key);
        
        pagesEvictedThisFrame++;
    }
}

uint32_t NaniteStreamingManager::allocatePage() {
    if (freePageList.empty()) {
        return UINT32_MAX;
    }
    
    uint32_t pageIndex = freePageList.back();
    freePageList.pop_back();
    pageAllocated[pageIndex] = true;
    return pageIndex;
}

void NaniteStreamingManager::freePage(uint32_t pageIndex) {
    if (pageIndex < poolSizePages && pageAllocated[pageIndex]) {
        pageAllocated[pageIndex] = false;
        freePageList.push_back(pageIndex);
    }
}

void NaniteStreamingManager::updatePageTable(FPageKey key, uint32_t gpuPageIndex) {
    // Page table layout: [resourceId * MAX_PAGES_PER_RESOURCE + pageIndex]
    constexpr uint32_t MAX_PAGES_PER_RESOURCE = 1024;
    uint32_t tableIndex = key.resourceId * MAX_PAGES_PER_RESOURCE + key.pageIndex;
    
    uint32_t* table = static_cast<uint32_t*>(pageTableMapped);
    table[tableIndex] = gpuPageIndex;
}

void NaniteStreamingManager::applyFixups(VkCommandBuffer cmd, FResidentPage* page, bool isLoading) {
    if (page->fixups.empty()) return;
    
    FStreamingResource* resource = getResource(page->key.resourceId);
    if (!resource || resource->hierarchyBuffer == VK_NULL_HANDLE) return;
    
    // Update hierarchy nodes to point to new cluster locations
    for (const auto& fixup : page->fixups) {
        // In a real implementation, we'd update the hierarchy buffer on GPU
        // For now, we'll use vkCmdUpdateBuffer for small updates
        
        uint32_t targetValue = isLoading ? 
            (page->gpuPageIndex * NaniteStreaming::GPU_PAGE_SIZE + fixup.targetClusterStart) :
            0xFFFFFFFF;
        
        VkDeviceSize offset = fixup.hierarchyNodeIndex * sizeof(uint32_t) * 16 + // Node size
                             fixup.childSlotIndex * sizeof(uint32_t);
        
        vkCmdUpdateBuffer(cmd, resource->hierarchyBuffer, offset, sizeof(uint32_t), &targetValue);
    }
}

void NaniteStreamingManager::ioThreadFunc() {
    while (ioThreadRunning) {
        std::unique_lock<std::mutex> lock(pendingMutex);
        
        // Find pages that need loading
        std::vector<FPendingPage*> toLoad;
        for (auto& pending : pendingPages) {
            if (pending->state == EPageState::Requested) {
                pending->state = EPageState::Loading;
                toLoad.push_back(pending.get());
            }
        }
        
        lock.unlock();
        
        // Load pages from disk
        for (FPendingPage* page : toLoad) {
            loadPageFromDisk(page);
        }
        
        // Wait for more work or shutdown
        if (toLoad.empty()) {
            lock.lock();
            ioCondition.wait_for(lock, std::chrono::milliseconds(10));
        }
    }
}

void NaniteStreamingManager::loadPageFromDisk(FPendingPage* page) {
    FStreamingResource* resource = getResource(page->key.resourceId);
    if (!resource) {
        page->state = EPageState::NotLoaded;
        return;
    }
    
    if (page->key.pageIndex >= resource->numPages) {
        page->state = EPageState::NotLoaded;
        return;
    }
    
    std::ifstream file(resource->sourcePath, std::ios::binary);
    if (!file.is_open()) {
        page->state = EPageState::NotLoaded;
        return;
    }
    
    uint64_t offset = resource->pageOffsets[page->key.pageIndex];
    uint32_t size = resource->pageSizes[page->key.pageIndex];
    
    page->cpuData.resize(size);
    file.seekg(offset);
    file.read(reinterpret_cast<char*>(page->cpuData.data()), size);
    
    // Decompress if needed (placeholder)
    // DecompressPage(page->cpuData);
    
    // State is already Loading, will be processed by main thread
}

NaniteStreamingManager::StagingBuffer* NaniteStreamingManager::acquireStagingBuffer() {
    for (auto& staging : stagingBufferPool) {
        if (!staging.inUse) {
            staging.inUse = true;
            return &staging;
        }
    }
    return nullptr;
}

void NaniteStreamingManager::releaseStagingBuffer(StagingBuffer* buffer) {
    buffer->inUse = false;
}

void NaniteStreamingManager::updateLRU(FResidentPage* page) {
    // Move page to end of LRU list (most recently used)
    auto it = std::find(lruList.begin(), lruList.end(), page);
    if (it != lruList.end()) {
        lruList.erase(it);
    }
    lruList.push_back(page);
    page->lastUsedFrame = currentFrame;
}

FResidentPage* NaniteStreamingManager::getLRUPage() {
    if (lruList.empty()) return nullptr;
    
    FResidentPage* page = lruList.front();
    lruList.erase(lruList.begin());
    return page;
}

FStreamingResource* NaniteStreamingManager::getResource(uint32_t resourceId) {
    auto it = resources.find(resourceId);
    return it != resources.end() ? it->second.get() : nullptr;
}

NaniteStreamingManager::Stats NaniteStreamingManager::getStats() const {
    Stats stats = {};
    stats.totalPages = poolSizePages;
    stats.residentPages = static_cast<uint32_t>(residentPages.size());
    stats.pendingPages = static_cast<uint32_t>(pendingPages.size());
    stats.evictedThisFrame = pagesEvictedThisFrame;
    stats.loadedThisFrame = pagesLoadedThisFrame;
    stats.totalBytesStreamed = totalBytesStreamed;
    stats.poolSizeBytes = static_cast<uint64_t>(poolSizePages) * NaniteStreaming::GPU_PAGE_SIZE;
    stats.poolUtilization = static_cast<float>(stats.residentPages) / static_cast<float>(stats.totalPages);
    return stats;
}

void NaniteStreamingManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                          VkMemoryPropertyFlags properties, 
                                          VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateBuffer(context.getDevice(), &bufferInfo, nullptr, &buffer);
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(context.getDevice(), buffer, &memReqs);
    
    VkMemoryAllocateFlagsInfo flagsInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.pNext = &flagsInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);
    
    vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &memory);
    vkBindBufferMemory(context.getDevice(), buffer, memory, 0);
}

uint32_t NaniteStreamingManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(context.getPhysicalDevice(), &memProps);
    
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}
