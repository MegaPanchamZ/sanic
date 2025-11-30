/**
 * LumenRadiosity.cpp
 * 
 * Implementation of Lumen-style radiosity for multi-bounce lighting.
 */

#include "LumenRadiosity.h"
#include "VulkanContext.h"
#include <fstream>
#include <algorithm>
#include <cstring>

LumenRadiosity::~LumenRadiosity() {
    cleanup();
}

bool LumenRadiosity::initialize(VulkanContext* context,
                                 uint32_t surfaceCacheWidth,
                                 uint32_t surfaceCacheHeight,
                                 const RadiosityConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    surfaceCacheWidth_ = surfaceCacheWidth;
    surfaceCacheHeight_ = surfaceCacheHeight;
    
    // Calculate probe count from surface cache size and spacing
    uint32_t probeCountX = surfaceCacheWidth / config_.probeSpacing;
    uint32_t probeCountY = surfaceCacheHeight / config_.probeSpacing;
    probeCount_ = probeCountX * probeCountY;
    
    if (!createProbeBuffers()) { cleanup(); return false; }
    if (!createFrameData()) { cleanup(); return false; }
    if (!createPipelines()) { cleanup(); return false; }
    
    initialized_ = true;
    return true;
}

void LumenRadiosity::cleanup() {
    if (!context_) return;
    VkDevice device = context_->getDevice();
    
    // Pipelines
    auto destroyPipeline = [device](VkPipeline& p, VkPipelineLayout& l) {
        if (p) vkDestroyPipeline(device, p, nullptr);
        if (l) vkDestroyPipelineLayout(device, l, nullptr);
        p = VK_NULL_HANDLE;
        l = VK_NULL_HANDLE;
    };
    
    destroyPipeline(probePlacePipeline_, probePlaceLayout_);
    destroyPipeline(probeTracePipeline_, probeTraceLayout_);
    destroyPipeline(spatialFilterPipeline_, spatialFilterLayout_);
    destroyPipeline(convertSHPipeline_, convertSHLayout_);
    destroyPipeline(integratePipeline_, integrateLayout_);
    destroyPipeline(temporalPipeline_, temporalLayout_);
    destroyPipeline(rtTracePipeline_, rtTraceLayout_);
    
    // Descriptors
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (descLayout_) vkDestroyDescriptorSetLayout(device, descLayout_, nullptr);
    if (probeSampler_) vkDestroySampler(device, probeSampler_, nullptr);
    
    // Probe buffers
    if (probeBuffer_) vkDestroyBuffer(device, probeBuffer_, nullptr);
    if (probeMemory_) vkFreeMemory(device, probeMemory_, nullptr);
    if (shBuffer_) vkDestroyBuffer(device, shBuffer_, nullptr);
    if (shMemory_) vkFreeMemory(device, shMemory_, nullptr);
    
    // Frame data
    for (int i = 0; i < 2; i++) {
        auto& fd = frameData_[i];
        if (fd.traceRadianceView) vkDestroyImageView(device, fd.traceRadianceView, nullptr);
        if (fd.traceRadianceAtlas) vkDestroyImage(device, fd.traceRadianceAtlas, nullptr);
        if (fd.traceRadianceMemory) vkFreeMemory(device, fd.traceRadianceMemory, nullptr);
        
        if (fd.probeSHRedView) vkDestroyImageView(device, fd.probeSHRedView, nullptr);
        if (fd.probeSHRed) vkDestroyImage(device, fd.probeSHRed, nullptr);
        if (fd.probeSHRedMemory) vkFreeMemory(device, fd.probeSHRedMemory, nullptr);
        
        if (fd.probeSHGreenView) vkDestroyImageView(device, fd.probeSHGreenView, nullptr);
        if (fd.probeSHGreen) vkDestroyImage(device, fd.probeSHGreen, nullptr);
        if (fd.probeSHGreenMemory) vkFreeMemory(device, fd.probeSHGreenMemory, nullptr);
        
        if (fd.probeSHBlueView) vkDestroyImageView(device, fd.probeSHBlueView, nullptr);
        if (fd.probeSHBlue) vkDestroyImage(device, fd.probeSHBlue, nullptr);
        if (fd.probeSHBlueMemory) vkFreeMemory(device, fd.probeSHBlueMemory, nullptr);
    }
    
    // History
    if (historyView_) vkDestroyImageView(device, historyView_, nullptr);
    if (historyAtlas_) vkDestroyImage(device, historyAtlas_, nullptr);
    if (historyMemory_) vkFreeMemory(device, historyMemory_, nullptr);
    
    probes_.clear();
    initialized_ = false;
}

