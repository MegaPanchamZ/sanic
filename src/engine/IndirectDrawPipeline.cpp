#include "IndirectDrawPipeline.h"
#include "VulkanContext.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

// ============================================================================
// Helper Functions
// ============================================================================

static std::vector<char> readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
    
    return shaderModule;
}

// ============================================================================
// IndirectDrawPipeline Implementation
// ============================================================================

IndirectDrawPipeline::IndirectDrawPipeline(VulkanContext& ctx, const Config& cfg)
    : context(ctx)
    , config(cfg)
{
    createBuffers();
    createPipeline();
}

IndirectDrawPipeline::~IndirectDrawPipeline() {
    destroyResources();
}

void IndirectDrawPipeline::destroyResources() {
    VkDevice device = context.getDevice();
    vkDeviceWaitIdle(device);
    
    // Destroy buffers
    if (indirectCommandBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indirectCommandBuffer, nullptr);
        vkFreeMemory(device, indirectCommandMemory, nullptr);
        indirectCommandBuffer = VK_NULL_HANDLE;
    }
    
    if (drawCountBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, drawCountBuffer, nullptr);
        vkFreeMemory(device, drawCountMemory, nullptr);
        drawCountBuffer = VK_NULL_HANDLE;
    }
    
    if (materialBinCounterBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, materialBinCounterBuffer, nullptr);
        vkFreeMemory(device, materialBinCounterMemory, nullptr);
        materialBinCounterBuffer = VK_NULL_HANDLE;
    }
    
    if (materialBinDataBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, materialBinDataBuffer, nullptr);
        vkFreeMemory(device, materialBinDataMemory, nullptr);
        materialBinDataBuffer = VK_NULL_HANDLE;
    }
    
    if (statsReadbackBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, statsReadbackBuffer, nullptr);
        vkFreeMemory(device, statsReadbackMemory, nullptr);
        statsReadbackBuffer = VK_NULL_HANDLE;
    }
    
    // Destroy pipeline resources
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    
    if (buildIndirectPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, buildIndirectPipeline, nullptr);
        buildIndirectPipeline = VK_NULL_HANDLE;
    }
}

void IndirectDrawPipeline::createBuffers() {
    VkDevice device = context.getDevice();
    
    // Indirect command buffer
    // VkDrawMeshTasksIndirectCommandEXT = 3 * uint32_t = 12 bytes
    VkDeviceSize indirectSize = config.maxDrawCommands * 12;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = indirectSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | 
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &indirectCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create indirect command buffer");
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, indirectCommandBuffer, &memReqs);
    
    VkMemoryAllocateFlagsInfo allocFlags{};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlags;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits,
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &indirectCommandMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate indirect command memory");
    }
    
    vkBindBufferMemory(device, indirectCommandBuffer, indirectCommandMemory, 0);
    
    // Draw count buffer (4 uint32_t: total, hw, sw, padding)
    bufferInfo.size = 16;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &drawCountBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create draw count buffer");
    }
    
    vkGetBufferMemoryRequirements(device, drawCountBuffer, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &drawCountMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate draw count memory");
    }
    
    vkBindBufferMemory(device, drawCountBuffer, drawCountMemory, 0);
    
    // Material bin counters (one uint32_t per material)
    bufferInfo.size = config.maxMaterials * sizeof(uint32_t);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &materialBinCounterBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create material bin counter buffer");
    }
    
    vkGetBufferMemoryRequirements(device, materialBinCounterBuffer, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &materialBinCounterMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate material bin counter memory");
    }
    
    vkBindBufferMemory(device, materialBinCounterBuffer, materialBinCounterMemory, 0);
    
    // Material bin data (cluster indices per material)
    VkDeviceSize binDataSize = config.maxMaterials * config.maxClustersPerMaterial * sizeof(uint32_t);
    bufferInfo.size = binDataSize;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &materialBinDataBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create material bin data buffer");
    }
    
    vkGetBufferMemoryRequirements(device, materialBinDataBuffer, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &materialBinDataMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate material bin data memory");
    }
    
    vkBindBufferMemory(device, materialBinDataBuffer, materialBinDataMemory, 0);
    
    // Stats readback buffer (host visible)
    bufferInfo.size = sizeof(DrawStats);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &statsReadbackBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create stats readback buffer");
    }
    
    vkGetBufferMemoryRequirements(device, statsReadbackBuffer, &memReqs);
    
    VkMemoryAllocateInfo hostAllocInfo{};
    hostAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    hostAllocInfo.allocationSize = memReqs.size;
    hostAllocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &hostAllocInfo, nullptr, &statsReadbackMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate stats readback memory");
    }
    
    vkBindBufferMemory(device, statsReadbackBuffer, statsReadbackMemory, 0);
}

