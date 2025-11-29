/**
 * AssetLoader.cpp
 * 
 * Runtime asset loading implementation.
 * Handles file I/O, decompression, and GPU upload.
 */

#include "AssetLoader.h"
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace Sanic {

// Comparison function for priority queue
static auto loadRequestCompare = [](const LoadRequest& a, const LoadRequest& b) {
    return static_cast<int>(a.priority) < static_cast<int>(b.priority);
};

// ============================================================================
// ASSET LOADER IMPLEMENTATION
// ============================================================================

AssetLoader::AssetLoader() 
    : loadQueue_(loadRequestCompare) {
}

AssetLoader::~AssetLoader() {
    shutdown();
}

bool AssetLoader::initialize(VulkanContext* context, const StreamingConfig& config) {
    if (initialized_) {
        return true;
    }
    
    context_ = context;
    config_ = config;
    
    // Create transfer command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context_->getTransferQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | 
                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    
    if (vkCreateCommandPool(context_->getDevice(), &poolInfo, nullptr, &transferCommandPool_) != VK_SUCCESS) {
        return false;
    }
    
    transferQueue_ = context_->getTransferQueue();
    
    // Pre-allocate staging buffers
    stagingBuffers_.resize(config_.maxConcurrentLoads);
    for (auto& staging : stagingBuffers_) {
        staging.size = config_.readBufferSize;
        staging.buffer = createBuffer(
            staging.size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging.memory
        );
        
        vkMapMemory(context_->getDevice(), staging.memory, 0, staging.size, 0, &staging.mapped);
        staging.inUse = false;
    }
    
    // Start I/O threads
    shutdownRequested_ = false;
    for (uint32_t i = 0; i < config_.ioThreadCount; ++i) {
        ioThreads_.emplace_back(&AssetLoader::ioThreadFunc, this);
    }
    
    initialized_ = true;
    return true;
}

void AssetLoader::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Signal threads to stop
    shutdownRequested_ = true;
    queueCondition_.notify_all();
    
    // Wait for threads
    for (auto& thread : ioThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    ioThreads_.clear();
    
    // Free staging buffers
    for (auto& staging : stagingBuffers_) {
        if (staging.buffer != VK_NULL_HANDLE) {
            vkUnmapMemory(context_->getDevice(), staging.memory);
            vkDestroyBuffer(context_->getDevice(), staging.buffer, nullptr);
            vkFreeMemory(context_->getDevice(), staging.memory, nullptr);
        }
    }
    stagingBuffers_.clear();
    
    // Clear cache (free all assets)
    clearCache();
    
    // Destroy command pool
    if (transferCommandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_->getDevice(), transferCommandPool_, nullptr);
        transferCommandPool_ = VK_NULL_HANDLE;
    }
    
    initialized_ = false;
}

// ============================================================================
// SYNCHRONOUS LOADING
// ============================================================================

LoadedAsset* AssetLoader::loadSync(const std::string& filePath) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = assetCache_.find(filePath);
        if (it != assetCache_.end()) {
            it->second->refCount++;
            return it->second.get();
        }
    }
    
    // Open file
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return nullptr;
    }
    
    // Read header
    AssetHeader header;
    if (!loadHeader(filePath, header)) {
        return nullptr;
    }
    
    // Verify magic number
    if (header.magic != SANIC_MESH_MAGIC) {
        return nullptr;
    }
    
    // Create asset
    auto asset = std::make_unique<LoadedAsset>();
    asset->header = header;
    asset->filePath = filePath;
    
    // Read entire file into memory (for sync loading)
    file.seekg(0, std::ios::end);
    asset->fileSize = file.tellg();
    file.seekg(sizeof(AssetHeader), std::ios::beg);
    
    std::vector<uint8_t> fileData(asset->fileSize - sizeof(AssetHeader));
    file.read(reinterpret_cast<char*>(fileData.data()), fileData.size());
    file.close();
    
    // Parse and load each section
    size_t offset = 0;
    while (offset < fileData.size()) {
        // Read section header
        if (offset + sizeof(SectionHeader) > fileData.size()) {
            break;
        }
        
        SectionHeader sectionHeader;
        std::memcpy(&sectionHeader, fileData.data() + offset, sizeof(SectionHeader));
        offset += sizeof(SectionHeader);
        
        // Extract section data
        if (offset + sectionHeader.compressedSize > fileData.size()) {
            break;
        }
        
        std::vector<uint8_t> sectionData(fileData.data() + offset, 
                                         fileData.data() + offset + sectionHeader.compressedSize);
        offset += sectionHeader.compressedSize;
        
        // TODO: Decompress if needed
        // For now, assume uncompressed
        
        // Load based on section type
        switch (sectionHeader.type) {
            case SectionType::Geometry:
                loadGeometrySection(asset.get(), sectionData);
                break;
            case SectionType::Nanite:
                loadNaniteSection(asset.get(), sectionData);
                break;
            case SectionType::Lumen:
                loadLumenSection(asset.get(), sectionData);
                break;
            case SectionType::Physics:
                loadPhysicsSection(asset.get(), sectionData);
                break;
            default:
                // Skip unknown sections
                break;
        }
    }
    
    // Track statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    double loadTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        totalLoadTime_ += loadTimeMs;
        loadCount_++;
    }
    
    // Add to cache
    asset->refCount = 1;
    LoadedAsset* result = asset.get();
    
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        assetCache_[filePath] = std::move(asset);
    }
    
    return result;
}

