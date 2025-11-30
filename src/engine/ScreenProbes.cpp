/**
 * ScreenProbes.cpp
 * 
 * Implementation of screen-space probe system.
 */

#include "ScreenProbes.h"
#include "ShaderManager.h"
#include "VulkanContext.h"
#include <fstream>
#include <algorithm>

ScreenProbes::~ScreenProbes() {
    cleanup();
}

bool ScreenProbes::initialize(VulkanContext* context,
                               uint32_t screenWidth,
                               uint32_t screenHeight,
                               const ScreenProbeConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    
    tileCountX_ = (screenWidth + config_.tileSize - 1) / config_.tileSize;
    tileCountY_ = (screenHeight + config_.tileSize - 1) / config_.tileSize;
    probeCount_ = tileCountX_ * tileCountY_ * config_.maxProbesPerTile;
    
    if (!createProbeAtlas()) { cleanup(); return false; }
    if (!createProbeBuffers()) { cleanup(); return false; }
    if (!createPipelines()) { cleanup(); return false; }
    
    initialized_ = true;
    return true;
}

void ScreenProbes::cleanup() {
    if (!context_) return;
    VkDevice device = context_->getDevice();
    
    // Pipelines
    if (probePlacePipeline_) vkDestroyPipeline(device, probePlacePipeline_, nullptr);
    if (probePlaceLayout_) vkDestroyPipelineLayout(device, probePlaceLayout_, nullptr);
    if (probeTracePipeline_) vkDestroyPipeline(device, probeTracePipeline_, nullptr);
    if (probeTraceLayout_) vkDestroyPipelineLayout(device, probeTraceLayout_, nullptr);
    if (probeFilterPipeline_) vkDestroyPipeline(device, probeFilterPipeline_, nullptr);
    if (probeFilterLayout_) vkDestroyPipelineLayout(device, probeFilterLayout_, nullptr);
    if (probeInterpolatePipeline_) vkDestroyPipeline(device, probeInterpolatePipeline_, nullptr);
    if (probeInterpolateLayout_) vkDestroyPipelineLayout(device, probeInterpolateLayout_, nullptr);
    
    // Descriptors
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (descLayout_) vkDestroyDescriptorSetLayout(device, descLayout_, nullptr);
    
    // Sampler
    if (probeSampler_) vkDestroySampler(device, probeSampler_, nullptr);
    
    // Atlases
    if (probeAtlasView_) vkDestroyImageView(device, probeAtlasView_, nullptr);
    if (probeAtlas_) vkDestroyImage(device, probeAtlas_, nullptr);
    if (probeAtlasMemory_) vkFreeMemory(device, probeAtlasMemory_, nullptr);
    
    if (probeDepthView_) vkDestroyImageView(device, probeDepthView_, nullptr);
    if (probeDepthAtlas_) vkDestroyImage(device, probeDepthAtlas_, nullptr);
    if (probeDepthMemory_) vkFreeMemory(device, probeDepthMemory_, nullptr);
    
    if (historyView_) vkDestroyImageView(device, historyView_, nullptr);
    if (historyAtlas_) vkDestroyImage(device, historyAtlas_, nullptr);
    if (historyMemory_) vkFreeMemory(device, historyMemory_, nullptr);
    
    // Buffers
    if (probeBuffer_) vkDestroyBuffer(device, probeBuffer_, nullptr);
    if (probeMemory_) vkFreeMemory(device, probeMemory_, nullptr);
    
    if (tileBuffer_) vkDestroyBuffer(device, tileBuffer_, nullptr);
    if (tileMemory_) vkFreeMemory(device, tileMemory_, nullptr);
    
    if (rayBuffer_) vkDestroyBuffer(device, rayBuffer_, nullptr);
    if (rayMemory_) vkFreeMemory(device, rayMemory_, nullptr);
    
    initialized_ = false;
}