bool LumenRadiosity::createProbeBuffers() {
    VkDevice device = context_->getDevice();
    
    // Probe data buffer
    VkDeviceSize probeBufferSize = sizeof(GPURadiosityProbe) * probeCount_;
    
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = probeBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &probeBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, probeBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &probeMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, probeBuffer_, probeMemory_, 0);
    
    // SH coefficients buffer (9 vec4 per probe for RGB)
    VkDeviceSize shBufferSize = sizeof(GPUSH) * probeCount_;
    bufferInfo.size = shBufferSize;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &shBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    vkGetBufferMemoryRequirements(device, shBuffer_, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &shMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, shBuffer_, shMemory_, 0);
    
    // Initialize probe array
    probes_.resize(probeCount_);
    
    return true;
}

bool LumenRadiosity::createFrameData() {
    VkDevice device = context_->getDevice();
    
    // Probe atlas dimensions
    uint32_t probeCountX = surfaceCacheWidth_ / config_.probeSpacing;
    uint32_t probeCountY = surfaceCacheHeight_ / config_.probeSpacing;
    
    // Trace radiance atlas: probeCountX * hemisphereRes, probeCountY * hemisphereRes
    uint32_t traceWidth = probeCountX * config_.hemisphereResolution;
    uint32_t traceHeight = probeCountY * config_.hemisphereResolution;
    
    auto createImage = [&](VkImage& image, VkImageView& view, VkDeviceMemory& memory,
                           uint32_t width, uint32_t height, VkFormat format) -> bool {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        
        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            return false;
        }
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, image, &memReqs);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits,
                                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            return false;
        }
        
        vkBindImageMemory(device, image, memory, 0);
        
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        return vkCreateImageView(device, &viewInfo, nullptr, &view) == VK_SUCCESS;
    };
    
    for (int i = 0; i < 2; i++) {
        auto& fd = frameData_[i];
        fd.probeAtlasSize = glm::ivec2(probeCountX, probeCountY);
        
        // Trace radiance atlas (R11G11B10 for HDR radiance)
        if (!createImage(fd.traceRadianceAtlas, fd.traceRadianceView, fd.traceRadianceMemory,
                        traceWidth, traceHeight, VK_FORMAT_B10G11R11_UFLOAT_PACK32)) {
            return false;
        }
        
        // SH atlases (RGBA16F for SH coefficients, 3 vec4 per channel = 3 images)
        // Actually we store 9 coefficients per channel, so 3 vec4 = 12 floats, we need 3 images
        // For simplicity, use RGBA32F and store 3 SH basis per image
        if (!createImage(fd.probeSHRed, fd.probeSHRedView, fd.probeSHRedMemory,
                        probeCountX, probeCountY * 3, VK_FORMAT_R32G32B32A32_SFLOAT)) {
            return false;
        }
        
        if (!createImage(fd.probeSHGreen, fd.probeSHGreenView, fd.probeSHGreenMemory,
                        probeCountX, probeCountY * 3, VK_FORMAT_R32G32B32A32_SFLOAT)) {
            return false;
        }
        
        if (!createImage(fd.probeSHBlue, fd.probeSHBlueView, fd.probeSHBlueMemory,
                        probeCountX, probeCountY * 3, VK_FORMAT_R32G32B32A32_SFLOAT)) {
            return false;
        }
    }
    
    // History atlas for temporal accumulation
    if (!createImage(historyAtlas_, historyView_, historyMemory_,
                    probeCountX, probeCountY, VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return false;
    }
    
    // Sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    return vkCreateSampler(device, &samplerInfo, nullptr, &probeSampler_) == VK_SUCCESS;
}