// ============================================================================
// ASYNCHRONOUS LOADING
// ============================================================================

void AssetLoader::loadAsync(const LoadRequest& request) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        loadQueue_.push(request);
        pendingRequests_++;
    }
    queueCondition_.notify_one();
}

void AssetLoader::ioThreadFunc() {
    while (!shutdownRequested_) {
        LoadRequest request;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this] {
                return !loadQueue_.empty() || shutdownRequested_;
            });
            
            if (shutdownRequested_) {
                break;
            }
            
            if (!loadQueue_.empty()) {
                request = loadQueue_.top();
                loadQueue_.pop();
            } else {
                continue;
            }
        }
        
        processLoadRequest(request);
        pendingRequests_--;
    }
}

void AssetLoader::processLoadRequest(const LoadRequest& request) {
    // Same as sync load, but calls callback when done
    LoadedAsset* asset = loadSync(request.filePath);
    
    if (request.onComplete) {
        request.onComplete(asset, asset != nullptr);
    }
}

// ============================================================================
// SECTION LOADING
// ============================================================================

bool AssetLoader::loadHeader(const std::string& filePath, AssetHeader& outHeader) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.read(reinterpret_cast<char*>(&outHeader), sizeof(AssetHeader));
    return file.good();
}

bool AssetLoader::loadGeometrySection(LoadedAsset* asset, const std::vector<uint8_t>& data) {
    // Parse geometry data
    // Format: [uint32 vertexCount][uint32 indexCount][vertices...][indices...]
    
    if (data.size() < 8) {
        return false;
    }
    
    size_t offset = 0;
    
    uint32_t vertexCount, indexCount;
    std::memcpy(&vertexCount, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    std::memcpy(&indexCount, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    size_t vertexDataSize = vertexCount * sizeof(float) * 8;  // pos(3) + normal(3) + uv(2)
    size_t indexDataSize = indexCount * sizeof(uint32_t);
    
    if (data.size() < offset + vertexDataSize + indexDataSize) {
        return false;
    }
    
    // Create vertex buffer
    asset->vertexBuffer = createBuffer(
        vertexDataSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        asset->vertexMemory
    );
    
    uploadToBuffer(asset->vertexBuffer, data.data() + offset, vertexDataSize);
    offset += vertexDataSize;
    
    // Get buffer device address
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = asset->vertexBuffer;
    asset->vertexBufferAddress = vkGetBufferDeviceAddress(context_->getDevice(), &addressInfo);
    
    // Create index buffer
    asset->indexBuffer = createBuffer(
        indexDataSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        asset->indexMemory
    );
    
    uploadToBuffer(asset->indexBuffer, data.data() + offset, indexDataSize);
    
    addressInfo.buffer = asset->indexBuffer;
    asset->indexBufferAddress = vkGetBufferDeviceAddress(context_->getDevice(), &addressInfo);
    
    asset->vertexCount = vertexCount;
    asset->indexCount = indexCount;
    
    // Track memory
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        gpuMemoryUsed_ += vertexDataSize + indexDataSize;
    }
    
    return true;
}

bool AssetLoader::loadNaniteSection(LoadedAsset* asset, const std::vector<uint8_t>& data) {
    // Parse Nanite data
    // Format: [NaniteSectionHeader][clusters...][hierarchy...][meshlets...]
    
    if (data.size() < sizeof(NaniteSectionHeader)) {
        return false;
    }
    
    size_t offset = 0;
    
    NaniteSectionHeader naniteHeader;
    std::memcpy(&naniteHeader, data.data() + offset, sizeof(NaniteSectionHeader));
    offset += sizeof(NaniteSectionHeader);
    
    // Load clusters
    size_t clusterDataSize = naniteHeader.clusterCount * sizeof(CookedCluster);
    if (data.size() < offset + clusterDataSize) {
        return false;
    }
    
    asset->clusterBuffer = createBuffer(
        clusterDataSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | 
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        asset->naniteMemory  // Note: Using same memory object for simplicity
    );
    
    uploadToBuffer(asset->clusterBuffer, data.data() + offset, clusterDataSize);
    offset += clusterDataSize;
    
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = asset->clusterBuffer;
    asset->clusterBufferAddress = vkGetBufferDeviceAddress(context_->getDevice(), &addressInfo);
    asset->clusterCount = naniteHeader.clusterCount;
    
    // Load hierarchy
    size_t hierarchyDataSize = naniteHeader.hierarchyNodeCount * sizeof(CookedHierarchyNode);
    if (data.size() < offset + hierarchyDataSize) {
        return true;  // Clusters loaded successfully, hierarchy optional
    }
    
    VkDeviceMemory hierarchyMemory;
    asset->hierarchyBuffer = createBuffer(
        hierarchyDataSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        hierarchyMemory
    );
    
    uploadToBuffer(asset->hierarchyBuffer, data.data() + offset, hierarchyDataSize);
    offset += hierarchyDataSize;
    
    addressInfo.buffer = asset->hierarchyBuffer;
    asset->hierarchyBufferAddress = vkGetBufferDeviceAddress(context_->getDevice(), &addressInfo);
    asset->hierarchyNodeCount = naniteHeader.hierarchyNodeCount;
    
    // Load meshlets
    size_t meshletDataSize = naniteHeader.meshletCount * sizeof(CookedMeshlet);
    if (data.size() < offset + meshletDataSize) {
        return true;  // Previous data loaded
    }
    
    VkDeviceMemory meshletMemory;
    asset->meshletBuffer = createBuffer(
        meshletDataSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        meshletMemory
    );
    
    uploadToBuffer(asset->meshletBuffer, data.data() + offset, meshletDataSize);
    offset += meshletDataSize;
    
    addressInfo.buffer = asset->meshletBuffer;
    asset->meshletBufferAddress = vkGetBufferDeviceAddress(context_->getDevice(), &addressInfo);
    asset->meshletCount = naniteHeader.meshletCount;
    
    // Track memory
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        gpuMemoryUsed_ += clusterDataSize + hierarchyDataSize + meshletDataSize;
    }
    
    return true;
}

