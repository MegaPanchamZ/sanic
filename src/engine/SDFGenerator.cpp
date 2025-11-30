/**
 * SDFGenerator.cpp
 * 
 * Implementation of SDF generation system.
 */

#include "SDFGenerator.h"
#include "ShaderManager.h"
#include "VulkanContext.h"
#include <fstream>
#include <algorithm>
#include <cmath>

SDFGenerator::~SDFGenerator() {
    cleanup();
}

bool SDFGenerator::initialize(VulkanContext* context, const SDFConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    
    if (!createGlobalCascades()) { cleanup(); return false; }
    if (!createMeshAtlas()) { cleanup(); return false; }
    if (!createPipelines()) { cleanup(); return false; }
    
    initialized_ = true;
    return true;
}

void SDFGenerator::cleanup() {
    if (!context_) return;
    VkDevice device = context_->getDevice();
    
    // Pipelines
    if (meshSDFPipeline_) vkDestroyPipeline(device, meshSDFPipeline_, nullptr);
    if (meshSDFLayout_) vkDestroyPipelineLayout(device, meshSDFLayout_, nullptr);
    if (globalSDFPipeline_) vkDestroyPipeline(device, globalSDFPipeline_, nullptr);
    if (globalSDFLayout_) vkDestroyPipelineLayout(device, globalSDFLayout_, nullptr);
    if (sdfCombinePipeline_) vkDestroyPipeline(device, sdfCombinePipeline_, nullptr);
    if (sdfCombineLayout_) vkDestroyPipelineLayout(device, sdfCombineLayout_, nullptr);
    
    // Descriptors
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (meshSDFDescLayout_) vkDestroyDescriptorSetLayout(device, meshSDFDescLayout_, nullptr);
    if (globalSDFDescLayout_) vkDestroyDescriptorSetLayout(device, globalSDFDescLayout_, nullptr);
    
    // Mesh atlas
    if (meshAtlasView_) vkDestroyImageView(device, meshAtlasView_, nullptr);
    if (meshAtlas_) vkDestroyImage(device, meshAtlas_, nullptr);
    if (meshAtlasMemory_) vkFreeMemory(device, meshAtlasMemory_, nullptr);
    
    // Mesh descriptors
    if (meshDescBuffer_) vkDestroyBuffer(device, meshDescBuffer_, nullptr);
    if (meshDescMemory_) vkFreeMemory(device, meshDescMemory_, nullptr);
    
    // Cascades
    for (auto& cascade : cascades_) {
        if (cascade.volumeView) vkDestroyImageView(device, cascade.volumeView, nullptr);
        if (cascade.volumeImage) vkDestroyImage(device, cascade.volumeImage, nullptr);
        if (cascade.volumeMemory) vkFreeMemory(device, cascade.volumeMemory, nullptr);
    }
    cascades_.clear();
    
    // Mesh SDFs
    for (auto& [id, sdf] : meshSDFs_) {
        if (!sdf.inAtlas) {
            if (sdf.volumeView) vkDestroyImageView(device, sdf.volumeView, nullptr);
            if (sdf.volumeImage) vkDestroyImage(device, sdf.volumeImage, nullptr);
            if (sdf.volumeMemory) vkFreeMemory(device, sdf.volumeMemory, nullptr);
        }
    }
    meshSDFs_.clear();
    
    initialized_ = false;
}

bool SDFGenerator::createGlobalCascades() {
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
    
    cascades_.resize(config_.cascadeCount);
    
    float extent = config_.baseCascadeExtent;
    for (uint32_t i = 0; i < config_.cascadeCount; i++) {
        SDFCascade& cascade = cascades_[i];
        cascade.extent = glm::vec3(extent);
        cascade.center = glm::vec3(0.0f);
        cascade.resolution = glm::ivec3(config_.cascadeResolution);
        cascade.voxelSize = (extent * 2.0f) / config_.cascadeResolution;
        cascade.needsUpdate = true;
        
        // Create 3D image
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.format = VK_FORMAT_R16_SFLOAT;
        imageInfo.extent = {config_.cascadeResolution, config_.cascadeResolution, config_.cascadeResolution};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        
        if (vkCreateImage(device, &imageInfo, nullptr, &cascade.volumeImage) != VK_SUCCESS) return false;
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, cascade.volumeImage, &memReqs);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &cascade.volumeMemory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, cascade.volumeImage, cascade.volumeMemory, 0);
        
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = cascade.volumeImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format = VK_FORMAT_R16_SFLOAT;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        if (vkCreateImageView(device, &viewInfo, nullptr, &cascade.volumeView) != VK_SUCCESS) return false;
        
        extent *= config_.cascadeScale;
    }
    
    return true;
}

