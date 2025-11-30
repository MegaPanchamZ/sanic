/**
 * TextureStreamer.cpp
 * 
 * Implementation of virtual texture streaming system.
 */

#include "TextureStreamer.h"
#include "VulkanContext.h"

// Note: stb_image is already implemented in stb_image_impl.cpp
#include "../../external/stb_image.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <cmath>

namespace Sanic {

TextureStreamer::~TextureStreamer() {
    shutdown();
}

bool TextureStreamer::initialize(VulkanContext* context, const StreamingConfig& config) {
    if (initialized_) {
        return true;
    }
    
    context_ = context;
    config_ = config;
    
    if (!createFeedbackBuffer()) {
        return false;
    }
    
    if (!createResidencyBuffer()) {
        return false;
    }
    
    if (!createStagingBuffer()) {
        return false;
    }
    
    if (!createSampler()) {
        return false;
    }
    
    // Start streaming threads
    shutdownRequested_ = false;
    for (uint32_t i = 0; i < config_.maxConcurrentLoads; ++i) {
        streamingThreads_.emplace_back(&TextureStreamer::streamingThreadFunc, this);
    }
    
    initialized_ = true;
    return true;
}

void TextureStreamer::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Signal shutdown to streaming threads
    {
        std::lock_guard<std::mutex> lock(streamingMutex_);
        shutdownRequested_ = true;
    }
    streamingCondition_.notify_all();
    
    // Wait for threads to finish
    for (auto& thread : streamingThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    streamingThreads_.clear();
    
    VkDevice device = context_->getDevice();
    
    // Cleanup textures
    for (auto& [id, state] : textures_) {
        if (state.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, state.view, nullptr);
        }
        if (state.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, state.image, nullptr);
        }
        if (state.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, state.memory, nullptr);
        }
    }
    textures_.clear();
    
    // Cleanup buffers
    if (feedbackBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, feedbackBuffer_, nullptr);
        feedbackBuffer_ = VK_NULL_HANDLE;
    }
    if (feedbackMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, feedbackMemory_, nullptr);
        feedbackMemory_ = VK_NULL_HANDLE;
    }
    
    if (feedbackCounterBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, feedbackCounterBuffer_, nullptr);
        feedbackCounterBuffer_ = VK_NULL_HANDLE;
    }
    if (feedbackCounterMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, feedbackCounterMemory_, nullptr);
        feedbackCounterMemory_ = VK_NULL_HANDLE;
    }
    
    if (residencyBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, residencyBuffer_, nullptr);
        residencyBuffer_ = VK_NULL_HANDLE;
    }
    if (residencyMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, residencyMemory_, nullptr);
        residencyMemory_ = VK_NULL_HANDLE;
    }
    
    if (stagingBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
    }
    if (stagingMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, stagingMemory_, nullptr);
        stagingMemory_ = VK_NULL_HANDLE;
    }
    
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    
    initialized_ = false;
}

bool TextureStreamer::createFeedbackBuffer() {
    VkDevice device = context_->getDevice();
    
    // Create feedback buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = config_.feedbackBufferSize * sizeof(FeedbackEntry) + sizeof(uint32_t);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &feedbackBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    // Allocate host-visible memory for CPU readback
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, feedbackBuffer_, &memReqs);
    
    VkMemoryAllocateFlagsInfo allocFlags{};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlags;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &feedbackMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, feedbackBuffer_, feedbackMemory_, 0);
    
    // Map for CPU access
    vkMapMemory(device, feedbackMemory_, 0, bufferInfo.size, 0, &feedbackMapped_);
    
    // Get device address
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = feedbackBuffer_;
    feedbackBufferAddress_ = vkGetBufferDeviceAddress(device, &addressInfo);
    
    return true;
}