bool AssetLoader::loadLumenSection(LoadedAsset* asset, const std::vector<uint8_t>& data) {
    // Parse Lumen data
    // Format: [LumenSectionHeader][sdfData...][surfaceCards...]
    
    if (data.size() < sizeof(LumenSectionHeader)) {
        return false;
    }
    
    size_t offset = 0;
    
    LumenSectionHeader lumenHeader;
    std::memcpy(&lumenHeader, data.data() + offset, sizeof(LumenSectionHeader));
    offset += sizeof(LumenSectionHeader);
    
    // Load SDF volume
    asset->sdfResolution = glm::ivec3(
        lumenHeader.sdfResolutionX,
        lumenHeader.sdfResolutionY,
        lumenHeader.sdfResolutionZ
    );
    asset->sdfVoxelSize = lumenHeader.sdfVoxelSize;
    
    size_t sdfDataSize = asset->sdfResolution.x * asset->sdfResolution.y * 
                         asset->sdfResolution.z * sizeof(float);
    
    if (data.size() < offset + sdfDataSize) {
        return false;
    }
    
    // Create 3D texture for SDF
    asset->sdfVolume = createImage3D(
        asset->sdfResolution.x,
        asset->sdfResolution.y,
        asset->sdfResolution.z,
        VK_FORMAT_R32_SFLOAT,
        asset->sdfMemory
    );
    
    uploadToImage3D(asset->sdfVolume, data.data() + offset,
                   asset->sdfResolution.x, asset->sdfResolution.y, asset->sdfResolution.z,
                   VK_FORMAT_R32_SFLOAT);
    offset += sdfDataSize;
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = asset->sdfVolume;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    vkCreateImageView(context_->getDevice(), &viewInfo, nullptr, &asset->sdfVolumeView);
    
    // Load surface cards
    size_t cardDataSize = lumenHeader.surfaceCardCount * sizeof(CookedSurfaceCard);
    if (data.size() >= offset + cardDataSize && lumenHeader.surfaceCardCount > 0) {
        asset->surfaceCardBuffer = createBuffer(
            cardDataSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            asset->surfaceCardMemory
        );
        
        uploadToBuffer(asset->surfaceCardBuffer, data.data() + offset, cardDataSize);
        asset->surfaceCardCount = lumenHeader.surfaceCardCount;
    }
    
    // Track memory
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        gpuMemoryUsed_ += sdfDataSize + cardDataSize;
    }
    
    return true;
}

