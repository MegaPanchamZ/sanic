/**
 * LandscapeSystem.cpp
 * 
 * Implementation of Unreal-style landscape/terrain system.
 */

#include "LandscapeSystem.h"
#include "VulkanContext.h"
#include "AsyncPhysics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>

namespace Sanic {

// LOD distance multipliers (geometric progression)
static constexpr float kLODDistances[] = {
    50.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f
};

LandscapeSystem::LandscapeSystem() = default;

LandscapeSystem::~LandscapeSystem() {
    shutdown();
}

bool LandscapeSystem::initialize(VulkanContext* context, AsyncPhysics* physics) {
    if (initialized_) return true;
    
    context_ = context;
    physics_ = physics;
    
    VkDevice device = context_->getDevice();
    
    // Create heightmap sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &heightmapSampler_) != VK_SUCCESS) {
        return false;
    }
    
    // Create weightmap sampler with anisotropy
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 8.0f;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &weightmapSampler_) != VK_SUCCESS) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void LandscapeSystem::shutdown() {
    if (!initialized_) return;
    
    VkDevice device = context_->getDevice();
    
    // Destroy all landscapes
    for (auto& [id, landscape] : landscapes_) {
        for (auto& component : landscape.components) {
            destroyComponent(component);
        }
        
        if (landscape.indirectBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, landscape.indirectBuffer, nullptr);
        }
        if (landscape.drawDataBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, landscape.drawDataBuffer, nullptr);
        }
        if (landscape.indirectMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, landscape.indirectMemory, nullptr);
        }
        if (landscape.drawDataMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, landscape.drawDataMemory, nullptr);
        }
        
        for (auto buffer : landscape.lodIndexBuffers) {
            if (buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, buffer, nullptr);
            }
        }
        if (landscape.lodIndexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, landscape.lodIndexMemory, nullptr);
        }
        
        if (landscape.lodComputePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, landscape.lodComputePipeline, nullptr);
        }
        if (landscape.lodPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, landscape.lodPipelineLayout, nullptr);
        }
        if (landscape.lodDescSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, landscape.lodDescSetLayout, nullptr);
        }
    }
    landscapes_.clear();
    
    if (heightmapSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, heightmapSampler_, nullptr);
        heightmapSampler_ = VK_NULL_HANDLE;
    }
    if (weightmapSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, weightmapSampler_, nullptr);
        weightmapSampler_ = VK_NULL_HANDLE;
    }
    
    initialized_ = false;
}

uint32_t LandscapeSystem::createLandscape(const LandscapeConfig& config) {
    uint32_t id = nextLandscapeId_++;
    Landscape& landscape = landscapes_[id];
    
    landscape.id = id;
    landscape.config = config;
    landscape.transform = glm::mat4(1.0f);
    landscape.invTransform = glm::mat4(1.0f);
    
    // Setup LOD levels
    landscape.lodLevels.resize(config.lodLevels);
    uint32_t resolution = config.heightmapResolution;
    
    for (uint32_t i = 0; i < config.lodLevels; ++i) {
        LandscapeLODLevel& level = landscape.lodLevels[i];
        level.resolution = resolution;
        level.lodDistance = kLODDistances[i] * config.lodBias;
        level.morphRange = level.lodDistance * config.lodMorphRange;
        
        // Halve resolution each LOD
        resolution = std::max(2u, resolution / 2);
    }
    
    // Generate shared LOD index buffers
    generateLODIndices(landscape);
    
    // Create components
    landscape.components.reserve(config.componentsX * config.componentsY);
    
    for (uint32_t y = 0; y < config.componentsY; ++y) {
        for (uint32_t x = 0; x < config.componentsX; ++x) {
            createComponent(landscape, x, y);
        }
    }
    
    // Create GPU buffers for indirect drawing
    VkDevice device = context_->getDevice();
    uint32_t maxComponents = config.componentsX * config.componentsY;
    
    // Indirect draw buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = maxComponents * sizeof(VkDrawIndexedIndirectCommand);
    bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | 
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateBuffer(device, &bufferInfo, nullptr, &landscape.indirectBuffer);
    
    // Draw data buffer
    bufferInfo.size = maxComponents * sizeof(LandscapeDrawData);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    vkCreateBuffer(device, &bufferInfo, nullptr, &landscape.drawDataBuffer);
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, landscape.indirectBuffer, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    vkAllocateMemory(device, &allocInfo, nullptr, &landscape.indirectMemory);
    vkBindBufferMemory(device, landscape.indirectBuffer, landscape.indirectMemory, 0);
    
    vkGetBufferMemoryRequirements(device, landscape.drawDataBuffer, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    vkAllocateMemory(device, &allocInfo, nullptr, &landscape.drawDataMemory);
    vkBindBufferMemory(device, landscape.drawDataBuffer, landscape.drawDataMemory, 0);
    
    // Create LOD compute pipeline
    createLODPipeline(landscape);
    
    return id;
}

void LandscapeSystem::destroyLandscape(uint32_t landscapeId) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return;
    
    Landscape& landscape = it->second;
    VkDevice device = context_->getDevice();
    
    for (auto& component : landscape.components) {
        destroyComponent(component);
    }
    
    // Cleanup GPU resources
    if (landscape.indirectBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, landscape.indirectBuffer, nullptr);
    }
    if (landscape.drawDataBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, landscape.drawDataBuffer, nullptr);
    }
    if (landscape.indirectMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, landscape.indirectMemory, nullptr);
    }
    if (landscape.drawDataMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, landscape.drawDataMemory, nullptr);
    }
    
    landscapes_.erase(it);
}