bool LumenRadiosity::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Descriptor layout
    VkDescriptorSetLayoutBinding bindings[12] = {};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};   // Probes
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};   // SH
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Surface cache
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Surface depth
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Surface normal
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Global SDF
    bindings[6] = {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};  // Trace radiance
    bindings[7] = {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};  // SH Red
    bindings[8] = {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};  // SH Green
    bindings[9] = {9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};  // SH Blue
    bindings[10] = {10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Indirect lighting
    bindings[11] = {11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Lights
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 12;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Push constants
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = 128;
    
    VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &descLayout_;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pushRange;
    
    // Create all pipeline layouts
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &probePlaceLayout_) != VK_SUCCESS) return false;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &probeTraceLayout_) != VK_SUCCESS) return false;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &spatialFilterLayout_) != VK_SUCCESS) return false;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &convertSHLayout_) != VK_SUCCESS) return false;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &integrateLayout_) != VK_SUCCESS) return false;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &temporalLayout_) != VK_SUCCESS) return false;
    
    // Descriptor pool
    VkDescriptorPoolSize poolSizes[3] = {};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10};
    
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descPool_) != VK_SUCCESS) {
        return false;
    }
    
    // Allocate descriptor sets
    VkDescriptorSetLayout layouts[2] = {descLayout_, descLayout_};
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descPool_;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, descSet_) != VK_SUCCESS) {
        return false;
    }
    
    // Actual pipeline creation would load shaders here
    return true;
}