bool AssetLoader::loadPhysicsSection(LoadedAsset* asset, const std::vector<uint8_t>& data) {
    // Physics data is typically used by CPU physics engine (Jolt)
    // For now, we just validate the section exists
    // In a full implementation, this would be passed to the physics system
    
    if (data.size() < sizeof(PhysicsSectionHeader)) {
        return false;
    }
    
    // Physics data would be handled by PhysicsWorld, not stored in LoadedAsset
    return true;
}

// ============================================================================
// UNLOADING
// ============================================================================

void AssetLoader::unload(LoadedAsset* asset) {
    if (!asset) {
        return;
    }
    
    if (--asset->refCount > 0) {
        return;  // Still in use
    }
    
    // Free GPU resources
    VkDevice device = context_->getDevice();
    
    // Geometry
    if (asset->vertexBuffer) {
        vkDestroyBuffer(device, asset->vertexBuffer, nullptr);
        vkFreeMemory(device, asset->vertexMemory, nullptr);
    }
    if (asset->indexBuffer) {
        vkDestroyBuffer(device, asset->indexBuffer, nullptr);
        vkFreeMemory(device, asset->indexMemory, nullptr);
    }
    
    // Nanite
    if (asset->clusterBuffer) {
        vkDestroyBuffer(device, asset->clusterBuffer, nullptr);
    }
    if (asset->hierarchyBuffer) {
        vkDestroyBuffer(device, asset->hierarchyBuffer, nullptr);
    }
    if (asset->meshletBuffer) {
        vkDestroyBuffer(device, asset->meshletBuffer, nullptr);
    }
    if (asset->meshletVerticesBuffer) {
        vkDestroyBuffer(device, asset->meshletVerticesBuffer, nullptr);
    }
    if (asset->meshletTrianglesBuffer) {
        vkDestroyBuffer(device, asset->meshletTrianglesBuffer, nullptr);
    }
    if (asset->naniteMemory) {
        vkFreeMemory(device, asset->naniteMemory, nullptr);
    }
    
    // Lumen
    if (asset->sdfVolumeView) {
        vkDestroyImageView(device, asset->sdfVolumeView, nullptr);
    }
    if (asset->sdfVolume) {
        vkDestroyImage(device, asset->sdfVolume, nullptr);
        vkFreeMemory(device, asset->sdfMemory, nullptr);
    }
    if (asset->surfaceCardBuffer) {
        vkDestroyBuffer(device, asset->surfaceCardBuffer, nullptr);
        vkFreeMemory(device, asset->surfaceCardMemory, nullptr);
    }
    
    // Remove from cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        assetCache_.erase(asset->filePath);
    }
}

// ============================================================================
// PAGE STREAMING
// ============================================================================

void AssetLoader::requestPages(LoadedAsset* asset, const std::vector<uint32_t>& pageIndices, LoadPriority priority) {
    // Mark pages as requested
    for (uint32_t pageIndex : pageIndices) {
        if (pageIndex < asset->pageStates.size()) {
            auto& state = asset->pageStates[pageIndex];
            if (state.state == PageState::NotLoaded) {
                state.state = PageState::Loading;
                
                // Create load request for this page
                LoadRequest request;
                request.filePath = asset->filePath;
                request.priority = priority;
                request.assetId = std::hash<std::string>{}(asset->filePath) & 0xFFFFFFFF;
                request.pagesToLoad.push_back(pageIndex);
                
                loadAsync(request);
            }
        }
    }
}