bool LandscapeSystem::createComponent(Landscape& landscape, uint32_t x, uint32_t y) {
    LandscapeComponent component;
    component.id = static_cast<uint32_t>(landscape.components.size());
    component.sectionCoord = glm::ivec2(x, y);
    component.heightmapResolution = landscape.config.heightmapResolution;
    
    // Initialize flat heightmap
    uint32_t numHeights = component.heightmapResolution * component.heightmapResolution;
    component.heightmap.resize(numHeights, 32768);  // 0.5 height
    component.minHeight = 0.5f * landscape.config.heightScale;
    component.maxHeight = 0.5f * landscape.config.heightScale;
    
    // Initialize weightmaps
    uint32_t weightmapsNeeded = (landscape.config.maxLayersPerComponent + 3) / 4;
    component.weightmaps.resize(weightmapsNeeded);
    
    for (auto& weightmap : component.weightmaps) {
        weightmap.width = landscape.config.weightmapResolution;
        weightmap.height = landscape.config.weightmapResolution;
        weightmap.channelCount = 4;
        weightmap.data.resize(weightmap.width * weightmap.height * 4, 0);
        
        // First layer fully painted by default
        if (&weightmap == &component.weightmaps[0]) {
            for (size_t i = 0; i < weightmap.data.size(); i += 4) {
                weightmap.data[i] = 255;  // First layer at full weight
            }
        }
    }
    
    // Compute bounds
    updateComponentBounds(component, landscape.config);
    
    // Create heightmap texture
    createHeightmapTexture(component);
    
    // Create weightmap textures
    for (auto& weightmap : component.weightmaps) {
        createWeightmapTexture(weightmap);
    }
    
    component.isLoaded = true;
    landscape.components.push_back(std::move(component));
    
    return true;
}

void LandscapeSystem::destroyComponent(LandscapeComponent& component) {
    VkDevice device = context_->getDevice();
    
    if (component.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, component.vertexBuffer, nullptr);
    }
    if (component.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, component.indexBuffer, nullptr);
    }
    if (component.vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, component.vertexMemory, nullptr);
    }
    if (component.indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, component.indexMemory, nullptr);
    }
    
    if (component.heightmapImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, component.heightmapImage, nullptr);
    }
    if (component.heightmapView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, component.heightmapView, nullptr);
    }
    if (component.heightmapMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, component.heightmapMemory, nullptr);
    }
    
    for (auto& weightmap : component.weightmaps) {
        if (weightmap.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, weightmap.image, nullptr);
        }
        if (weightmap.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, weightmap.view, nullptr);
        }
        if (weightmap.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, weightmap.memory, nullptr);
        }
    }
}

