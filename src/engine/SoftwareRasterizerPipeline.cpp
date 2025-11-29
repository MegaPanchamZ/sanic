/**
 * SoftwareRasterizerPipeline.cpp
 * 
 * Implementation of Nanite-style hybrid SW/HW rasterization.
 */

#include "SoftwareRasterizerPipeline.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

// Push constants for triangle binning
struct TriangleBinPushConstants {
    glm::mat4 viewProj;
    
    VkDeviceAddress visibleClusterBuffer;
    VkDeviceAddress clusterBuffer;
    VkDeviceAddress instanceBuffer;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress indexBuffer;
    VkDeviceAddress swTriangleBuffer;
    VkDeviceAddress hwBatchBuffer;
    VkDeviceAddress counters;
    
    uint32_t visibleClusterCount;
    uint32_t screenWidth;
    uint32_t screenHeight;
    float swThreshold;
};
static_assert(sizeof(TriangleBinPushConstants) == 144, "Push constants size");

// Push constants for SW rasterizer
struct SWRasterPushConstants {
    glm::mat4 viewProj;
    
    VkDeviceAddress visibleClusterBuffer;
    VkDeviceAddress clusterBuffer;
    VkDeviceAddress instanceBuffer;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress indexBuffer;
    VkDeviceAddress swTriangleBuffer;
    VkDeviceAddress visibilityBuffer;
    
    uint32_t swTriangleCount;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t pad;
};
static_assert(sizeof(SWRasterPushConstants) == 136, "Push constants size");

// Push constants for visibility buffer resolve
struct ResolvePushConstants {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    
    VkDeviceAddress clusterBuffer;
    VkDeviceAddress instanceBuffer;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress indexBuffer;
    VkDeviceAddress visibilityBuffer;
    
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t pad0;
    uint32_t pad1;
};
static_assert(sizeof(ResolvePushConstants) == 184, "Push constants size");

SoftwareRasterizerPipeline::~SoftwareRasterizerPipeline() {
    cleanup();
}