void AssetLoader::updateStreaming(float deltaTime) {
    currentFrame_++;
    
    // Evict pages that haven't been used recently
    std::lock_guard<std::mutex> lock(lruMutex_);
    
    while (!lruList_.empty()) {
        auto& oldest = lruList_.back();
        if (currentFrame_ - oldest.frameLastUsed > config_.pageRetentionFrames) {
            // Evict this page
            uint64_t key = (uint64_t(std::hash<LoadedAsset*>{}(oldest.asset)) << 32) | oldest.pageIndex;
            lruMap_.erase(key);
            
            if (oldest.pageIndex < oldest.asset->pageStates.size()) {
                oldest.asset->pageStates[oldest.pageIndex].state = PageState::NotLoaded;
                oldest.asset->residentPageCount--;
            }
            
            lruList_.pop_back();
        } else {
            break;  // Rest of list is more recent
        }
    }
}

void AssetLoader::touchPage(LoadedAsset* asset, uint32_t pageIndex) {
    std::lock_guard<std::mutex> lock(lruMutex_);
    
    uint64_t key = (uint64_t(std::hash<LoadedAsset*>{}(asset)) << 32) | pageIndex;
    auto it = lruMap_.find(key);
    
    if (it != lruMap_.end()) {
        // Move to front of LRU list
        it->second->frameLastUsed = currentFrame_;
        lruList_.splice(lruList_.begin(), lruList_, it->second);
    } else {
        // Add new entry
        lruList_.push_front({asset, pageIndex, currentFrame_});
        lruMap_[key] = lruList_.begin();
    }
}

// ============================================================================
// CACHE MANAGEMENT
// ============================================================================

void AssetLoader::setMemoryBudget(uint64_t gpuBytes, uint64_t cpuBytes) {
    config_.maxGpuMemoryBytes = gpuBytes;
    config_.maxCpuMemoryBytes = cpuBytes;
    
    // Trim if over budget
    trimCache(gpuBytes);
}

void AssetLoader::trimCache(uint64_t targetSize) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // Find assets with zero refcount, sorted by last use
    std::vector<std::pair<std::string, uint32_t>> candidates;
    
    for (auto& [path, asset] : assetCache_) {
        if (asset->refCount == 0) {
            candidates.push_back({path, 0});  // TODO: track last use time
        }
    }
    
    // Evict until under budget
    while (gpuMemoryUsed_ > targetSize && !candidates.empty()) {
        auto& path = candidates.back().first;
        auto it = assetCache_.find(path);
        if (it != assetCache_.end()) {
            unload(it->second.get());
        }
        candidates.pop_back();
    }
}

void AssetLoader::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    for (auto& [path, asset] : assetCache_) {
        asset->refCount = 0;  // Force unload
        unload(asset.get());
    }
    assetCache_.clear();
}

LoadedAsset* AssetLoader::getAsset(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = assetCache_.find(filePath);
    if (it != assetCache_.end()) {
        return it->second.get();
    }
    return nullptr;
}

// ============================================================================
// BUFFER CREATION
// ============================================================================