void LandscapeSystem::updateComponentBounds(LandscapeComponent& component, 
                                             const LandscapeConfig& config) {
    float offsetX = component.sectionCoord.x * config.componentSize;
    float offsetZ = component.sectionCoord.y * config.componentSize;
    
    component.boundsMin = glm::vec3(offsetX, component.minHeight, offsetZ);
    component.boundsMax = glm::vec3(
        offsetX + config.componentSize,
        component.maxHeight,
        offsetZ + config.componentSize
    );
    component.center = (component.boundsMin + component.boundsMax) * 0.5f;
}

bool LandscapeSystem::createHeightmapTexture(LandscapeComponent& component) {
    VkDevice device = context_->getDevice();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16_UNORM;
    imageInfo.extent = {component.heightmapResolution, component.heightmapResolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &component.heightmapImage) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, component.heightmapImage, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &component.heightmapMemory) != VK_SUCCESS) {
        return false;
    }
    
    vkBindImageMemory(device, component.heightmapImage, component.heightmapMemory, 0);
    
    // Create view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = component.heightmapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &component.heightmapView) != VK_SUCCESS) {
        return false;
    }
    
    // Upload initial data
    updateHeightmapTexture(component);
    
    return true;
}

bool LandscapeSystem::updateHeightmapTexture(LandscapeComponent& component) {
    // Upload heightmap data via staging buffer
    // (Simplified - use proper staging in production)
    
    VkDevice device = context_->getDevice();
    size_t dataSize = component.heightmap.size() * sizeof(uint16_t);
    
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
    
    // Copy data
    void* mapped;
    vkMapMemory(device, stagingMemory, 0, dataSize, 0, &mapped);
    memcpy(mapped, component.heightmap.data(), dataSize);
    vkUnmapMemory(device, stagingMemory);
    
    // Submit copy command
    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    
    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = component.heightmapImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
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
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {component.heightmapResolution, component.heightmapResolution, 1};
    
    vkCmdCopyBufferToImage(cmd, stagingBuffer, component.heightmapImage,
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
    
    // Cleanup staging
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    
    return true;
}

bool LandscapeSystem::createWeightmapTexture(LandscapeWeightmap& weightmap) {
    VkDevice device = context_->getDevice();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {weightmap.width, weightmap.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &weightmap.image) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, weightmap.image, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    vkAllocateMemory(device, &allocInfo, nullptr, &weightmap.memory);
    vkBindImageMemory(device, weightmap.image, weightmap.memory, 0);
    
    // Create view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = weightmap.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    vkCreateImageView(device, &viewInfo, nullptr, &weightmap.view);
    
    return true;
}

bool LandscapeSystem::updateWeightmapTexture(LandscapeWeightmap& weightmap) {
    // Similar to heightmap upload
    // (Implementation follows same pattern as updateHeightmapTexture)
    return true;
}

void LandscapeSystem::generateLODIndices(Landscape& landscape) {
    // Generate index buffers for each LOD level
    // Each LOD has different vertex density but same topology
    
    for (uint32_t lod = 0; lod < landscape.config.lodLevels; ++lod) {
        uint32_t resolution = landscape.lodLevels[lod].resolution;
        uint32_t quadsPerSide = resolution - 1;
        uint32_t numIndices = quadsPerSide * quadsPerSide * 6;
        
        std::vector<uint32_t> indices;
        indices.reserve(numIndices);
        
        for (uint32_t y = 0; y < quadsPerSide; ++y) {
            for (uint32_t x = 0; x < quadsPerSide; ++x) {
                uint32_t topLeft = y * resolution + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (y + 1) * resolution + x;
                uint32_t bottomRight = bottomLeft + 1;
                
                // Two triangles per quad
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);
                
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }
        
        landscape.lodIndexCounts[lod] = static_cast<uint32_t>(indices.size());
        
        // Create index buffer
        VkDevice device = context_->getDevice();
        size_t bufferSize = indices.size() * sizeof(uint32_t);
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        vkCreateBuffer(device, &bufferInfo, nullptr, &landscape.lodIndexBuffers[lod]);
        
        // Allocate and upload
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, landscape.lodIndexBuffers[lod], &memReqs);
        
        // Note: In production, allocate all LOD buffers from one memory block
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = context_->findMemoryType(
            memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        
        VkDeviceMemory memory;
        vkAllocateMemory(device, &allocInfo, nullptr, &memory);
        vkBindBufferMemory(device, landscape.lodIndexBuffers[lod], memory, 0);
        
        // Upload via staging (simplified)
        // In production, batch these uploads
    }
}

void LandscapeSystem::createLODPipeline(Landscape& landscape) {
    // Create compute pipeline for GPU LOD selection
    // This runs per-component and outputs draw commands + LOD data
    
    // (Pipeline creation code - similar to other compute pipelines in engine)
}

void LandscapeSystem::setTransform(uint32_t landscapeId, const glm::vec3& position,
                                    const glm::quat& rotation) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return;
    
    Landscape& landscape = it->second;
    landscape.transform = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
    landscape.invTransform = glm::inverse(landscape.transform);
}

