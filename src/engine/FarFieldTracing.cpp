/**
 * FarFieldTracing.cpp
 * 
 * Implementation of far-field tracing using global SDF.
 */

#include "FarFieldTracing.h"
#include "VulkanContext.h"
#include <fstream>
#include <algorithm>
#include <cstring>

FarFieldTracing::~FarFieldTracing() {
    cleanup();
}

bool FarFieldTracing::initialize(VulkanContext* context, const FarFieldConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    
    // Check for hardware RT support
    // In production, check VkPhysicalDeviceRayTracingPipelineFeaturesKHR
    hasHardwareRT_ = false;
    
    if (!createGlobalSDF()) { cleanup(); return false; }
    if (!createBrickBuffer()) { cleanup(); return false; }
    if (!createPipelines()) { cleanup(); return false; }
    
    initialized_ = true;
    return true;
}

void FarFieldTracing::cleanup() {
    if (!context_) return;
    VkDevice device = context_->getDevice();
    
    // Pipelines
    if (sdfCompositePipeline_) vkDestroyPipeline(device, sdfCompositePipeline_, nullptr);
    if (sdfCompositeLayout_) vkDestroyPipelineLayout(device, sdfCompositeLayout_, nullptr);
    if (farFieldTracePipeline_) vkDestroyPipeline(device, farFieldTracePipeline_, nullptr);
    if (farFieldTraceLayout_) vkDestroyPipelineLayout(device, farFieldTraceLayout_, nullptr);
    if (farFieldProbePipeline_) vkDestroyPipeline(device, farFieldProbePipeline_, nullptr);
    if (farFieldProbeLayout_) vkDestroyPipelineLayout(device, farFieldProbeLayout_, nullptr);
    
    // Descriptors
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (descLayout_) vkDestroyDescriptorSetLayout(device, descLayout_, nullptr);
    if (sdfSampler_) vkDestroySampler(device, sdfSampler_, nullptr);
    
    // Global SDF
    if (globalSDFView_) vkDestroyImageView(device, globalSDFView_, nullptr);
    if (globalSDF_) vkDestroyImage(device, globalSDF_, nullptr);
    if (globalSDFMemory_) vkFreeMemory(device, globalSDFMemory_, nullptr);
    
    // Buffers
    if (brickBuffer_) vkDestroyBuffer(device, brickBuffer_, nullptr);
    if (brickMemory_) vkFreeMemory(device, brickMemory_, nullptr);
    if (sdfDataBuffer_) vkDestroyBuffer(device, sdfDataBuffer_, nullptr);
    if (sdfDataMemory_) vkFreeMemory(device, sdfDataMemory_, nullptr);
    
    // TLAS
    if (farFieldTLAS_ != VK_NULL_HANDLE) {
        // vkDestroyAccelerationStructureKHR(device, farFieldTLAS_, nullptr);
    }
    if (farFieldTLASBuffer_) vkDestroyBuffer(device, farFieldTLASBuffer_, nullptr);
    if (farFieldTLASMemory_) vkFreeMemory(device, farFieldTLASMemory_, nullptr);
    
    bricks_.clear();
    initialized_ = false;
}

bool FarFieldTracing::createGlobalSDF() {
    VkDevice device = context_->getDevice();
    uint32_t res = config_.globalSDFResolution;
    
    // Create 3D SDF texture
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R16_SFLOAT;  // Half-float distance
    imageInfo.extent = {res, res, res};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &globalSDF_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, globalSDF_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &globalSDFMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindImageMemory(device, globalSDF_, globalSDFMemory_, 0);
    
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = globalSDF_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R16_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &globalSDFView_) != VK_SUCCESS) {
        return false;
    }
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // Far distance at borders
    
    return vkCreateSampler(device, &samplerInfo, nullptr, &sdfSampler_) == VK_SUCCESS;
}