bool SDFGenerator::createMeshAtlas() {
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
    
    if (!config_.useMeshAtlas) return true;
    
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R16_SFLOAT;
    imageInfo.extent = {(uint32_t)config_.atlasResolution.x, 
                        (uint32_t)config_.atlasResolution.y, 
                        (uint32_t)config_.atlasResolution.z};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &meshAtlas_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, meshAtlas_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &meshAtlasMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device, meshAtlas_, meshAtlasMemory_, 0);
    
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = meshAtlas_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R16_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &meshAtlasView_) != VK_SUCCESS) return false;
    
    // Mesh descriptor buffer
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(GPUMeshSDF) * config_.maxMeshSDFs;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &meshDescBuffer_) != VK_SUCCESS) return false;
    
    vkGetBufferMemoryRequirements(device, meshDescBuffer_, &memReqs);
    
    VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    allocInfo.pNext = &flagsInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &meshDescMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, meshDescBuffer_, meshDescMemory_, 0);
    
    return true;
}

bool SDFGenerator::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Mesh SDF pipeline
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &meshSDFDescLayout_) != VK_SUCCESS) return false;
    
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = 128;
    
    VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &meshSDFDescLayout_;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pushRange;
    
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &meshSDFLayout_) != VK_SUCCESS) return false;
    
    VkShaderModule shaderModule = Sanic::ShaderManager::loadShader("shaders/sdf_generate_mesh.comp");
    
    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = meshSDFLayout_;
    
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &meshSDFPipeline_);
    
    return result == VK_SUCCESS;
}

float SDFGenerator::pointTriangleDistance(const glm::vec3& p,
                                           const glm::vec3& a,
                                           const glm::vec3& b,
                                           const glm::vec3& c) {
    glm::vec3 ba = b - a;
    glm::vec3 cb = c - b;
    glm::vec3 ac = a - c;
    glm::vec3 pa = p - a;
    glm::vec3 pb = p - b;
    glm::vec3 pc = p - c;
    
    glm::vec3 nor = glm::cross(ba, ac);
    float area2 = glm::length(nor);
    if (area2 < 1e-10f) return glm::length(pa);
    nor /= area2;
    
    float sba = glm::sign(glm::dot(glm::cross(ba, nor), pa));
    float scb = glm::sign(glm::dot(glm::cross(cb, nor), pb));
    float sac = glm::sign(glm::dot(glm::cross(ac, nor), pc));
    
    if (sba + scb + sac < 2.0f) {
        float t1 = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
        float d1 = glm::length(p - (a + ba * t1));
        
        float t2 = glm::clamp(glm::dot(pb, cb) / glm::dot(cb, cb), 0.0f, 1.0f);
        float d2 = glm::length(p - (b + cb * t2));
        
        float t3 = glm::clamp(glm::dot(pc, ac) / glm::dot(ac, ac), 0.0f, 1.0f);
        float d3 = glm::length(p - (c + ac * t3));
        
        return std::min({d1, d2, d3});
    }
    
    return std::abs(glm::dot(nor, pa));
}

void SDFGenerator::generateSDFCPU(const float* vertices, uint32_t vertexCount,
                                   const uint32_t* indices, uint32_t indexCount,
                                   std::vector<float>& outSDF,
                                   glm::ivec3 resolution,
                                   const glm::vec3& boundsMin,
                                   float voxelSize) {
    outSDF.resize(resolution.x * resolution.y * resolution.z);
    
    uint32_t triangleCount = indexCount / 3;
    
    for (int z = 0; z < resolution.z; z++) {
        for (int y = 0; y < resolution.y; y++) {
            for (int x = 0; x < resolution.x; x++) {
                glm::vec3 pos = boundsMin + glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f) * voxelSize;
                
                float minDist = 1e10f;
                
                for (uint32_t t = 0; t < triangleCount; t++) {
                    uint32_t i0 = indices[t * 3 + 0];
                    uint32_t i1 = indices[t * 3 + 1];
                    uint32_t i2 = indices[t * 3 + 2];
                    
                    glm::vec3 a(vertices[i0 * 3], vertices[i0 * 3 + 1], vertices[i0 * 3 + 2]);
                    glm::vec3 b(vertices[i1 * 3], vertices[i1 * 3 + 1], vertices[i1 * 3 + 2]);
                    glm::vec3 c(vertices[i2 * 3], vertices[i2 * 3 + 1], vertices[i2 * 3 + 2]);
                    
                    float dist = pointTriangleDistance(pos, a, b, c);
                    minDist = std::min(minDist, dist);
                }
                
                outSDF[z * resolution.x * resolution.y + y * resolution.x + x] = minDist;
            }
        }
    }
}