bool LandscapeSystem::importHeightmap(uint32_t landscapeId, const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    
    // Determine dimensions (assume square for raw heightmaps)
    uint32_t width = static_cast<uint32_t>(std::sqrt(fileSize / 2));  // 16-bit
    
    return importHeightmap(landscapeId, reinterpret_cast<const uint16_t*>(data.data()), width, width);
}

bool LandscapeSystem::importHeightmap(uint32_t landscapeId, const uint16_t* data,
                                       uint32_t width, uint32_t height) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return false;
    
    Landscape& landscape = it->second;
    const LandscapeConfig& config = landscape.config;
    
    // Distribute heightmap data across components
    uint32_t pixelsPerComponentX = width / config.componentsX;
    uint32_t pixelsPerComponentY = height / config.componentsY;
    
    for (auto& component : landscape.components) {
        uint32_t startX = component.sectionCoord.x * pixelsPerComponentX;
        uint32_t startY = component.sectionCoord.y * pixelsPerComponentY;
        
        // Resample to component resolution
        for (uint32_t y = 0; y < component.heightmapResolution; ++y) {
            for (uint32_t x = 0; x < component.heightmapResolution; ++x) {
                float u = float(x) / (component.heightmapResolution - 1);
                float v = float(y) / (component.heightmapResolution - 1);
                
                uint32_t srcX = startX + static_cast<uint32_t>(u * (pixelsPerComponentX - 1));
                uint32_t srcY = startY + static_cast<uint32_t>(v * (pixelsPerComponentY - 1));
                
                srcX = std::min(srcX, width - 1);
                srcY = std::min(srcY, height - 1);
                
                component.heightmap[y * component.heightmapResolution + x] = 
                    data[srcY * width + srcX];
            }
        }
        
        // Update min/max height
        component.minHeight = std::numeric_limits<float>::max();
        component.maxHeight = std::numeric_limits<float>::lowest();
        
        for (uint16_t h : component.heightmap) {
            float height = (float(h) / 65535.0f) * config.heightScale;
            component.minHeight = std::min(component.minHeight, height);
            component.maxHeight = std::max(component.maxHeight, height);
        }
        
        updateComponentBounds(component, config);
        updateHeightmapTexture(component);
    }
    
    return true;
}

uint32_t LandscapeSystem::addLayer(uint32_t landscapeId, const LandscapeLayer& layer) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return 0;
    
    Landscape& landscape = it->second;
    LandscapeLayer newLayer = layer;
    newLayer.id = static_cast<uint32_t>(landscape.layers.size()) + 1;
    
    landscape.layers.push_back(newLayer);
    return newLayer.id;
}

void LandscapeSystem::removeLayer(uint32_t landscapeId, uint32_t layerId) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return;
    
    Landscape& landscape = it->second;
    landscape.layers.erase(
        std::remove_if(landscape.layers.begin(), landscape.layers.end(),
                       [layerId](const LandscapeLayer& l) { return l.id == layerId; }),
        landscape.layers.end()
    );
}