bool TextureStreamer::createResidencyBuffer() {
    VkDevice device = context_->getDevice();
    
    // Max textures * 1 byte (lowest resident mip per texture)
    const uint32_t maxTextures = 4096;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = maxTextures * sizeof(uint8_t);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &residencyBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, residencyBuffer_, &memReqs);
    
    VkMemoryAllocateFlagsInfo allocFlags{};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlags;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &residencyMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, residencyBuffer_, residencyMemory_, 0);
    vkMapMemory(device, residencyMemory_, 0, bufferInfo.size, 0, &residencyMapped_);
    
    // Initialize all to max mip (nothing loaded)
    memset(residencyMapped_, 0xFF, bufferInfo.size);
    
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = residencyBuffer_;
    residencyBufferAddress_ = vkGetBufferDeviceAddress(device, &addressInfo);
    
    return true;
}

bool TextureStreamer::createStagingBuffer() {
    VkDevice device = context_->getDevice();
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = config_.stagingBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, stagingBuffer_, stagingMemory_, 0);
    vkMapMemory(device, stagingMemory_, 0, config_.stagingBufferSize, 0, &stagingMapped_);
    
    return true;
}

bool TextureStreamer::createSampler() {
    VkDevice device = context_->getDevice();
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = static_cast<float>(config_.maxAnisotropy);
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    
    return vkCreateSampler(device, &samplerInfo, nullptr, &sampler_) == VK_SUCCESS;
}

uint32_t TextureStreamer::registerTexture(const std::string& path) {
    std::lock_guard<std::mutex> lock(texturesMutex_);
    
    uint32_t id = nextTextureId_++;
    TextureStreamState& state = textures_[id];
    state.path = path;
    
    // Load metadata (dimensions, mip count) without full texture data
    int width, height, channels;
    if (stbi_info(path.c_str(), &width, &height, &channels)) {
        state.width = static_cast<uint32_t>(width);
        state.height = static_cast<uint32_t>(height);
        state.mipLevels = static_cast<uint32_t>(
            std::floor(std::log2(std::max(width, height))) + 1
        );
        state.format = channels == 4 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8_SRGB;
        
        // Initialize residency tracking
        state.mipResidency.resize(state.mipLevels, MipResidency::NotLoaded);
        state.lowestResidentMip = UINT32_MAX;
        
        // Request lowest quality mips immediately
        for (uint32_t i = 0; i < config_.mipsToPreload && i < state.mipLevels; ++i) {
            uint32_t mipToLoad = state.mipLevels - 1 - i; // Start from smallest
            StreamRequest request;
            request.textureId = id;
            request.mipLevel = mipToLoad;
            request.priority = StreamPriority::Normal;
            request.screenCoverage = 0.0f;
            request.frameRequested = currentFrame_;
            
            std::lock_guard<std::mutex> reqLock(requestMutex_);
            requestQueue_.push(request);
        }
        
        // Create the GPU image (all mips, sparse if supported)
        // For simplicity, we create the full image and stream mip data
        createGPUImage(id, state);
    }
    
    return id;
}

void TextureStreamer::createGPUImage(uint32_t textureId, TextureStreamState& state) {
    VkDevice device = context_->getDevice();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = state.format;
    imageInfo.extent = {state.width, state.height, 1};
    imageInfo.mipLevels = state.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &state.image) != VK_SUCCESS) {
        return;
    }
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, state.image, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &state.memory) != VK_SUCCESS) {
        vkDestroyImage(device, state.image, nullptr);
        state.image = VK_NULL_HANDLE;
        return;
    }
    
    vkBindImageMemory(device, state.image, state.memory, 0);
    state.memoryUsage = memReqs.size;
    currentMemoryUsage_ += memReqs.size;
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = state.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = state.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = state.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    vkCreateImageView(device, &viewInfo, nullptr, &state.view);
}

void TextureStreamer::unregisterTexture(uint32_t textureId) {
    std::lock_guard<std::mutex> lock(texturesMutex_);
    
    auto it = textures_.find(textureId);
    if (it == textures_.end()) {
        return;
    }
    
    VkDevice device = context_->getDevice();
    TextureStreamState& state = it->second;
    
    currentMemoryUsage_ -= state.memoryUsage;
    
    if (state.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, state.view, nullptr);
    }
    if (state.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, state.image, nullptr);
    }
    if (state.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, state.memory, nullptr);
    }
    
    textures_.erase(it);
}