bool SoftwareRasterizerPipeline::initialize(VulkanContext* context, const RasterConfig& config) {
    if (initialized_) {
        return true;
    }
    
    context_ = context;
    config_ = config;
    
    if (!createBuffers()) {
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

void SoftwareRasterizerPipeline::cleanup() {
    if (!context_) return;
    
    VkDevice device = context_->getDevice();
    
    // Destroy pipelines
    if (triangleBinPipeline_) vkDestroyPipeline(device, triangleBinPipeline_, nullptr);
    if (swRasterPipeline_) vkDestroyPipeline(device, swRasterPipeline_, nullptr);
    if (resolveVisbufferPipeline_) vkDestroyPipeline(device, resolveVisbufferPipeline_, nullptr);
    
    // Destroy layouts
    if (triangleBinLayout_) vkDestroyPipelineLayout(device, triangleBinLayout_, nullptr);
    if (swRasterLayout_) vkDestroyPipelineLayout(device, swRasterLayout_, nullptr);
    if (resolveLayout_) vkDestroyPipelineLayout(device, resolveLayout_, nullptr);
    
    // Destroy descriptor resources
    if (resolveDescriptorPool_) vkDestroyDescriptorPool(device, resolveDescriptorPool_, nullptr);
    if (resolveDescriptorLayout_) vkDestroyDescriptorSetLayout(device, resolveDescriptorLayout_, nullptr);
    
    // Destroy buffers
    if (swTriangleBuffer_) vkDestroyBuffer(device, swTriangleBuffer_, nullptr);
    if (swTriangleMemory_) vkFreeMemory(device, swTriangleMemory_, nullptr);
    if (hwBatchBuffer_) vkDestroyBuffer(device, hwBatchBuffer_, nullptr);
    if (hwBatchMemory_) vkFreeMemory(device, hwBatchMemory_, nullptr);
    if (counterBuffer_) vkDestroyBuffer(device, counterBuffer_, nullptr);
    if (counterMemory_) vkFreeMemory(device, counterMemory_, nullptr);
    if (readbackBuffer_) vkDestroyBuffer(device, readbackBuffer_, nullptr);
    if (readbackMemory_) vkFreeMemory(device, readbackMemory_, nullptr);
    
    triangleBinPipeline_ = VK_NULL_HANDLE;
    swRasterPipeline_ = VK_NULL_HANDLE;
    resolveVisbufferPipeline_ = VK_NULL_HANDLE;
    triangleBinLayout_ = VK_NULL_HANDLE;
    swRasterLayout_ = VK_NULL_HANDLE;
    resolveLayout_ = VK_NULL_HANDLE;
    resolveDescriptorPool_ = VK_NULL_HANDLE;
    resolveDescriptorLayout_ = VK_NULL_HANDLE;
    swTriangleBuffer_ = VK_NULL_HANDLE;
    swTriangleMemory_ = VK_NULL_HANDLE;
    hwBatchBuffer_ = VK_NULL_HANDLE;
    hwBatchMemory_ = VK_NULL_HANDLE;
    counterBuffer_ = VK_NULL_HANDLE;
    counterMemory_ = VK_NULL_HANDLE;
    readbackBuffer_ = VK_NULL_HANDLE;
    readbackMemory_ = VK_NULL_HANDLE;
    
    initialized_ = false;
}

bool SoftwareRasterizerPipeline::createBuffers() {
    VkDevice device = context_->getDevice();
    
    // Helper to find memory type
    auto findMemoryType = [this](uint32_t typeFilter, VkMemoryPropertyFlags properties) -> uint32_t {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(context_->getPhysicalDevice(), &memProps);
        
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && 
                (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("Failed to find suitable memory type");
    };
    
    // Helper to create buffer with BDA
    auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags memProps,
                           VkBuffer& buffer, VkDeviceMemory& memory,
                           VkDeviceAddress* addr = nullptr) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        if (addr) {
            bufferInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        }
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            return false;
        }
        
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, buffer, &memReqs);
        
        VkMemoryAllocateFlagsInfo allocFlags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        allocFlags.flags = addr ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.pNext = addr ? &allocFlags : nullptr;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, memProps);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            return false;
        }
        
        vkBindBufferMemory(device, buffer, memory, 0);
        
        if (addr) {
            VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            addrInfo.buffer = buffer;
            *addr = vkGetBufferDeviceAddress(device, &addrInfo);
        }
        
        return true;
    };
    
    // Create SW triangle buffer
    VkDeviceSize swTriSize = sizeof(SWTriangle) * config_.maxSWTriangles;
    if (!createBuffer(swTriSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     swTriangleBuffer_, swTriangleMemory_, &swTriangleAddr_)) {
        return false;
    }
    
    // Create HW batch buffer
    VkDeviceSize hwBatchSize = sizeof(HWBatch) * config_.maxHWBatches;
    if (!createBuffer(hwBatchSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     hwBatchBuffer_, hwBatchMemory_, &hwBatchAddr_)) {
        return false;
    }
    
    // Create counter buffer
    if (!createBuffer(sizeof(BinningCounters),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     counterBuffer_, counterMemory_, &counterAddr_)) {
        return false;
    }
    
    // Create readback buffer
    if (!createBuffer(sizeof(BinningCounters),
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     readbackBuffer_, readbackMemory_)) {
        return false;
    }
    
    return true;
}

bool SoftwareRasterizerPipeline::loadShader(const std::string& path, VkShaderModule& outModule) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> code(fileSize);
    file.seekg(0);
    file.read(code.data(), fileSize);
    file.close();
    
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    return vkCreateShaderModule(context_->getDevice(), &createInfo, nullptr, &outModule) == VK_SUCCESS;
}

bool SoftwareRasterizerPipeline::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // =========================================================================
    // Triangle Binning Pipeline
    // =========================================================================
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TriangleBinPushConstants);
        
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &triangleBinLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkShaderModule shaderModule;
        if (!loadShader("shaders/triangle_bin.comp.spv", shaderModule)) {
            return false;
        }
        
        VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = triangleBinLayout_;
        
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &triangleBinPipeline_);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    // =========================================================================
    // SW Rasterizer Pipeline
    // =========================================================================
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(SWRasterPushConstants);
        
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &swRasterLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkShaderModule shaderModule;
        if (!loadShader("shaders/sw_rasterize.comp.spv", shaderModule)) {
            return false;
        }
        
        VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = swRasterLayout_;
        
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &swRasterPipeline_);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    // =========================================================================
    // Visibility Buffer Resolve Pipeline
    // =========================================================================
    {
        // Descriptor layout for G-Buffer images
        VkDescriptorSetLayoutBinding bindings[4] = {};
        for (int i = 0; i < 4; i++) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 4;
        layoutInfo.pBindings = bindings;
        
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &resolveDescriptorLayout_) != VK_SUCCESS) {
            return false;
        }
        
        // Create descriptor pool
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSize.descriptorCount = 4;
        
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &resolveDescriptorPool_) != VK_SUCCESS) {
            return false;
        }
        
        // Allocate descriptor set
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = resolveDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &resolveDescriptorLayout_;
        
        if (vkAllocateDescriptorSets(device, &allocInfo, &resolveDescriptorSet_) != VK_SUCCESS) {
            return false;
        }
        
        // Pipeline layout
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(ResolvePushConstants);
        
        VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeLayoutInfo.setLayoutCount = 1;
        pipeLayoutInfo.pSetLayouts = &resolveDescriptorLayout_;
        pipeLayoutInfo.pushConstantRangeCount = 1;
        pipeLayoutInfo.pPushConstantRanges = &pushRange;
        
        if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &resolveLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkShaderModule shaderModule;
        if (!loadShader("shaders/visbuffer_resolve.comp.spv", shaderModule)) {
            return false;
        }
        
        VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = resolveLayout_;
        
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &resolveVisbufferPipeline_);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