void LandscapeSystem::applyBrush(uint32_t landscapeId, const glm::vec3& worldPos,
                                  const LandscapeBrush& brush) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return;
    
    Landscape& landscape = it->second;
    const LandscapeConfig& config = landscape.config;
    
    // Transform to local space
    glm::vec4 localPos4 = landscape.invTransform * glm::vec4(worldPos, 1.0f);
    glm::vec2 localPos(localPos4.x, localPos4.z);
    
    // Find affected components
    float brushRadiusWorld = brush.radius;
    
    for (auto& component : landscape.components) {
        // Check if brush overlaps component
        glm::vec2 compMin(component.boundsMin.x, component.boundsMin.z);
        glm::vec2 compMax(component.boundsMax.x, component.boundsMax.z);
        
        if (localPos.x + brushRadiusWorld < compMin.x ||
            localPos.x - brushRadiusWorld > compMax.x ||
            localPos.y + brushRadiusWorld < compMin.y ||
            localPos.y - brushRadiusWorld > compMax.y) {
            continue;
        }
        
        // Local position relative to component
        glm::vec2 compLocalPos = localPos - compMin;
        
        switch (brush.mode) {
            case LandscapeBrush::Mode::Raise:
            case LandscapeBrush::Mode::Lower:
                brushRaise(component, compLocalPos, brush, 
                          brush.mode == LandscapeBrush::Mode::Lower ? -1.0f : 1.0f);
                break;
            case LandscapeBrush::Mode::Smooth:
                brushSmooth(component, compLocalPos, brush);
                break;
            case LandscapeBrush::Mode::Flatten:
                brushFlatten(component, compLocalPos, brush, worldPos.y);
                break;
            case LandscapeBrush::Mode::Layer:
                brushPaintLayer(component, compLocalPos, brush, brush.targetLayerId);
                break;
            default:
                break;
        }
        
        // Update bounds and GPU data
        updateComponentBounds(component, config);
        updateHeightmapTexture(component);
    }
}

void LandscapeSystem::brushRaise(LandscapeComponent& component, const glm::vec2& localPos,
                                  const LandscapeBrush& brush, float direction) {
    float componentSize = component.boundsMax.x - component.boundsMin.x;
    float texelSize = componentSize / (component.heightmapResolution - 1);
    
    int brushRadiusTexels = static_cast<int>(brush.radius / texelSize);
    int centerX = static_cast<int>(localPos.x / texelSize);
    int centerY = static_cast<int>(localPos.y / texelSize);
    
    for (int y = -brushRadiusTexels; y <= brushRadiusTexels; ++y) {
        for (int x = -brushRadiusTexels; x <= brushRadiusTexels; ++x) {
            int texelX = centerX + x;
            int texelY = centerY + y;
            
            if (texelX < 0 || texelX >= static_cast<int>(component.heightmapResolution) ||
                texelY < 0 || texelY >= static_cast<int>(component.heightmapResolution)) {
                continue;
            }
            
            float dist = std::sqrt(float(x * x + y * y)) * texelSize;
            if (dist > brush.radius) continue;
            
            // Falloff
            float t = dist / brush.radius;
            float falloff = 1.0f - std::pow(t, 1.0f / (1.0f - brush.falloff + 0.001f));
            
            // Apply
            uint32_t idx = texelY * component.heightmapResolution + texelX;
            int32_t newHeight = component.heightmap[idx] + 
                static_cast<int32_t>(direction * brush.strength * falloff * 1000.0f);
            
            component.heightmap[idx] = static_cast<uint16_t>(std::clamp(newHeight, 0, 65535));
        }
    }
}

void LandscapeSystem::brushSmooth(LandscapeComponent& component, const glm::vec2& localPos,
                                   const LandscapeBrush& brush) {
    float componentSize = component.boundsMax.x - component.boundsMin.x;
    float texelSize = componentSize / (component.heightmapResolution - 1);
    
    int brushRadiusTexels = static_cast<int>(brush.radius / texelSize);
    int centerX = static_cast<int>(localPos.x / texelSize);
    int centerY = static_cast<int>(localPos.y / texelSize);
    
    // Two-pass: first compute averages, then apply
    std::vector<float> newHeights(component.heightmap.size());
    
    for (int y = -brushRadiusTexels; y <= brushRadiusTexels; ++y) {
        for (int x = -brushRadiusTexels; x <= brushRadiusTexels; ++x) {
            int texelX = centerX + x;
            int texelY = centerY + y;
            
            if (texelX < 0 || texelX >= static_cast<int>(component.heightmapResolution) ||
                texelY < 0 || texelY >= static_cast<int>(component.heightmapResolution)) {
                continue;
            }
            
            float dist = std::sqrt(float(x * x + y * y)) * texelSize;
            if (dist > brush.radius) continue;
            
            // Average neighborhood
            float sum = 0.0f;
            int count = 0;
            
            for (int ny = -1; ny <= 1; ++ny) {
                for (int nx = -1; nx <= 1; ++nx) {
                    int sx = texelX + nx;
                    int sy = texelY + ny;
                    
                    if (sx >= 0 && sx < static_cast<int>(component.heightmapResolution) &&
                        sy >= 0 && sy < static_cast<int>(component.heightmapResolution)) {
                        sum += component.heightmap[sy * component.heightmapResolution + sx];
                        count++;
                    }
                }
            }
            
            uint32_t idx = texelY * component.heightmapResolution + texelX;
            float avg = sum / count;
            float t = dist / brush.radius;
            float falloff = 1.0f - t;
            
            newHeights[idx] = glm::mix(float(component.heightmap[idx]), avg, falloff * brush.strength);
        }
    }
    
    // Apply
    for (size_t i = 0; i < component.heightmap.size(); ++i) {
        if (newHeights[i] > 0) {
            component.heightmap[i] = static_cast<uint16_t>(std::clamp(newHeights[i], 0.0f, 65535.0f));
        }
    }
}