bool LumenRadiosity::loadShader(const std::string& path, VkShaderModule& outModule) {
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

void LumenRadiosity::placeProbes(VkCommandBuffer cmd,
                                  VkImageView surfaceCacheDepth,
                                  VkImageView surfaceCacheNormal) {
    if (!probePlacePipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, probePlacePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, probePlaceLayout_,
                            0, 1, &descSet_[frameIndex_ % 2], 0, nullptr);
    
    struct PushConstants {
        uint32_t surfaceWidth;
        uint32_t surfaceHeight;
        uint32_t probeSpacing;
        uint32_t frameIndex;
    } push;
    
    push.surfaceWidth = surfaceCacheWidth_;
    push.surfaceHeight = surfaceCacheHeight_;
    push.probeSpacing = config_.probeSpacing;
    push.frameIndex = frameIndex_;
    
    vkCmdPushConstants(cmd, probePlaceLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    uint32_t probeCountX = surfaceCacheWidth_ / config_.probeSpacing;
    uint32_t probeCountY = surfaceCacheHeight_ / config_.probeSpacing;
    
    vkCmdDispatch(cmd, (probeCountX + 7) / 8, (probeCountY + 7) / 8, 1);
}

void LumenRadiosity::traceProbes(VkCommandBuffer cmd,
                                  VkImageView surfaceCache,
                                  VkImageView globalSDF,
                                  VkBuffer lightBuffer,
                                  uint32_t lightCount) {
    if (!probeTracePipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, probeTracePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, probeTraceLayout_,
                            0, 1, &descSet_[frameIndex_ % 2], 0, nullptr);
    
    struct PushConstants {
        uint32_t probeCount;
        uint32_t hemisphereRes;
        uint32_t maxTraceDistance;
        float traceBias;
        uint32_t lightCount;
        uint32_t frameIndex;
        uint32_t pad0, pad1;
    } push;
    
    push.probeCount = probeCount_;
    push.hemisphereRes = config_.hemisphereResolution;
    push.maxTraceDistance = config_.maxTraceDistance;
    push.traceBias = config_.traceBias;
    push.lightCount = lightCount;
    push.frameIndex = frameIndex_;
    
    vkCmdPushConstants(cmd, probeTraceLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    // One workgroup per probe, hemisphereRes² threads per workgroup
    uint32_t workgroupSize = config_.hemisphereResolution * config_.hemisphereResolution;
    vkCmdDispatch(cmd, (probeCount_ + 63) / 64, 1, 1);
}

void LumenRadiosity::spatialFilter(VkCommandBuffer cmd) {
    if (!spatialFilterPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatialFilterPipeline_);
    
    struct PushConstants {
        float filterRadius;
        uint32_t usePlaneWeighting;
        uint32_t useProbeOcclusion;
        uint32_t probeCount;
    } push;
    
    push.filterRadius = config_.spatialFilterRadius;
    push.usePlaneWeighting = config_.usePlaneWeighting ? 1 : 0;
    push.useProbeOcclusion = config_.useProbeOcclusion ? 1 : 0;
    push.probeCount = probeCount_;
    
    vkCmdPushConstants(cmd, spatialFilterLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    uint32_t probeCountX = surfaceCacheWidth_ / config_.probeSpacing;
    uint32_t probeCountY = surfaceCacheHeight_ / config_.probeSpacing;
    
    vkCmdDispatch(cmd, (probeCountX + 7) / 8, (probeCountY + 7) / 8, 1);
}

void LumenRadiosity::convertToSH(VkCommandBuffer cmd) {
    if (!convertSHPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, convertSHPipeline_);
    
    struct PushConstants {
        uint32_t probeCount;
        uint32_t hemisphereRes;
        uint32_t pad0, pad1;
    } push;
    
    push.probeCount = probeCount_;
    push.hemisphereRes = config_.hemisphereResolution;
    
    vkCmdPushConstants(cmd, convertSHLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    vkCmdDispatch(cmd, (probeCount_ + 63) / 64, 1, 1);
}

void LumenRadiosity::integrateSH(VkCommandBuffer cmd,
                                  VkImageView indirectLightingAtlas) {
    if (!integratePipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, integratePipeline_);
    
    struct PushConstants {
        uint32_t surfaceWidth;
        uint32_t surfaceHeight;
        uint32_t probeSpacing;
        uint32_t pad;
    } push;
    
    push.surfaceWidth = surfaceCacheWidth_;
    push.surfaceHeight = surfaceCacheHeight_;
    push.probeSpacing = config_.probeSpacing;
    
    vkCmdPushConstants(cmd, integrateLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    vkCmdDispatch(cmd, (surfaceCacheWidth_ + 7) / 8, (surfaceCacheHeight_ + 7) / 8, 1);
}

void LumenRadiosity::temporalAccumulate(VkCommandBuffer cmd) {
    if (!temporalPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporalPipeline_);
    
    struct PushConstants {
        uint32_t probeCount;
        float temporalWeight;
        uint32_t frameIndex;
        uint32_t temporalFrames;
    } push;
    
    push.probeCount = probeCount_;
    push.temporalWeight = config_.temporalWeight;
    push.frameIndex = frameIndex_;
    push.temporalFrames = config_.temporalFrames;
    
    vkCmdPushConstants(cmd, temporalLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    vkCmdDispatch(cmd, (probeCount_ + 63) / 64, 1, 1);
}

void LumenRadiosity::update(VkCommandBuffer cmd,
                             VkImageView surfaceCache,
                             VkImageView surfaceCacheDepth,
                             VkImageView surfaceCacheNormal,
                             VkImageView globalSDF,
                             VkBuffer lightBuffer,
                             uint32_t lightCount,
                             VkImageView indirectLightingAtlas) {
    // Full radiosity update pipeline
    placeProbes(cmd, surfaceCacheDepth, surfaceCacheNormal);
    
    // Memory barrier
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    traceProbes(cmd, surfaceCache, globalSDF, lightBuffer, lightCount);
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    spatialFilter(cmd);
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    convertToSH(cmd);
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    integrateSH(cmd, indirectLightingAtlas);
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    temporalAccumulate(cmd);
    
    frameIndex_++;
}

VkImageView LumenRadiosity::getTraceRadianceView() const {
    return frameData_[frameIndex_ % 2].traceRadianceView;
}

LumenRadiosity::Stats LumenRadiosity::getStats() const {
    Stats stats{};
    stats.totalProbes = probeCount_;
    
    uint32_t validCount = 0;
    float totalValidity = 0.0f;
    for (const auto& probe : probes_) {
        if (probe.validity > 0.0f) {
            validCount++;
            totalValidity += probe.validity;
        }
    }
    
    stats.validProbes = validCount;
    stats.averageValidity = validCount > 0 ? totalValidity / validCount : 0.0f;
    stats.updatedThisFrame = std::min(probeCount_, config_.maxProbesPerFrame);
    
    return stats;
}