bool FarFieldTracing::createBrickBuffer() {
    VkDevice device = context_->getDevice();
    
    VkDeviceSize bufferSize = sizeof(GPUSDFBrick) * config_.maxBricks;
    
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &brickBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, brickBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &brickMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, brickBuffer_, brickMemory_, 0);
    
    // SDF data buffer - actual distance values
    uint32_t brickVoxels = config_.brickResolution * config_.brickResolution * config_.brickResolution;
    VkDeviceSize sdfDataSize = sizeof(uint16_t) * brickVoxels * config_.maxBricks;
    
    bufferInfo.size = sdfDataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &sdfDataBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    vkGetBufferMemoryRequirements(device, sdfDataBuffer_, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &sdfDataMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, sdfDataBuffer_, sdfDataMemory_, 0);
    
    return true;
}

bool FarFieldTracing::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Descriptor layout
    VkDescriptorSetLayoutBinding bindings[8] = {};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};  // Global SDF
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Bricks
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // SDF data
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Surface cache
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Ray origins
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Ray directions
    bindings[6] = {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};  // Output radiance
    bindings[7] = {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; // Probes
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 8;
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
    
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &sdfCompositeLayout_) != VK_SUCCESS) {
        return false;
    }
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &farFieldTraceLayout_) != VK_SUCCESS) {
        return false;
    }
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &farFieldProbeLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Create pipelines (shader modules would be loaded here)
    // For now, return true - actual pipeline creation requires compiled shaders
    
    return true;
}