void LandscapeSystem::brushFlatten(LandscapeComponent& component, const glm::vec2& localPos,
                                    const LandscapeBrush& brush, float targetHeight) {
    // Similar to raise but targets a specific height
    // (Implementation follows same pattern)
}

void LandscapeSystem::brushPaintLayer(LandscapeComponent& component, const glm::vec2& localPos,
                                       const LandscapeBrush& brush, uint32_t layerId) {
    if (layerId == 0 || component.weightmaps.empty()) return;
    
    uint32_t weightmapIdx = (layerId - 1) / 4;
    uint32_t channelIdx = (layerId - 1) % 4;
    
    if (weightmapIdx >= component.weightmaps.size()) return;
    
    LandscapeWeightmap& weightmap = component.weightmaps[weightmapIdx];
    float componentSize = component.boundsMax.x - component.boundsMin.x;
    float texelSize = componentSize / weightmap.width;
    
    int brushRadiusTexels = static_cast<int>(brush.radius / texelSize);
    int centerX = static_cast<int>(localPos.x / texelSize);
    int centerY = static_cast<int>(localPos.y / texelSize);
    
    for (int y = -brushRadiusTexels; y <= brushRadiusTexels; ++y) {
        for (int x = -brushRadiusTexels; x <= brushRadiusTexels; ++x) {
            int texelX = centerX + x;
            int texelY = centerY + y;
            
            if (texelX < 0 || texelX >= static_cast<int>(weightmap.width) ||
                texelY < 0 || texelY >= static_cast<int>(weightmap.height)) {
                continue;
            }
            
            float dist = std::sqrt(float(x * x + y * y)) * texelSize;
            if (dist > brush.radius) continue;
            
            float t = dist / brush.radius;
            float falloff = 1.0f - std::pow(t, 1.0f / (1.0f - brush.falloff + 0.001f));
            
            uint32_t idx = (texelY * weightmap.width + texelX) * 4;
            
            // Increase target channel, decrease others proportionally
            float currentWeight = weightmap.data[idx + channelIdx] / 255.0f;
            float newWeight = std::min(1.0f, currentWeight + brush.strength * falloff);
            
            // Normalize other channels
            float otherTotal = 0.0f;
            for (uint32_t c = 0; c < 4; ++c) {
                if (c != channelIdx) {
                    otherTotal += weightmap.data[idx + c] / 255.0f;
                }
            }
            
            if (otherTotal > 0.0f && newWeight < 1.0f) {
                float scale = (1.0f - newWeight) / otherTotal;
                for (uint32_t c = 0; c < 4; ++c) {
                    if (c != channelIdx) {
                        weightmap.data[idx + c] = static_cast<uint8_t>(
                            (weightmap.data[idx + c] / 255.0f) * scale * 255.0f
                        );
                    }
                }
            } else if (newWeight >= 1.0f) {
                for (uint32_t c = 0; c < 4; ++c) {
                    if (c != channelIdx) {
                        weightmap.data[idx + c] = 0;
                    }
                }
            }
            
            weightmap.data[idx + channelIdx] = static_cast<uint8_t>(newWeight * 255.0f);
        }
    }
    
    weightmap.isDirty = true;
}

