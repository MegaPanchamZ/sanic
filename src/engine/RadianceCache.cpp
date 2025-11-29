/**
 * RadianceCache.cpp
 * 
 * Implementation of world-space radiance cache.
 */

#include "RadianceCache.h"
#include "VulkanContext.h"
#include <fstream>
#include <cmath>

RadianceCache::~RadianceCache() {
    cleanup();
}

bool RadianceCache::initialize(VulkanContext* context, const RadianceCacheConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    
    if (!createClipMaps()) { cleanup(); return false; }
    if (!createBuffers()) { cleanup(); return false; }
    if (!createPipelines()) { cleanup(); return false; }
    
    initialized_ = true;
    return true;
}

void RadianceCache::cleanup() {
    if (!context_) return;
    VkDevice device = context_->getDevice();
    
    // Pipelines
    if (scrollPipeline_) vkDestroyPipeline(device, scrollPipeline_, nullptr);
    if (scrollLayout_) vkDestroyPipelineLayout(device, scrollLayout_, nullptr);
    if (injectPipeline_) vkDestroyPipeline(device, injectPipeline_, nullptr);
    if (injectLayout_) vkDestroyPipelineLayout(device, injectLayout_, nullptr);
    if (samplePipeline_) vkDestroyPipeline(device, samplePipeline_, nullptr);
    if (sampleLayout_) vkDestroyPipelineLayout(device, sampleLayout_, nullptr);
    if (irradiancePipeline_) vkDestroyPipeline(device, irradiancePipeline_, nullptr);
    if (irradianceLayout_) vkDestroyPipelineLayout(device, irradianceLayout_, nullptr);
    
    // Descriptors
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (descLayout_) vkDestroyDescriptorSetLayout(device, descLayout_, nullptr);
    
    // Sampler
    if (volumeSampler_) vkDestroySampler(device, volumeSampler_, nullptr);
    
    // Clipmaps
    for (auto& clip : clipMaps_) {
        if (clip.radianceView) vkDestroyImageView(device, clip.radianceView, nullptr);
        if (clip.radianceVolume) vkDestroyImage(device, clip.radianceVolume, nullptr);
        if (clip.radianceMemory) vkFreeMemory(device, clip.radianceMemory, nullptr);
        
        if (clip.irradianceView) vkDestroyImageView(device, clip.irradianceView, nullptr);
        if (clip.irradianceVolume) vkDestroyImage(device, clip.irradianceVolume, nullptr);
        if (clip.irradianceMemory) vkFreeMemory(device, clip.irradianceMemory, nullptr);
    }
    clipMaps_.clear();
    
    // Buffers
    if (clipMapBuffer_) vkDestroyBuffer(device, clipMapBuffer_, nullptr);
    if (clipMapMemory_) vkFreeMemory(device, clipMapMemory_, nullptr);
    
    if (probeBuffer_) vkDestroyBuffer(device, probeBuffer_, nullptr);
    if (probeMemory_) vkFreeMemory(device, probeMemory_, nullptr);
    
    if (updateQueueBuffer_) vkDestroyBuffer(device, updateQueueBuffer_, nullptr);
    if (updateQueueMemory_) vkFreeMemory(device, updateQueueMemory_, nullptr);
    
    initialized_ = false;
}

bool RadianceCache::createClipMaps() {
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
    
    clipMaps_.resize(config_.clipMapLevels);
    
    float cellSize = config_.baseCellSize;
    for (uint32_t i = 0; i < config_.clipMapLevels; i++) {
        ClipMapLevel& clip = clipMaps_[i];
        clip.center = glm::vec3(0.0f);
        clip.voxelSize = cellSize;
        clip.resolution = glm::ivec3(config_.baseResolution);
        clip.offset = glm::ivec3(0);
        clip.needsUpdate = true;
        
        // Create radiance volume
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.format = config_.radianceFormat;
        imageInfo.extent = {config_.baseResolution, config_.baseResolution, config_.baseResolution};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        
        if (vkCreateImage(device, &imageInfo, nullptr, &clip.radianceVolume) != VK_SUCCESS) return false;
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, clip.radianceVolume, &memReqs);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &clip.radianceMemory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, clip.radianceVolume, clip.radianceMemory, 0);
        
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = clip.radianceVolume;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format = config_.radianceFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        if (vkCreateImageView(device, &viewInfo, nullptr, &clip.radianceView) != VK_SUCCESS) return false;
        
        // Create irradiance volume
        imageInfo.format = config_.irradianceFormat;
        if (vkCreateImage(device, &imageInfo, nullptr, &clip.irradianceVolume) != VK_SUCCESS) return false;
        
        vkGetImageMemoryRequirements(device, clip.irradianceVolume, &memReqs);
        allocInfo.allocationSize = memReqs.size;
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &clip.irradianceMemory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, clip.irradianceVolume, clip.irradianceMemory, 0);
        
        viewInfo.image = clip.irradianceVolume;
        viewInfo.format = config_.irradianceFormat;
        if (vkCreateImageView(device, &viewInfo, nullptr, &clip.irradianceView) != VK_SUCCESS) return false;
        
        cellSize *= config_.clipMapScale;
    }
    
    // Create volume sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &volumeSampler_) != VK_SUCCESS) return false;
    
    return true;
}