void TextureStreamer::beginFrame(uint64_t frameNumber) {
    currentFrame_ = frameNumber;
    loadsThisFrame_ = 0;
    evictionsThisFrame_ = 0;
    
    // Reset feedback counter
    if (feedbackMapped_) {
        *static_cast<uint32_t*>(feedbackMapped_) = 0;
    }
}

void TextureStreamer::update(VkCommandBuffer cmd) {
    processFeedback();
    processRequestQueue();
    performEviction();
    updateResidencyBuffer(cmd);
}

void TextureStreamer::processFeedback() {
    if (!feedbackMapped_) return;
    
    uint32_t* counterPtr = static_cast<uint32_t*>(feedbackMapped_);
    uint32_t count = std::min(*counterPtr, config_.feedbackBufferSize);
    
    FeedbackEntry* entries = reinterpret_cast<FeedbackEntry*>(counterPtr + 1);
    
    std::lock_guard<std::mutex> lock(texturesMutex_);
    
    for (uint32_t i = 0; i < count; ++i) {
        FeedbackEntry& entry = entries[i];
        
        auto it = textures_.find(entry.textureId);
        if (it == textures_.end()) continue;
        
        TextureStreamState& state = it->second;
        state.lastAccessFrame = currentFrame_;
        
        uint32_t requestedMip = entry.requestedMip;
        if (requestedMip >= state.mipLevels) continue;
        
        // Check if we need to load this mip
        if (state.mipResidency[requestedMip] == MipResidency::NotLoaded) {
            StreamRequest request;
            request.textureId = entry.textureId;
            request.mipLevel = requestedMip;
            request.priority = StreamPriority::High;
            request.screenCoverage = 1.0f; // Approximate
            request.frameRequested = currentFrame_;
            
            std::lock_guard<std::mutex> reqLock(requestMutex_);
            requestQueue_.push(request);
        }
    }
}

void TextureStreamer::processRequestQueue() {
    // Signal streaming threads that there's work
    streamingCondition_.notify_all();
}

void TextureStreamer::performEviction() {
    // Check if we need to evict
    if (currentMemoryUsage_ <= config_.gpuMemoryBudget) {
        return;
    }
    
    uint64_t targetBytes = currentMemoryUsage_ - config_.gpuMemoryBudget;
    requestEviction(targetBytes);
}

void TextureStreamer::requestEviction(uint64_t bytesToFree) {
    std::lock_guard<std::mutex> lock(texturesMutex_);
    
    // Build LRU list
    std::vector<std::pair<uint64_t, std::pair<uint32_t, uint32_t>>> candidates; // {frame, {textureId, mip}}
    
    for (auto& [id, state] : textures_) {
        // Don't evict preload mips
        for (uint32_t mip = 0; mip < state.mipLevels - config_.mipsToPreload; ++mip) {
            if (state.mipResidency[mip] == MipResidency::Resident) {
                candidates.push_back({state.lastAccessFrame, {id, mip}});
            }
        }
    }
    
    // Sort by LRU (oldest first)
    std::sort(candidates.begin(), candidates.end());
    
    uint64_t freedBytes = 0;
    for (auto& [frame, idMip] : candidates) {
        if (freedBytes >= bytesToFree) break;
        if (currentFrame_ - frame < config_.framesBeforeEvict) continue;
        
        auto& [textureId, mip] = idMip;
        TextureStreamState& state = textures_[textureId];
        
        // Mark for eviction
        state.mipResidency[mip] = MipResidency::PendingEvict;
        
        // Calculate freed memory (approximate)
        uint32_t mipWidth = std::max(1u, state.width >> mip);
        uint32_t mipHeight = std::max(1u, state.height >> mip);
        uint64_t mipSize = mipWidth * mipHeight * 4; // Assume 4 bytes per pixel
        
        freedBytes += mipSize;
        evictionsThisFrame_++;
    }
}