void LandscapeSystem::updateLOD(uint32_t landscapeId, const glm::vec3& cameraPos,
                                 const glm::mat4& viewProj) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return;
    
    Landscape& landscape = it->second;
    
    for (auto& component : landscape.components) {
        // Transform camera to local space
        glm::vec4 localCamera = landscape.invTransform * glm::vec4(cameraPos, 1.0f);
        float dist = glm::distance(glm::vec3(localCamera), component.center);
        
        // Find appropriate LOD
        uint32_t lod = 0;
        float morphFactor = 0.0f;
        
        for (uint32_t i = 0; i < landscape.config.lodLevels - 1; ++i) {
            if (dist < landscape.lodLevels[i].lodDistance) {
                lod = i;
                
                // Compute morph factor for smooth transitions
                float prevDist = (i > 0) ? landscape.lodLevels[i - 1].lodDistance : 0.0f;
                float nextDist = landscape.lodLevels[i].lodDistance;
                float morphStart = nextDist - landscape.lodLevels[i].morphRange;
                
                if (dist > morphStart) {
                    morphFactor = (dist - morphStart) / landscape.lodLevels[i].morphRange;
                }
                break;
            }
            lod = i + 1;
        }
        
        component.currentLOD = lod;
        component.morphFactor = morphFactor;
    }
    
    // Update neighbor LODs for seam stitching
    for (auto& component : landscape.components) {
        glm::ivec2 coords = component.sectionCoord;
        
        auto getComponentLOD = [&](int x, int y) -> uint32_t {
            if (x < 0 || x >= static_cast<int>(landscape.config.componentsX) ||
                y < 0 || y >= static_cast<int>(landscape.config.componentsY)) {
                return component.currentLOD;
            }
            return landscape.components[y * landscape.config.componentsX + x].currentLOD;
        };
        
        component.neighborLODs[0] = getComponentLOD(coords.x, coords.y - 1);  // North
        component.neighborLODs[1] = getComponentLOD(coords.x + 1, coords.y);  // East
        component.neighborLODs[2] = getComponentLOD(coords.x, coords.y + 1);  // South
        component.neighborLODs[3] = getComponentLOD(coords.x - 1, coords.y);  // West
    }
}

void LandscapeSystem::cullAndPrepare(uint32_t landscapeId, const glm::mat4& viewProj,
                                      VkCommandBuffer cmd) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return;
    
    Landscape& landscape = it->second;
    landscape.visibleCount = 0;
    
    // Extract frustum planes
    glm::mat4 mvp = viewProj * landscape.transform;
    
    // Frustum culling (simplified - full implementation would extract 6 planes)
    for (auto& component : landscape.components) {
        // Transform bounds to clip space and check visibility
        glm::vec4 center = mvp * glm::vec4(component.center, 1.0f);
        
        // Simple sphere test
        float radius = glm::length(component.boundsMax - component.boundsMin) * 0.5f;
        
        if (std::abs(center.x) <= center.w + radius &&
            std::abs(center.y) <= center.w + radius &&
            center.z >= -radius && center.z <= center.w + radius) {
            component.isVisible = true;
            landscape.visibleCount++;
        } else {
            component.isVisible = false;
        }
    }
    
    // Generate draw commands (in production, do this on GPU)
}

float LandscapeSystem::getHeightAt(uint32_t landscapeId, float worldX, float worldZ) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return 0.0f;
    
    Landscape& landscape = it->second;
    
    // Transform to local space
    glm::vec4 localPos = landscape.invTransform * glm::vec4(worldX, 0, worldZ, 1);
    
    // Find component
    int compX = static_cast<int>(localPos.x / landscape.config.componentSize);
    int compY = static_cast<int>(localPos.z / landscape.config.componentSize);
    
    if (compX < 0 || compX >= static_cast<int>(landscape.config.componentsX) ||
        compY < 0 || compY >= static_cast<int>(landscape.config.componentsY)) {
        return 0.0f;
    }
    
    const LandscapeComponent& component = 
        landscape.components[compY * landscape.config.componentsX + compX];
    
    // Local position within component
    float localX = localPos.x - compX * landscape.config.componentSize;
    float localZ = localPos.z - compY * landscape.config.componentSize;
    
    return sampleHeightmap(component, localX, localZ);
}