bool ScreenProbes::createProbeAtlas() {
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
    
    // Create radiance atlas
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = atlasConfig_.radianceFormat;
    imageInfo.extent = {atlasConfig_.atlasWidth, atlasConfig_.atlasHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &probeAtlas_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, probeAtlas_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &probeAtlasMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device, probeAtlas_, probeAtlasMemory_, 0);
    
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = probeAtlas_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = atlasConfig_.radianceFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &probeAtlasView_) != VK_SUCCESS) return false;
    
    // Create depth atlas
    imageInfo.format = atlasConfig_.depthFormat;
    if (vkCreateImage(device, &imageInfo, nullptr, &probeDepthAtlas_) != VK_SUCCESS) return false;
    
    vkGetImageMemoryRequirements(device, probeDepthAtlas_, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &probeDepthMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device, probeDepthAtlas_, probeDepthMemory_, 0);
    
    viewInfo.image = probeDepthAtlas_;
    viewInfo.format = atlasConfig_.depthFormat;
    if (vkCreateImageView(device, &viewInfo, nullptr, &probeDepthView_) != VK_SUCCESS) return false;
    
    // Create history atlas
    imageInfo.format = atlasConfig_.radianceFormat;
    if (vkCreateImage(device, &imageInfo, nullptr, &historyAtlas_) != VK_SUCCESS) return false;
    
    vkGetImageMemoryRequirements(device, historyAtlas_, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &historyMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device, historyAtlas_, historyMemory_, 0);
    
    viewInfo.image = historyAtlas_;
    viewInfo.format = atlasConfig_.radianceFormat;
    if (vkCreateImageView(device, &viewInfo, nullptr, &historyView_) != VK_SUCCESS) return false;
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &probeSampler_) != VK_SUCCESS) return false;
    
    return true;
}

bool ScreenProbes::createProbeBuffers() {
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
    
    VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    // Probe buffer
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(GPUScreenProbe) * probeCount_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &probeBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, probeBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.pNext = &flagsInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &probeMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, probeBuffer_, probeMemory_, 0);
    
    // Tile buffer
    bufferInfo.size = sizeof(uint32_t) * 4 * tileCountX_ * tileCountY_;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &tileBuffer_) != VK_SUCCESS) return false;
    
    vkGetBufferMemoryRequirements(device, tileBuffer_, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &tileMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, tileBuffer_, tileMemory_, 0);
    
    return true;
}

bool ScreenProbes::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Descriptor set layout
    VkDescriptorSetLayoutBinding bindings[6] = {};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 6;
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
    
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &probePlaceLayout_) != VK_SUCCESS) return false;
    
    VkShaderModule shaderModule = ShaderManager::loadShader("shaders/probe_place.comp");
    
    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = probePlaceLayout_;
    
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &probePlacePipeline_);
    
    return result == VK_SUCCESS;
}

void ScreenProbes::placeProbes(VkCommandBuffer cmd,
                                VkImageView depthView,
                                VkImageView normalView,
                                const glm::mat4& viewProj,
                                const glm::mat4& invViewProj) {
    // Would bind pipeline and dispatch
    uint32_t groupsX = tileCountX_;
    uint32_t groupsY = tileCountY_;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, probePlacePipeline_);
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    frameIndex_++;
}

void ScreenProbes::traceProbes(VkCommandBuffer cmd,
                                VkImageView gbufferAlbedo,
                                VkImageView gbufferNormal,
                                VkImageView gbufferDepth,
                                VkBuffer lightBuffer,
                                uint32_t lightCount) {
    // Would dispatch probe_trace.comp
}

void ScreenProbes::filterProbes(VkCommandBuffer cmd) {
    // Would dispatch probe_filter.comp
}

void ScreenProbes::interpolateToScreen(VkCommandBuffer cmd,
                                        VkImageView outputRadiance,
                                        VkImageView depthView,
                                        VkImageView normalView) {
    // Would dispatch probe_interpolate.comp
}