bool FarFieldTracing::loadShader(const std::string& path, VkShaderModule& outModule) {
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

void FarFieldTracing::updateGlobalSDF(VkCommandBuffer cmd,
                                       const std::vector<VkImageView>& meshSDFs,
                                       const std::vector<glm::mat4>& transforms,
                                       const glm::vec3& cameraPos) {
    lastCameraPos_ = cameraPos;
    
    // Update brick allocation based on camera
    updateBrickAllocation(cameraPos);
    
    // Composite mesh SDFs into global SDF
    compositeMeshSDFs(cmd, meshSDFs, transforms);
    
    frameIndex_++;
}

void FarFieldTracing::compositeMeshSDFs(VkCommandBuffer cmd,
                                         const std::vector<VkImageView>& meshSDFs,
                                         const std::vector<glm::mat4>& transforms) {
    if (!sdfCompositePipeline_) return;
    
    // Transition global SDF to storage
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = globalSDF_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sdfCompositePipeline_);
    
    struct PushConstants {
        glm::vec4 cameraPos;
        glm::vec4 gridOrigin;
        float voxelSize;
        uint32_t resolution;
        uint32_t meshCount;
        float maxDistance;
    } push;
    
    push.cameraPos = glm::vec4(lastCameraPos_, 1.0f);
    push.gridOrigin = glm::vec4(
        lastCameraPos_.x - config_.globalSDFResolution * config_.globalSDFVoxelSize * 0.5f,
        lastCameraPos_.y - config_.globalSDFResolution * config_.globalSDFVoxelSize * 0.5f,
        lastCameraPos_.z - config_.globalSDFResolution * config_.globalSDFVoxelSize * 0.5f,
        0.0f
    );
    push.voxelSize = config_.globalSDFVoxelSize;
    push.resolution = config_.globalSDFResolution;
    push.meshCount = static_cast<uint32_t>(meshSDFs.size());
    push.maxDistance = config_.farFieldMaxDistance;
    
    vkCmdPushConstants(cmd, sdfCompositeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    uint32_t groupSize = (config_.globalSDFResolution + 7) / 8;
    vkCmdDispatch(cmd, groupSize, groupSize, groupSize);
    
    // Transition to sampled
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void FarFieldTracing::updateBrickAllocation(const glm::vec3& cameraPos) {
    // Simple brick allocation around camera
    bricks_.clear();
    
    float brickWorldSize = config_.brickResolution * config_.globalSDFVoxelSize;
    int bricksPerAxis = static_cast<int>(config_.farFieldMaxDistance / brickWorldSize);
    
    glm::ivec3 cameraBrick(
        static_cast<int>(cameraPos.x / brickWorldSize),
        static_cast<int>(cameraPos.y / brickWorldSize),
        static_cast<int>(cameraPos.z / brickWorldSize)
    );
    
    uint32_t brickIndex = 0;
    for (int z = -bricksPerAxis; z <= bricksPerAxis && brickIndex < config_.maxBricks; z++) {
        for (int y = -bricksPerAxis; y <= bricksPerAxis && brickIndex < config_.maxBricks; y++) {
            for (int x = -bricksPerAxis; x <= bricksPerAxis && brickIndex < config_.maxBricks; x++) {
                glm::ivec3 brickPos = cameraBrick + glm::ivec3(x, y, z);
                
                // Distance-based LOD
                float dist = glm::length(glm::vec3(x, y, z) * brickWorldSize);
                uint32_t mip = 0;
                if (dist > config_.nearFieldRadius) mip = 1;
                if (dist > config_.nearFieldRadius * 2.0f) mip = 2;
                
                SDFBrick brick;
                brick.position = brickPos;
                brick.mipLevel = mip;
                brick.boundsMin = glm::vec3(brickPos) * brickWorldSize;
                brick.boundsMax = brick.boundsMin + glm::vec3(brickWorldSize);
                brick.dataOffset = brickIndex * config_.brickResolution * config_.brickResolution * config_.brickResolution;
                brick.flags = 0;
                
                bricks_.push_back(brick);
                brickIndex++;
            }
        }
    }
}

void FarFieldTracing::traceFarField(VkCommandBuffer cmd,
                                     VkImageView rayOrigins,
                                     VkImageView rayDirections,
                                     VkImageView surfaceCache,
                                     VkImageView outputRadiance) {
    if (!farFieldTracePipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, farFieldTracePipeline_);
    
    struct PushConstants {
        glm::vec4 cameraPos;
        float nearFieldRadius;
        float farFieldMaxDistance;
        float transitionWidth;
        uint32_t samples;
        float roughnessBias;
        uint32_t frameIndex;
        uint32_t pad0, pad1;
    } push;
    
    push.cameraPos = glm::vec4(lastCameraPos_, 1.0f);
    push.nearFieldRadius = config_.nearFieldRadius;
    push.farFieldMaxDistance = config_.farFieldMaxDistance;
    push.transitionWidth = config_.transitionWidth;
    push.samples = config_.farFieldSamples;
    push.roughnessBias = config_.farFieldRoughnessBias;
    push.frameIndex = frameIndex_;
    
    vkCmdPushConstants(cmd, farFieldTraceLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    // Dispatch per-pixel - actual size from ray texture
    // For now use typical screen resolution
    vkCmdDispatch(cmd, 240, 135, 1);  // 1920/8, 1080/8
}

void FarFieldTracing::traceFarFieldProbes(VkCommandBuffer cmd,
                                           VkBuffer probeBuffer,
                                           uint32_t probeCount,
                                           VkImageView surfaceCache,
                                           VkBuffer outputRadiance) {
    if (!farFieldProbePipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, farFieldProbePipeline_);
    
    struct PushConstants {
        glm::vec4 cameraPos;
        float nearFieldRadius;
        float farFieldMaxDistance;
        uint32_t probeCount;
        uint32_t samples;
    } push;
    
    push.cameraPos = glm::vec4(lastCameraPos_, 1.0f);
    push.nearFieldRadius = config_.nearFieldRadius;
    push.farFieldMaxDistance = config_.farFieldMaxDistance;
    push.probeCount = probeCount;
    push.samples = config_.farFieldSamples;
    
    vkCmdPushConstants(cmd, farFieldProbeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    uint32_t groups = (probeCount + 63) / 64;
    vkCmdDispatch(cmd, groups, 1, 1);
}

void FarFieldTracing::buildFarFieldTLAS(VkCommandBuffer cmd,
                                         VkAccelerationStructureKHR* meshBLAS,
                                         const std::vector<glm::mat4>& transforms,
                                         uint32_t meshCount) {
    // Hardware RT TLAS building - requires ray tracing extension
    // Implementation would use vkCmdBuildAccelerationStructuresKHR
}

FarFieldTracing::Stats FarFieldTracing::getStats() const {
    Stats stats{};
    stats.activeBricks = static_cast<uint32_t>(bricks_.size());
    stats.sdfMemoryBytes = stats.activeBricks * config_.brickResolution * config_.brickResolution * 
                           config_.brickResolution * sizeof(uint16_t);
    stats.averageTraceDistance = (config_.nearFieldRadius + config_.farFieldMaxDistance) * 0.5f;
    stats.farFieldHits = 0;  // Would be updated from GPU readback
    return stats;
}

