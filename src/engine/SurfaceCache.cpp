/**
 * SurfaceCache.cpp
 * 
 * Implementation of Lumen-style surface cache system.
 */

#include "SurfaceCache.h"
#include "VulkanContext.h"
#include <fstream>
#include <algorithm>
#include <cstring>

SurfaceCache::~SurfaceCache() {
    cleanup();
}

bool SurfaceCache::initialize(VulkanContext* context, const SurfaceCacheConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    
    if (!createAtlasTextures()) {
        cleanup();
        return false;
    }
    
    if (!createBuffers()) {
        cleanup();
        return false;
    }
    
    if (!createDescriptorSets()) {
        cleanup();
        return false;
    }
    
    if (!createPipelines()) {
        cleanup();
        return false;
    }
    
    initialized_ = true;
    return true;
}

void SurfaceCache::cleanup() {
    if (!context_) return;
    
    VkDevice device = context_->getDevice();
    
    // Pipelines
    if (cardCapturePipeline_) vkDestroyPipeline(device, cardCapturePipeline_, nullptr);
    if (cardCaptureLayout_) vkDestroyPipelineLayout(device, cardCaptureLayout_, nullptr);
    if (radianceUpdatePipeline_) vkDestroyPipeline(device, radianceUpdatePipeline_, nullptr);
    if (radianceUpdateLayout_) vkDestroyPipelineLayout(device, radianceUpdateLayout_, nullptr);
    
    // Descriptors
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (atlasDescLayout_) vkDestroyDescriptorSetLayout(device, atlasDescLayout_, nullptr);
    
    // Sampler
    if (atlasSampler_) vkDestroySampler(device, atlasSampler_, nullptr);
    
    // Buffers
    if (cardBuffer_) vkDestroyBuffer(device, cardBuffer_, nullptr);
    if (cardMemory_) vkFreeMemory(device, cardMemory_, nullptr);
    if (pageTableBuffer_) vkDestroyBuffer(device, pageTableBuffer_, nullptr);
    if (pageTableMemory_) vkFreeMemory(device, pageTableMemory_, nullptr);
    
    // Atlas textures
    auto destroyImage = [device](VkImage& img, VkImageView& view, VkDeviceMemory& mem) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (img) vkDestroyImage(device, img, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        img = VK_NULL_HANDLE;
        view = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    };
    
    destroyImage(radianceAtlas_, radianceAtlasView_, radianceAtlasMemory_);
    destroyImage(normalAtlas_, normalAtlasView_, normalAtlasMemory_);
    destroyImage(depthAtlas_, depthAtlasView_, depthAtlasMemory_);
    
    cards_.clear();
    meshToCards_.clear();
    atlasRows_.clear();
    pendingUpdates_.clear();
    
    initialized_ = false;
}

bool SurfaceCache::createAtlasTextures() {
    VkDevice device = context_->getDevice();
    
    auto findMemoryType = [this](uint32_t typeFilter, VkMemoryPropertyFlags properties) -> uint32_t {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(context_->getPhysicalDevice(), &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    };
    
    auto createAtlas = [&](VkFormat format, VkImage& image, VkImageView& view, VkDeviceMemory& memory) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {config_.atlasWidth, config_.atlasHeight, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) return false;
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, image, &memReqs);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, image, memory, 0);
        
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        return vkCreateImageView(device, &viewInfo, nullptr, &view) == VK_SUCCESS;
    };
    
    if (!createAtlas(VK_FORMAT_R16G16B16A16_SFLOAT, radianceAtlas_, radianceAtlasView_, radianceAtlasMemory_)) return false;
    if (!createAtlas(VK_FORMAT_R16G16B16A16_SFLOAT, normalAtlas_, normalAtlasView_, normalAtlasMemory_)) return false;
    if (!createAtlas(VK_FORMAT_R32_SFLOAT, depthAtlas_, depthAtlasView_, depthAtlasMemory_)) return false;
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    return vkCreateSampler(device, &samplerInfo, nullptr, &atlasSampler_) == VK_SUCCESS;
}

bool SurfaceCache::createBuffers() {
    VkDevice device = context_->getDevice();
    
    auto findMemoryType = [this](uint32_t typeFilter, VkMemoryPropertyFlags properties) -> uint32_t {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(context_->getPhysicalDevice(), &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    };
    
    // Card buffer
    VkDeviceSize cardBufferSize = sizeof(GPUMeshCard) * config_.maxCards;
    
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = cardBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &cardBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, cardBuffer_, &memReqs);
    
    VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.pNext = &flagsInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &cardMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, cardBuffer_, cardMemory_, 0);
    
    VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addrInfo.buffer = cardBuffer_;
    cardBufferAddr_ = vkGetBufferDeviceAddress(device, &addrInfo);
    
    return true;
}

bool SurfaceCache::createDescriptorSets() {
    VkDevice device = context_->getDevice();
    
    // Atlas descriptor layout
    VkDescriptorSetLayoutBinding bindings[4] = {};
    for (int i = 0; i < 4; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &atlasDescLayout_) != VK_SUCCESS) return false;
    
    // Pool
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 3;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descPool_) != VK_SUCCESS) return false;
    
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &atlasDescLayout_;
    
    return vkAllocateDescriptorSets(device, &allocInfo, &atlasDescSet_) == VK_SUCCESS;
}