bool RadianceCache::createBuffers() {
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
    
    // Clipmap uniform buffer
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(GPUClipMapData) * config_.clipMapLevels;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &clipMapBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, clipMapBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &clipMapMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, clipMapBuffer_, clipMapMemory_, 0);
    
    return true;
}

bool RadianceCache::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Descriptor set layout
    VkDescriptorSetLayoutBinding bindings[8] = {};
    for (int i = 0; i < 4; i++) {
        bindings[i] = {(uint32_t)i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    for (int i = 4; i < 8; i++) {
        bindings[i] = {(uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 8;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descLayout_) != VK_SUCCESS) return false;
    
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = 128;
    
    VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &descLayout_;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pushRange;
    
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &injectLayout_) != VK_SUCCESS) return false;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &sampleLayout_) != VK_SUCCESS) return false;
    
    return true;
}

bool RadianceCache::loadShader(const std::string& path, VkShaderModule& outModule) {
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

void RadianceCache::update(VkCommandBuffer cmd,
                            const glm::vec3& cameraPos,
                            VkImageView gbufferDepth,
                            VkImageView gbufferNormal,
                            VkImageView gbufferAlbedo,
                            VkBuffer lightBuffer,
                            uint32_t lightCount) {
    scrollClipMaps(cmd, cameraPos);
    lastCameraPos_ = cameraPos;
}

void RadianceCache::scrollClipMaps(VkCommandBuffer cmd, const glm::vec3& cameraPos) {
    glm::vec3 delta = cameraPos - lastCameraPos_;
    
    for (uint32_t i = 0; i < clipMaps_.size(); i++) {
        ClipMapLevel& clip = clipMaps_[i];
        
        // Check if we need to scroll
        glm::vec3 scrollThreshold = glm::vec3(clip.voxelSize * 4.0f);
        glm::vec3 centerDelta = cameraPos - clip.center;
        
        if (glm::any(glm::greaterThan(glm::abs(centerDelta), scrollThreshold))) {
            // Scroll the clipmap
            glm::ivec3 scrollAmount = glm::ivec3(centerDelta / clip.voxelSize);
            clip.offset = (clip.offset + scrollAmount) % clip.resolution;
            clip.center = cameraPos;
            clip.needsUpdate = true;
        }
    }
}

void RadianceCache::injectProbes(VkCommandBuffer cmd,
                                  VkBuffer probeBufferInput,
                                  uint32_t probeCount) {
    // Would dispatch radiance_inject.comp
}

void RadianceCache::sampleRadiance(VkCommandBuffer cmd,
                                    VkImageView outputRadiance,
                                    VkImageView depthView,
                                    VkImageView normalView,
                                    const glm::mat4& invViewProj) {
    // Would dispatch radiance_sample.comp
}

void RadianceCache::computeIrradiance(VkCommandBuffer cmd) {
    // Compute SH irradiance from radiance
}

glm::ivec3 RadianceCache::worldToClipCoord(const glm::vec3& worldPos, uint32_t level) const {
    if (level >= clipMaps_.size()) return glm::ivec3(0);
    
    const ClipMapLevel& clip = clipMaps_[level];
    glm::vec3 localPos = (worldPos - clip.center) / clip.voxelSize;
    glm::ivec3 coord = glm::ivec3(glm::floor(localPos)) + clip.resolution / 2;
    
    return coord;
}

glm::ivec3 RadianceCache::getToroidalOffset(uint32_t level) const {
    if (level >= clipMaps_.size()) return glm::ivec3(0);
    return clipMaps_[level].offset;
}

VkImageView RadianceCache::getRadianceView(uint32_t level) const {
    if (level >= clipMaps_.size()) return VK_NULL_HANDLE;
    return clipMaps_[level].radianceView;
}

VkImageView RadianceCache::getIrradianceView(uint32_t level) const {
    if (level >= clipMaps_.size()) return VK_NULL_HANDLE;
    return clipMaps_[level].irradianceView;
}