void SoftwareRasterizerPipeline::resetCounters(VkCommandBuffer cmd) {
    // Fill counter buffer with zeros
    vkCmdFillBuffer(cmd, counterBuffer_, 0, sizeof(BinningCounters), 0);
    
    // Memory barrier
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void SoftwareRasterizerPipeline::binTriangles(VkCommandBuffer cmd,
                                              VkBuffer visibleClusterBuffer,
                                              uint32_t visibleCount,
                                              VkBuffer clusterBuffer,
                                              VkBuffer instanceBuffer,
                                              VkBuffer vertexBuffer,
                                              VkBuffer indexBuffer,
                                              const glm::mat4& viewProj,
                                              uint32_t screenWidth,
                                              uint32_t screenHeight) {
    if (visibleCount == 0) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, triangleBinPipeline_);
    
    // Get buffer addresses
    VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    
    addrInfo.buffer = visibleClusterBuffer;
    VkDeviceAddress visibleClusterAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = clusterBuffer;
    VkDeviceAddress clusterAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = instanceBuffer;
    VkDeviceAddress instanceAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = vertexBuffer;
    VkDeviceAddress vertexAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = indexBuffer;
    VkDeviceAddress indexAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    TriangleBinPushConstants pc;
    pc.viewProj = viewProj;
    pc.visibleClusterBuffer = visibleClusterAddr;
    pc.clusterBuffer = clusterAddr;
    pc.instanceBuffer = instanceAddr;
    pc.vertexBuffer = vertexAddr;
    pc.indexBuffer = indexAddr;
    pc.swTriangleBuffer = swTriangleAddr_;
    pc.hwBatchBuffer = hwBatchAddr_;
    pc.counters = counterAddr_;
    pc.visibleClusterCount = visibleCount;
    pc.screenWidth = screenWidth;
    pc.screenHeight = screenHeight;
    pc.swThreshold = config_.swThreshold;
    
    vkCmdPushConstants(cmd, triangleBinLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(TriangleBinPushConstants), &pc);
    
    // Dispatch one workgroup per visible cluster
    vkCmdDispatch(cmd, visibleCount, 1, 1);
    
    // Memory barrier for binning results
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void SoftwareRasterizerPipeline::rasterizeSW(VkCommandBuffer cmd,
                                             VkBuffer visibilityBuffer,
                                             VkBuffer visibleClusterBuffer,
                                             VkBuffer clusterBuffer,
                                             VkBuffer instanceBuffer,
                                             VkBuffer vertexBuffer,
                                             VkBuffer indexBuffer,
                                             const glm::mat4& viewProj,
                                             uint32_t screenWidth,
                                             uint32_t screenHeight) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, swRasterPipeline_);
    
    // Get buffer addresses
    VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    
    addrInfo.buffer = visibleClusterBuffer;
    VkDeviceAddress visibleClusterAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = clusterBuffer;
    VkDeviceAddress clusterAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = instanceBuffer;
    VkDeviceAddress instanceAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = vertexBuffer;
    VkDeviceAddress vertexAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = indexBuffer;
    VkDeviceAddress indexAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = visibilityBuffer;
    VkDeviceAddress visibilityAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    SWRasterPushConstants pc;
    pc.viewProj = viewProj;
    pc.visibleClusterBuffer = visibleClusterAddr;
    pc.clusterBuffer = clusterAddr;
    pc.instanceBuffer = instanceAddr;
    pc.vertexBuffer = vertexAddr;
    pc.indexBuffer = indexAddr;
    pc.swTriangleBuffer = swTriangleAddr_;
    pc.visibilityBuffer = visibilityAddr;
    pc.swTriangleCount = 0; // Will be read from counter buffer via indirect
    pc.screenWidth = screenWidth;
    pc.screenHeight = screenHeight;
    pc.pad = 0;
    
    vkCmdPushConstants(cmd, swRasterLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(SWRasterPushConstants), &pc);
    
    // Dispatch indirectly based on SW triangle count
    // For now, use a maximum dispatch. In production, use vkCmdDispatchIndirect
    uint32_t maxGroups = (config_.maxSWTriangles + 63) / 64;
    vkCmdDispatch(cmd, std::min(maxGroups, 65535u), 1, 1);
    
    // Memory barrier for rasterization results
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void SoftwareRasterizerPipeline::resolveVisibilityBuffer(VkCommandBuffer cmd,
                                                         VkBuffer visibilityBuffer,
                                                         VkBuffer clusterBuffer,
                                                         VkBuffer instanceBuffer,
                                                         VkBuffer vertexBuffer,
                                                         VkBuffer indexBuffer,
                                                         VkImageView gbufferPosition,
                                                         VkImageView gbufferNormal,
                                                         VkImageView gbufferAlbedo,
                                                         VkImageView gbufferMaterial,
                                                         const glm::mat4& viewProj,
                                                         const glm::mat4& invViewProj,
                                                         uint32_t screenWidth,
                                                         uint32_t screenHeight) {
    // Update descriptor set with G-Buffer images
    VkDescriptorImageInfo imageInfos[4] = {};
    imageInfos[0].imageView = gbufferPosition;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[1].imageView = gbufferNormal;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[2].imageView = gbufferAlbedo;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[3].imageView = gbufferMaterial;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet writes[4] = {};
    for (int i = 0; i < 4; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = resolveDescriptorSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &imageInfos[i];
    }
    
    vkUpdateDescriptorSets(context_->getDevice(), 4, writes, 0, nullptr);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolveVisbufferPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolveLayout_,
                           0, 1, &resolveDescriptorSet_, 0, nullptr);
    
    // Get buffer addresses
    VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    
    addrInfo.buffer = clusterBuffer;
    VkDeviceAddress clusterAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = instanceBuffer;
    VkDeviceAddress instanceAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = vertexBuffer;
    VkDeviceAddress vertexAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = indexBuffer;
    VkDeviceAddress indexAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = visibilityBuffer;
    VkDeviceAddress visibilityAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    ResolvePushConstants pc;
    pc.viewProj = viewProj;
    pc.invViewProj = invViewProj;
    pc.clusterBuffer = clusterAddr;
    pc.instanceBuffer = instanceAddr;
    pc.vertexBuffer = vertexAddr;
    pc.indexBuffer = indexAddr;
    pc.visibilityBuffer = visibilityAddr;
    pc.screenWidth = screenWidth;
    pc.screenHeight = screenHeight;
    pc.pad0 = 0;
    pc.pad1 = 0;
    
    vkCmdPushConstants(cmd, resolveLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(ResolvePushConstants), &pc);
    
    // Dispatch for full screen (8x8 workgroups)
    uint32_t groupsX = (screenWidth + 7) / 8;
    uint32_t groupsY = (screenHeight + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

RasterStats SoftwareRasterizerPipeline::readbackStats() {
    RasterStats stats{};
    
    // Copy counter buffer to readback buffer
    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(BinningCounters);
    vkCmdCopyBuffer(cmd, counterBuffer_, readbackBuffer_, 1, &copyRegion);
    
    context_->endSingleTimeCommands(cmd);
    
    // Map and read
    void* mapped;
    vkMapMemory(context_->getDevice(), readbackMemory_, 0, sizeof(BinningCounters), 0, &mapped);
    
    BinningCounters counters;
    memcpy(&counters, mapped, sizeof(BinningCounters));
    
    vkUnmapMemory(context_->getDevice(), readbackMemory_);
    
    stats.swTriangles = counters.swTriangleCount;
    stats.hwBatches = counters.hwBatchCount;
    stats.swPixels = counters.totalSWPixels;
    stats.hwPixels = counters.totalHWPixels;
    
    if (stats.hwPixels > 0) {
        stats.swHwRatio = static_cast<float>(stats.swPixels) / static_cast<float>(stats.hwPixels);
    }
    
    return stats;
}