void IndirectDrawPipeline::createPipeline() {
    VkDevice device = context.getDevice();
    
    // Load shader
    auto shaderCode = readShaderFile("shaders/build_indirect.spv");
    VkShaderModule shaderModule = createShaderModule(device, shaderCode);
    
    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BuildIndirectPushConstants);
    
    // Pipeline layout (no descriptor sets, just push constants)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create indirect draw pipeline layout");
    }
    
    // Compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &buildIndirectPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create indirect draw compute pipeline");
    }
    
    vkDestroyShaderModule(device, shaderModule, nullptr);
}

void IndirectDrawPipeline::resetCounters(VkCommandBuffer cmd) {
    // Reset draw count buffer
    vkCmdFillBuffer(cmd, drawCountBuffer, 0, 16, 0);
    
    // Reset material bin counters
    vkCmdFillBuffer(cmd, materialBinCounterBuffer, 0, 
                    config.maxMaterials * sizeof(uint32_t), 0);
    
    // Barrier to ensure fills complete before compute
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr);
}

void IndirectDrawPipeline::buildDrawCommands(VkCommandBuffer cmd,
                                              VkBuffer visibleClusterBuffer,
                                              VkBuffer clusterBuffer,
                                              uint32_t visibleClusterCount) {
    // Get buffer addresses
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    
    addressInfo.buffer = visibleClusterBuffer;
    VkDeviceAddress visibleClusterAddr = vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
    
    addressInfo.buffer = clusterBuffer;
    VkDeviceAddress clusterAddr = vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
    
    addressInfo.buffer = indirectCommandBuffer;
    VkDeviceAddress indirectAddr = vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
    
    addressInfo.buffer = drawCountBuffer;
    VkDeviceAddress drawCountAddr = vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
    
    addressInfo.buffer = materialBinCounterBuffer;
    VkDeviceAddress binCounterAddr = vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
    
    addressInfo.buffer = materialBinDataBuffer;
    VkDeviceAddress binDataAddr = vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
    
    // Build push constants
    BuildIndirectPushConstants pc{};
    pc.visibleClusterBuffer = visibleClusterAddr;
    pc.clusterBuffer = clusterAddr;
    pc.indirectBuffer = indirectAddr;
    pc.drawCountBuffer = drawCountAddr;
    pc.materialBinCounters = binCounterAddr;
    pc.materialBinData = binDataAddr;
    pc.visibleClusterCount = visibleClusterCount;
    pc.maxClustersPerBin = config.maxClustersPerMaterial;
    pc.materialCount = config.maxMaterials;
    pc.meshletsPerCluster = 8;  // Average estimate
    
    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, buildIndirectPipeline);
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                       0, sizeof(BuildIndirectPushConstants), &pc);
    
    uint32_t groupCount = (visibleClusterCount + 63) / 64;
    vkCmdDispatch(cmd, groupCount, 1, 1);
    
    // Barrier before indirect draw
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr);
}

VkDeviceAddress IndirectDrawPipeline::getIndirectBufferAddress() const {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = indirectCommandBuffer;
    return vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
}

VkDeviceAddress IndirectDrawPipeline::getDrawCountBufferAddress() const {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = drawCountBuffer;
    return vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
}

IndirectDrawPipeline::DrawStats IndirectDrawPipeline::getDrawStats() {
    // Copy draw count buffer to readback buffer
    VkCommandBuffer cmd = context.beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = sizeof(DrawStats);
    
    vkCmdCopyBuffer(cmd, drawCountBuffer, statsReadbackBuffer, 1, &copyRegion);
    
    context.endSingleTimeCommands(cmd);
    
    // Map and read
    DrawStats stats{};
    void* data;
    vkMapMemory(context.getDevice(), statsReadbackMemory, 0, sizeof(DrawStats), 0, &data);
    memcpy(&stats, data, sizeof(DrawStats));
    vkUnmapMemory(context.getDevice(), statsReadbackMemory);
    
    return stats;
}