float LandscapeSystem::sampleHeightmap(const LandscapeComponent& component, 
                                        float localX, float localZ) {
    float componentSize = component.boundsMax.x - component.boundsMin.x;
    float u = localX / componentSize;
    float v = localZ / componentSize;
    
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    
    float fx = u * (component.heightmapResolution - 1);
    float fy = v * (component.heightmapResolution - 1);
    
    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, static_cast<int>(component.heightmapResolution) - 1);
    int y1 = std::min(y0 + 1, static_cast<int>(component.heightmapResolution) - 1);
    
    float tx = fx - x0;
    float ty = fy - y0;
    
    float h00 = component.heightmap[y0 * component.heightmapResolution + x0] / 65535.0f;
    float h10 = component.heightmap[y0 * component.heightmapResolution + x1] / 65535.0f;
    float h01 = component.heightmap[y1 * component.heightmapResolution + x0] / 65535.0f;
    float h11 = component.heightmap[y1 * component.heightmapResolution + x1] / 65535.0f;
    
    // Bilinear interpolation
    float h0 = glm::mix(h00, h10, tx);
    float h1 = glm::mix(h01, h11, tx);
    
    return glm::mix(h0, h1, ty) * (component.maxHeight - component.minHeight) + component.minHeight;
}

glm::vec3 LandscapeSystem::getNormalAt(uint32_t landscapeId, float worldX, float worldZ) {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return glm::vec3(0, 1, 0);
    
    // Use central differences
    float delta = 1.0f;
    float hL = getHeightAt(landscapeId, worldX - delta, worldZ);
    float hR = getHeightAt(landscapeId, worldX + delta, worldZ);
    float hD = getHeightAt(landscapeId, worldX, worldZ - delta);
    float hU = getHeightAt(landscapeId, worldX, worldZ + delta);
    
    glm::vec3 normal(hL - hR, 2.0f * delta, hD - hU);
    return glm::normalize(normal);
}

bool LandscapeSystem::raycast(uint32_t landscapeId, const glm::vec3& origin,
                               const glm::vec3& direction, float maxDistance,
                               glm::vec3& outHitPoint, glm::vec3& outNormal) {
    // Ray marching against heightmap
    float step = 1.0f;
    float t = 0.0f;
    
    while (t < maxDistance) {
        glm::vec3 pos = origin + direction * t;
        float terrainHeight = getHeightAt(landscapeId, pos.x, pos.z);
        
        if (pos.y < terrainHeight) {
            // Binary search for exact intersection
            float tMin = t - step;
            float tMax = t;
            
            for (int i = 0; i < 8; ++i) {
                float tMid = (tMin + tMax) * 0.5f;
                glm::vec3 midPos = origin + direction * tMid;
                float midHeight = getHeightAt(landscapeId, midPos.x, midPos.z);
                
                if (midPos.y < midHeight) {
                    tMax = tMid;
                } else {
                    tMin = tMid;
                }
            }
            
            outHitPoint = origin + direction * tMax;
            outNormal = getNormalAt(landscapeId, outHitPoint.x, outHitPoint.z);
            return true;
        }
        
        t += step;
    }
    
    return false;
}

uint32_t LandscapeSystem::getDrawCount(uint32_t landscapeId) const {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return 0;
    return it->second.visibleCount;
}

VkBuffer LandscapeSystem::getDrawBuffer(uint32_t landscapeId) const {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return VK_NULL_HANDLE;
    return it->second.indirectBuffer;
}

VkBuffer LandscapeSystem::getDrawDataBuffer(uint32_t landscapeId) const {
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return VK_NULL_HANDLE;
    return it->second.drawDataBuffer;
}

LandscapeSystem::Statistics LandscapeSystem::getStatistics(uint32_t landscapeId) const {
    Statistics stats = {};
    
    auto it = landscapes_.find(landscapeId);
    if (it == landscapes_.end()) return stats;
    
    const Landscape& landscape = it->second;
    stats.totalComponents = static_cast<uint32_t>(landscape.components.size());
    stats.visibleComponents = landscape.visibleCount;
    
    for (const auto& component : landscape.components) {
        if (component.isVisible) {
            stats.lodDistributions[component.currentLOD]++;
            stats.trianglesRendered += landscape.lodIndexCounts[component.currentLOD] / 3;
        }
    }
    
    return stats;
}

} // namespace Sanic