void TextureStreamer::updateResidencyBuffer(VkCommandBuffer cmd) {
    if (!residencyMapped_) return;
    
    uint8_t* residencyData = static_cast<uint8_t*>(residencyMapped_);
    
    std::lock_guard<std::mutex> lock(texturesMutex_);
    
    for (auto& [id, state] : textures_) {
        if (id >= 4096) continue;
        
        // Find lowest resident mip
        uint32_t lowestMip = UINT32_MAX;
        for (uint32_t mip = 0; mip < state.mipLevels; ++mip) {
            if (state.mipResidency[mip] == MipResidency::Resident) {
                lowestMip = mip;
                break;
            }
        }
        
        state.lowestResidentMip = lowestMip;
        residencyData[id] = static_cast<uint8_t>(std::min(lowestMip, 255u));
    }
}

void TextureStreamer::streamingThreadFunc() {
    while (!shutdownRequested_) {
        StreamRequest request;
        bool hasRequest = false;
        
        {
            std::unique_lock<std::mutex> lock(requestMutex_);
            if (requestQueue_.empty()) {
                std::unique_lock<std::mutex> streamLock(streamingMutex_);
                streamingCondition_.wait_for(streamLock, std::chrono::milliseconds(100));
                continue;
            }
            
            request = requestQueue_.top();
            requestQueue_.pop();
            hasRequest = true;
        }
        
        if (hasRequest) {
            // Check if already loading
            uint64_t key = (static_cast<uint64_t>(request.textureId) << 32) | request.mipLevel;
            
            {
                std::lock_guard<std::mutex> lock(pendingMutex_);
                if (pendingLoads_.count(key)) {
                    continue;
                }
                pendingLoads_.insert(key);
            }
            
            loadMipLevel(request.textureId, request.mipLevel);
            
            {
                std::lock_guard<std::mutex> lock(pendingMutex_);
                pendingLoads_.erase(key);
            }
        }
    }
}