VkBuffer AssetLoader::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags properties, VkDeviceMemory& memory) {
    VkBuffer buffer;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(context_->getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context_->getDevice(), buffer, &memRequirements);
    
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &allocFlagsInfo : nullptr;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(memRequirements.memoryTypeBits, properties);
    
    if (vkAllocateMemory(context_->getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(context_->getDevice(), buffer, nullptr);
        return VK_NULL_HANDLE;
    }
    
    vkBindBufferMemory(context_->getDevice(), buffer, memory, 0);
    
    return buffer;
}

void AssetLoader::uploadToBuffer(VkBuffer buffer, const void* data, VkDeviceSize size) {
    // Get staging buffer
    StagingBuffer* staging = acquireStagingBuffer(size);
    if (!staging) {
        return;
    }
    
    // Copy data to staging
    std::memcpy(staging->mapped, data, size);
    
    // Record copy command
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = transferCommandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    vkAllocateCommandBuffers(context_->getDevice(), &allocInfo, &cmd);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging->buffer, buffer, 1, &copyRegion);
    
    vkEndCommandBuffer(cmd);
    
    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    vkQueueSubmit(transferQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(transferQueue_);  // TODO: Use fences for async
    
    vkFreeCommandBuffers(context_->getDevice(), transferCommandPool_, 1, &cmd);
    releaseStagingBuffer(staging);
}

VkImage AssetLoader::createImage3D(uint32_t width, uint32_t height, uint32_t depth,
                                    VkFormat format, VkDeviceMemory& memory) {
    VkImage image;
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = depth;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(context_->getDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(context_->getDevice(), image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(memRequirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(context_->getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(context_->getDevice(), image, nullptr);
        return VK_NULL_HANDLE;
    }
    
    vkBindImageMemory(context_->getDevice(), image, memory, 0);
    
    return image;
}

void AssetLoader::uploadToImage3D(VkImage image, const void* data,
                                   uint32_t width, uint32_t height, uint32_t depth, VkFormat format) {
    // Calculate data size
    uint32_t bytesPerPixel = 4;  // Assuming R32_SFLOAT
    VkDeviceSize size = width * height * depth * bytesPerPixel;
    
    // Get staging buffer
    StagingBuffer* staging = acquireStagingBuffer(size);
    if (!staging) {
        return;
    }
    
    std::memcpy(staging->mapped, data, size);
    
    // Record commands
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = transferCommandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    vkAllocateCommandBuffers(context_->getDevice(), &allocInfo, &cmd);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Transition image layout
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, depth};
    
    vkCmdCopyBufferToImage(cmd, staging->buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkEndCommandBuffer(cmd);
    
    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    vkQueueSubmit(transferQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(transferQueue_);
    
    vkFreeCommandBuffers(context_->getDevice(), transferCommandPool_, 1, &cmd);
    releaseStagingBuffer(staging);
}

AssetLoader::StagingBuffer* AssetLoader::acquireStagingBuffer(VkDeviceSize size) {
    std::lock_guard<std::mutex> lock(stagingMutex_);
    
    for (auto& staging : stagingBuffers_) {
        if (!staging.inUse && staging.size >= size) {
            staging.inUse = true;
            return &staging;
        }
    }
    
    // Need to allocate a new larger staging buffer
    // For now, just return first available and hope for the best
    for (auto& staging : stagingBuffers_) {
        if (!staging.inUse) {
            // Resize if needed
            if (staging.size < size) {
                vkUnmapMemory(context_->getDevice(), staging.memory);
                vkDestroyBuffer(context_->getDevice(), staging.buffer, nullptr);
                vkFreeMemory(context_->getDevice(), staging.memory, nullptr);
                
                staging.size = size;
                staging.buffer = createBuffer(
                    size,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    staging.memory
                );
                vkMapMemory(context_->getDevice(), staging.memory, 0, size, 0, &staging.mapped);
            }
            staging.inUse = true;
            return &staging;
        }
    }
    
    return nullptr;
}

void AssetLoader::releaseStagingBuffer(StagingBuffer* buffer) {
    std::lock_guard<std::mutex> lock(stagingMutex_);
    buffer->inUse = false;
}

// ============================================================================
// STATISTICS
// ============================================================================

AssetLoader::Stats AssetLoader::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    Stats stats;
    stats.gpuMemoryUsed = gpuMemoryUsed_;
    stats.cpuMemoryUsed = cpuMemoryUsed_;
    stats.assetsLoaded = static_cast<uint32_t>(assetCache_.size());
    stats.pagesResident = 0;  // TODO: Count from all assets
    stats.pagesStreaming = 0;
    stats.loadRequestsPending = pendingRequests_.load();
    stats.averageLoadTimeMs = loadCount_ > 0 ? static_cast<float>(totalLoadTime_ / loadCount_) : 0.0f;
    
    return stats;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

bool isValidSanicMesh(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    
    return file.good() && magic == SANIC_MESH_MAGIC;
}

bool getAssetInfo(const std::string& filePath, AssetHeader& outHeader) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.read(reinterpret_cast<char*>(&outHeader), sizeof(AssetHeader));
    
    return file.good() && outHeader.magic == SANIC_MESH_MAGIC;
}

} // namespace Sanic