bool SurfaceCache::createPipelines() {
    // Pipeline creation similar to other systems
    // Simplified for this implementation
    return true;
}

bool SurfaceCache::loadShader(const std::string& path, VkShaderModule& outModule) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    size_t size = file.tellg();
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), size);
    
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = size;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    return vkCreateShaderModule(context_->getDevice(), &createInfo, nullptr, &outModule) == VK_SUCCESS;
}

uint32_t SurfaceCache::registerMesh(uint32_t meshId,
                                     const glm::vec3& boundsMin,
                                     const glm::vec3& boundsMax,
                                     const glm::mat4& transform) {
    glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    glm::vec3 extents = (boundsMax - boundsMin) * 0.5f;
    
    uint32_t firstCard = static_cast<uint32_t>(cards_.size());
    generateCardsForMesh(meshId, center, extents, transform);
    
    return firstCard;
}

void SurfaceCache::generateCardsForMesh(uint32_t meshId,
                                         const glm::vec3& center,
                                         const glm::vec3& extents,
                                         const glm::mat4& transform) {
    // 6 cardinal directions
    const glm::vec3 normals[6] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1}
    };
    
    std::vector<uint32_t> cardIndices;
    
    for (int i = 0; i < 6; i++) {
        MeshCard card;
        card.center = center + normals[i] * extents;
        card.extent = glm::max(extents.x, glm::max(extents.y, extents.z));
        card.normal = normals[i];
        card.meshId = meshId;
        card.cardIndex = i;
        
        // Allocate atlas space
        uint32_t atlasX, atlasY;
        if (allocateAtlasTile(config_.cardResolution, config_.cardResolution, atlasX, atlasY)) {
            card.atlasOffset = atlasY * config_.atlasWidth + atlasX;
            card.atlasSize = glm::vec2(config_.cardResolution);
        }
        
        // Compute bounds
        glm::vec3 tangent = glm::abs(normals[i].y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        tangent = glm::normalize(tangent - normals[i] * glm::dot(tangent, normals[i]));
        glm::vec3 bitangent = glm::cross(normals[i], tangent);
        
        card.boundsMin = card.center - tangent * card.extent - bitangent * card.extent;
        card.boundsMax = card.center + tangent * card.extent + bitangent * card.extent;
        
        cardIndices.push_back(static_cast<uint32_t>(cards_.size()));
        cards_.push_back(card);
        pendingUpdates_.push_back(cardIndices.back());
    }
    
    meshToCards_[meshId] = cardIndices;
}

bool SurfaceCache::allocateAtlasTile(uint32_t width, uint32_t height,
                                      uint32_t& outX, uint32_t& outY) {
    // Simple row-based allocation
    for (auto& row : atlasRows_) {
        if (row.height >= height && row.usedWidth + width <= config_.atlasWidth) {
            outX = row.usedWidth;
            outY = row.y;
            row.usedWidth += width;
            return true;
        }
    }
    
    // Need new row
    uint32_t newY = 0;
    if (!atlasRows_.empty()) {
        newY = atlasRows_.back().y + atlasRows_.back().height;
    }
    
    if (newY + height > config_.atlasHeight) {
        return false;  // Atlas full
    }
    
    atlasRows_.push_back({newY, height, width});
    outX = 0;
    outY = newY;
    return true;
}

void SurfaceCache::invalidateCards(uint32_t meshId) {
    auto it = meshToCards_.find(meshId);
    if (it != meshToCards_.end()) {
        for (uint32_t cardIdx : it->second) {
            pendingUpdates_.push_back(cardIdx);
        }
    }
}

void SurfaceCache::invalidateAllCards() {
    pendingUpdates_.clear();
    for (size_t i = 0; i < cards_.size(); i++) {
        pendingUpdates_.push_back(static_cast<uint32_t>(i));
    }
}

void SurfaceCache::captureCards(VkCommandBuffer cmd,
                                 VkBuffer lightBuffer,
                                 uint32_t lightCount,
                                 VkImageView shadowMap,
                                 const glm::mat4& lightViewProj) {
    // Capture pending cards
    // Implementation would dispatch card_capture.comp for each card
    pendingUpdates_.clear();
}

void SurfaceCache::updateRadiance(VkCommandBuffer cmd,
                                   VkImageView irradianceProbes,
                                   VkBuffer probeBuffer) {
    // Update card radiance from probes
    // Implementation would dispatch card_radiance.comp
}

SurfaceCache::Stats SurfaceCache::getStats() const {
    Stats stats;
    stats.totalCards = static_cast<uint32_t>(cards_.size());
    stats.validCards = stats.totalCards - static_cast<uint32_t>(pendingUpdates_.size());
    stats.pendingUpdates = static_cast<uint32_t>(pendingUpdates_.size());
    
    uint32_t usedPixels = 0;
    for (const auto& row : atlasRows_) {
        usedPixels += row.usedWidth * row.height;
    }
    stats.atlasUsedPixels = usedPixels;
    stats.atlasUtilization = static_cast<float>(usedPixels) / (config_.atlasWidth * config_.atlasHeight);
    
    return stats;
}