void TextureStreamer::loadMipLevel(uint32_t textureId, uint32_t mipLevel) {
    std::string path;
    uint32_t width, height;
    VkFormat format;
    
    {
        std::lock_guard<std::mutex> lock(texturesMutex_);
        auto it = textures_.find(textureId);
        if (it == textures_.end()) return;
        
        TextureStreamState& state = it->second;
        path = state.path;
        width = std::max(1u, state.width >> mipLevel);
        height = std::max(1u, state.height >> mipLevel);
        format = state.format;
        
        state.mipResidency[mipLevel] = MipResidency::Loading;
    }
    
    // Load image data
    int imgWidth, imgHeight, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &imgWidth, &imgHeight, &channels, STBI_rgb_alpha);
    
    if (!pixels) {
        std::lock_guard<std::mutex> lock(texturesMutex_);
        auto it = textures_.find(textureId);
        if (it != textures_.end()) {
            it->second.mipResidency[mipLevel] = MipResidency::NotLoaded;
        }
        return;
    }
    
    // Generate the specific mip level
    std::vector<uint8_t> mipData;
    if (mipLevel == 0) {
        mipData.assign(pixels, pixels + imgWidth * imgHeight * 4);
    } else {
        // Simple box filter for mip generation
        uint32_t srcWidth = imgWidth;
        uint32_t srcHeight = imgHeight;
        std::vector<uint8_t> srcData(pixels, pixels + srcWidth * srcHeight * 4);
        
        for (uint32_t m = 0; m < mipLevel; ++m) {
            uint32_t dstWidth = std::max(1u, srcWidth / 2);
            uint32_t dstHeight = std::max(1u, srcHeight / 2);
            std::vector<uint8_t> dstData(dstWidth * dstHeight * 4);
            
            for (uint32_t y = 0; y < dstHeight; ++y) {
                for (uint32_t x = 0; x < dstWidth; ++x) {
                    uint32_t sx = x * 2;
                    uint32_t sy = y * 2;
                    
                    for (int c = 0; c < 4; ++c) {
                        uint32_t sum = 0;
                        sum += srcData[(sy * srcWidth + sx) * 4 + c];
                        sum += srcData[(sy * srcWidth + std::min(sx + 1, srcWidth - 1)) * 4 + c];
                        sum += srcData[(std::min(sy + 1, srcHeight - 1) * srcWidth + sx) * 4 + c];
                        sum += srcData[(std::min(sy + 1, srcHeight - 1) * srcWidth + std::min(sx + 1, srcWidth - 1)) * 4 + c];
                        dstData[(y * dstWidth + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
                    }
                }
            }
            
            srcData = std::move(dstData);
            srcWidth = dstWidth;
            srcHeight = dstHeight;
        }
        
        mipData = std::move(srcData);
    }
    
    stbi_image_free(pixels);
    
    // Upload to GPU (simplified - in production, use proper command buffer submission)
    uploadMipLevel(textureId, mipLevel, mipData.data(), mipData.size());
    
    // Mark as resident
    {
        std::lock_guard<std::mutex> lock(texturesMutex_);
        auto it = textures_.find(textureId);
        if (it != textures_.end()) {
            it->second.mipResidency[mipLevel] = MipResidency::Resident;
            loadsThisFrame_++;
        }
    }
}

void TextureStreamer::uploadMipLevel(uint32_t textureId, uint32_t mipLevel, 
                                      const void* data, size_t size) {
    // This is a simplified upload - in production, you'd use a proper
    // transfer queue and synchronization
    
    // Copy to staging buffer
    uint64_t offset = stagingOffset_.fetch_add(size);
    if (offset + size > config_.stagingBufferSize) {
        stagingOffset_ = 0;
        offset = 0;
    }
    
    memcpy(static_cast<uint8_t*>(stagingMapped_) + offset, data, size);
    
    // Submit copy command
    // Note: This needs proper synchronization with the render thread
    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    
    std::lock_guard<std::mutex> lock(texturesMutex_);
    auto it = textures_.find(textureId);
    if (it == textures_.end() || it->second.image == VK_NULL_HANDLE) {
        context_->endSingleTimeCommands(cmd);
        return;
    }
    
    TextureStreamState& state = it->second;
    uint32_t mipWidth = std::max(1u, state.width >> mipLevel);
    uint32_t mipHeight = std::max(1u, state.height >> mipLevel);
    
    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = state.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {mipWidth, mipHeight, 1};
    
    vkCmdCopyBufferToImage(cmd, stagingBuffer_, state.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    context_->endSingleTimeCommands(cmd);
}

VkImageView TextureStreamer::getTextureView(uint32_t textureId) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(texturesMutex_));
    auto it = textures_.find(textureId);
    if (it != textures_.end()) {
        return it->second.view;
    }
    return VK_NULL_HANDLE;
}

TextureStreamer::Statistics TextureStreamer::getStatistics() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(texturesMutex_));
    
    Statistics stats{};
    stats.texturesRegistered = textures_.size();
    stats.gpuMemoryUsed = currentMemoryUsage_;
    stats.gpuMemoryBudget = config_.gpuMemoryBudget;
    stats.loadsThisFrame = loadsThisFrame_;
    stats.evictionsThisFrame = evictionsThisFrame_;
    
    {
        std::lock_guard<std::mutex> lock2(const_cast<std::mutex&>(requestMutex_));
        stats.pendingRequests = requestQueue_.size();
    }
    
    for (auto& [id, state] : textures_) {
        if (state.isFullyResident()) {
            stats.texturesFullyResident++;
        }
    }
    
    return stats;
}

void TextureStreamer::forceLoad(uint32_t textureId, uint32_t targetMip) {
    std::lock_guard<std::mutex> lock(texturesMutex_);
    auto it = textures_.find(textureId);
    if (it == textures_.end()) return;
    
    TextureStreamState& state = it->second;
    
    for (uint32_t mip = targetMip; mip < state.mipLevels; ++mip) {
        if (state.mipResidency[mip] == MipResidency::NotLoaded) {
            StreamRequest request;
            request.textureId = textureId;
            request.mipLevel = mip;
            request.priority = StreamPriority::Critical;
            request.screenCoverage = 1.0f;
            request.frameRequested = currentFrame_;
            
            std::lock_guard<std::mutex> reqLock(requestMutex_);
            requestQueue_.push(request);
        }
    }
}

} // namespace Sanic