bool SDFGenerator::generateMeshSDF(uint32_t meshId,
                                    const float* vertices,
                                    uint32_t vertexCount,
                                    const uint32_t* indices,
                                    uint32_t indexCount) {
    // Compute bounds
    glm::vec3 boundsMin(1e10f);
    glm::vec3 boundsMax(-1e10f);
    
    for (uint32_t i = 0; i < vertexCount; i++) {
        glm::vec3 v(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
        boundsMin = glm::min(boundsMin, v);
        boundsMax = glm::max(boundsMax, v);
    }
    
    // Add padding
    glm::vec3 padding = (boundsMax - boundsMin) * config_.meshPadding;
    boundsMin -= padding;
    boundsMax += padding;
    
    // Compute resolution
    glm::vec3 size = boundsMax - boundsMin;
    float maxSize = std::max({size.x, size.y, size.z});
    float voxelSize = maxSize / config_.defaultMeshResolution;
    glm::ivec3 resolution = glm::ivec3(glm::ceil(size / voxelSize));
    
    // Generate on CPU (fallback)
    std::vector<float> sdfData;
    generateSDFCPU(vertices, vertexCount, indices, indexCount, sdfData, resolution, boundsMin, voxelSize);
    
    // Store
    MeshSDF sdf;
    sdf.meshId = meshId;
    sdf.resolution = resolution;
    sdf.voxelSize = voxelSize;
    sdf.boundsMin = boundsMin;
    sdf.boundsMax = boundsMax;
    sdf.inAtlas = false;  // Would allocate in atlas for production
    
    meshSDFs_[meshId] = sdf;
    
    return true;
}

void SDFGenerator::generateMeshSDFGPU(VkCommandBuffer cmd,
                                       uint32_t meshId,
                                       VkBuffer vertexBuffer,
                                       uint32_t vertexCount,
                                       VkBuffer indexBuffer,
                                       uint32_t indexCount,
                                       const glm::vec3& boundsMin,
                                       const glm::vec3& boundsMax) {
    // Would dispatch sdf_generate_mesh.comp
}

void SDFGenerator::updateGlobalSDF(VkCommandBuffer cmd,
                                    const glm::vec3& cameraPos,
                                    VkBuffer instanceBuffer,
                                    uint32_t instanceCount) {
    // Update cascade centers based on camera
    for (uint32_t i = 0; i < cascades_.size(); i++) {
        cascades_[i].center = cameraPos;
        cascades_[i].needsUpdate = true;
    }
    
    // Would dispatch sdf_global_update.comp for each cascade
    lastCameraPos_ = cameraPos;
}

const MeshSDF* SDFGenerator::getMeshSDF(uint32_t meshId) const {
    auto it = meshSDFs_.find(meshId);
    return it != meshSDFs_.end() ? &it->second : nullptr;
}

VkImageView SDFGenerator::getGlobalSDFView(uint32_t cascadeLevel) const {
    if (cascadeLevel < cascades_.size()) {
        return cascades_[cascadeLevel].volumeView;
    }
    return VK_NULL_HANDLE;
}

void SDFGenerator::getCascadeInfo(std::vector<CascadeInfo>& outInfo) const {
    outInfo.resize(cascades_.size());
    for (size_t i = 0; i < cascades_.size(); i++) {
        outInfo[i].centerExtent = glm::vec4(cascades_[i].center, cascades_[i].extent.x);
        outInfo[i].voxelSize = cascades_[i].voxelSize;
    }
}
