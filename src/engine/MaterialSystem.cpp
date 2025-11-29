/**
 * MaterialSystem.cpp
 * 
 * Implementation of the unified material and lighting system.
 */

#include "MaterialSystem.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

// Push constants for material binning
struct MaterialBinPushConstants {
    VkDeviceAddress visibilityBuffer;
    VkDeviceAddress clusterBuffer;
    VkDeviceAddress pixelWorkBuffer;
    VkDeviceAddress materialTileBuffer;
    VkDeviceAddress counters;
    
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t tileCountX;
    uint32_t tileCountY;
};

// Push constants for material evaluation
struct MaterialEvalPushConstants {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    
    VkDeviceAddress clusterBuffer;
    VkDeviceAddress instanceBuffer;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress indexBuffer;
    VkDeviceAddress pixelWorkBuffer;
    VkDeviceAddress materialBuffer;
    
    uint32_t workItemOffset;
    uint32_t workItemCount;
    uint32_t materialId;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
};

// Push constants for deferred lighting
struct LightingPushConstants {
    glm::vec3 cameraPos;
    float ambientIntensity;
    uint32_t screenWidth;
    uint32_t screenHeight;
    float exposure;
    float pad;
};

MaterialSystem::~MaterialSystem() {
    cleanup();
}

bool MaterialSystem::initialize(VulkanContext* context, const MaterialConfig& config) {
    if (initialized_) {
        return true;
    }
    
    context_ = context;
    config_ = config;
    
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

void MaterialSystem::cleanup() {
    if (!context_) return;
    
    VkDevice device = context_->getDevice();
    
    // Destroy pipelines
    if (materialBinPipeline_) vkDestroyPipeline(device, materialBinPipeline_, nullptr);
    if (materialEvalPipeline_) vkDestroyPipeline(device, materialEvalPipeline_, nullptr);
    if (deferredLightingPipeline_) vkDestroyPipeline(device, deferredLightingPipeline_, nullptr);
    
    // Destroy layouts
    if (materialBinLayout_) vkDestroyPipelineLayout(device, materialBinLayout_, nullptr);
    if (materialEvalLayout_) vkDestroyPipelineLayout(device, materialEvalLayout_, nullptr);
    if (deferredLightingLayout_) vkDestroyPipelineLayout(device, deferredLightingLayout_, nullptr);
    
    // Destroy descriptor resources
    if (bindlessDescriptorPool_) vkDestroyDescriptorPool(device, bindlessDescriptorPool_, nullptr);
    if (bindlessTextureLayout_) vkDestroyDescriptorSetLayout(device, bindlessTextureLayout_, nullptr);
    if (gbufferDescriptorPool_) vkDestroyDescriptorPool(device, gbufferDescriptorPool_, nullptr);
    if (gbufferLayout_) vkDestroyDescriptorSetLayout(device, gbufferLayout_, nullptr);
    
    // Destroy buffers
    auto destroyBuffer = [device](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf) vkDestroyBuffer(device, buf, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        buf = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    };
    
    destroyBuffer(materialBuffer_, materialMemory_);
    destroyBuffer(lightBuffer_, lightMemory_);
    destroyBuffer(pixelWorkBuffer_, pixelWorkMemory_);
    destroyBuffer(materialTileBuffer_, materialTileMemory_);
    destroyBuffer(counterBuffer_, counterMemory_);
    
    // Destroy sampler
    if (defaultSampler_) vkDestroySampler(device, defaultSampler_, nullptr);
    
    initialized_ = false;
}

bool MaterialSystem::createBuffers() {
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
    
    // Helper to create buffer
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
    
    // Material buffer
    if (!createBuffer(sizeof(GPUMaterial) * MAX_MATERIALS,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     materialBuffer_, materialMemory_, &materialAddr_)) {
        return false;
    }
    
    // Light buffer (header + lights)
    VkDeviceSize lightBufferSize = sizeof(uint32_t) * 4 + sizeof(GPULight) * MAX_LIGHTS;
    if (!createBuffer(lightBufferSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     lightBuffer_, lightMemory_, &lightAddr_)) {
        return false;
    }
    
    // Pixel work buffer
    if (!createBuffer(sizeof(PixelWorkItem) * config_.maxPixelWorkItems,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     pixelWorkBuffer_, pixelWorkMemory_, &pixelWorkAddr_)) {
        return false;
    }
    
    // Material tile buffer
    if (!createBuffer(sizeof(MaterialTile) * config_.maxMaterialTiles,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     materialTileBuffer_, materialTileMemory_, &materialTileAddr_)) {
        return false;
    }
    
    // Counter buffer
    if (!createBuffer(sizeof(MaterialCounters),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     counterBuffer_, counterMemory_, &counterAddr_)) {
        return false;
    }
    
    // Create default sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &defaultSampler_) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

bool MaterialSystem::createDescriptorSets() {
    VkDevice device = context_->getDevice();
    
    // Bindless texture descriptor layout
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = MAX_TEXTURES;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        VkDescriptorBindingFlags bindingFlags = 
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        
        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        bindingFlagsInfo.bindingCount = 1;
        bindingFlagsInfo.pBindingFlags = &bindingFlags;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.pNext = &bindingFlagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &bindlessTextureLayout_) != VK_SUCCESS) {
            return false;
        }
        
        // Create pool
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = MAX_TEXTURES;
        
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &bindlessDescriptorPool_) != VK_SUCCESS) {
            return false;
        }
        
        // Allocate descriptor set
        uint32_t variableCount = MAX_TEXTURES;
        VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
        variableInfo.descriptorSetCount = 1;
        variableInfo.pDescriptorCounts = &variableCount;
        
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.pNext = &variableInfo;
        allocInfo.descriptorPool = bindlessDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &bindlessTextureLayout_;
        
        if (vkAllocateDescriptorSets(device, &allocInfo, &bindlessDescriptorSet_) != VK_SUCCESS) {
            return false;
        }
    }
    
    // G-Buffer descriptor layout (for material eval and lighting)
    {
        VkDescriptorSetLayoutBinding bindings[8] = {};
        
        // G-Buffer images (0-3)
        for (int i = 0; i < 4; i++) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        
        // Bindless textures (4)
        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = MAX_TEXTURES;
        bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        // Environment maps (5-7)
        for (int i = 5; i < 8; i++) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 8;
        layoutInfo.pBindings = bindings;
        
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &gbufferLayout_) != VK_SUCCESS) {
            return false;
        }
        
        // Create pool
        VkDescriptorPoolSize poolSizes[2] = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[0].descriptorCount = 8;  // 4 per set, 2 sets
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = MAX_TEXTURES * 2 + 6;
        
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &gbufferDescriptorPool_) != VK_SUCCESS) {
            return false;
        }
        
        // Allocate descriptor sets
        VkDescriptorSetLayout layouts[2] = {gbufferLayout_, gbufferLayout_};
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = gbufferDescriptorPool_;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts;
        
        VkDescriptorSet sets[2];
        if (vkAllocateDescriptorSets(device, &allocInfo, sets) != VK_SUCCESS) {
            return false;
        }
        materialEvalDescriptorSet_ = sets[0];
        lightingDescriptorSet_ = sets[1];
    }
    
    return true;
}

bool MaterialSystem::loadShader(const std::string& path, VkShaderModule& outModule) {
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

bool MaterialSystem::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Material binning pipeline
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(MaterialBinPushConstants);
        
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &materialBinLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkShaderModule shaderModule;
        if (!loadShader("shaders/material_bin.comp.spv", shaderModule)) {
            return false;
        }
        
        VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = materialBinLayout_;
        
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &materialBinPipeline_);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    // Material evaluation pipeline
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(MaterialEvalPushConstants);
        
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &gbufferLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &materialEvalLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkShaderModule shaderModule;
        if (!loadShader("shaders/material_eval.comp.spv", shaderModule)) {
            return false;
        }
        
        VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = materialEvalLayout_;
        
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &materialEvalPipeline_);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    // Deferred lighting pipeline
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(LightingPushConstants);
        
        // Layout includes G-Buffer and light buffer
        VkDescriptorSetLayout layouts[1] = {gbufferLayout_};
        
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = layouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &deferredLightingLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkShaderModule shaderModule;
        if (!loadShader("shaders/deferred_lighting.comp.spv", shaderModule)) {
            return false;
        }
        
        VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = deferredLightingLayout_;
        
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &deferredLightingPipeline_);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

uint32_t MaterialSystem::registerMaterial(const GPUMaterial& material) {
    uint32_t id = static_cast<uint32_t>(materials_.size());
    materials_.push_back(material);
    dataDirty_ = true;
    return id;
}

void MaterialSystem::updateMaterial(uint32_t materialId, const GPUMaterial& material) {
    if (materialId < materials_.size()) {
        materials_[materialId] = material;
        dataDirty_ = true;
    }
}

uint32_t MaterialSystem::registerTexture(VkImageView imageView, VkSampler sampler) {
    uint32_t index = static_cast<uint32_t>(registeredTextures_.size());
    registeredTextures_.push_back(imageView);
    
    // Update bindless descriptor
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.sampler = sampler ? sampler : defaultSampler_;
    
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = bindlessDescriptorSet_;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(context_->getDevice(), 1, &write, 0, nullptr);
    
    return index;
}

uint32_t MaterialSystem::addLight(const GPULight& light) {
    uint32_t id = static_cast<uint32_t>(lights_.size());
    lights_.push_back(light);
    dataDirty_ = true;
    return id;
}

void MaterialSystem::updateLight(uint32_t lightId, const GPULight& light) {
    if (lightId < lights_.size()) {
        lights_[lightId] = light;
        dataDirty_ = true;
    }
}

void MaterialSystem::clearLights() {
    lights_.clear();
    dataDirty_ = true;
}

void MaterialSystem::uploadData(VkCommandBuffer cmd) {
    if (!dataDirty_) return;
    
    // Upload materials
    if (!materials_.empty()) {
        // Use staging buffer or direct copy
        // For simplicity, assuming host-visible staging
        VkDeviceSize size = sizeof(GPUMaterial) * materials_.size();
        
        // Create staging buffer, copy, destroy
        // ... (simplified - would use proper staging in production)
    }
    
    // Upload lights
    if (!lights_.empty()) {
        // Similar staging upload
    }
    
    dataDirty_ = false;
}

void MaterialSystem::resetCounters(VkCommandBuffer cmd) {
    vkCmdFillBuffer(cmd, counterBuffer_, 0, sizeof(MaterialCounters), 0);
    
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void MaterialSystem::binMaterials(VkCommandBuffer cmd,
                                  VkBuffer visibilityBuffer,
                                  VkBuffer clusterBuffer,
                                  uint32_t screenWidth,
                                  uint32_t screenHeight) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, materialBinPipeline_);
    
    VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    
    addrInfo.buffer = visibilityBuffer;
    VkDeviceAddress visAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = clusterBuffer;
    VkDeviceAddress clusterAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    uint32_t tileCountX = (screenWidth + 7) / 8;
    uint32_t tileCountY = (screenHeight + 7) / 8;
    
    MaterialBinPushConstants pc;
    pc.visibilityBuffer = visAddr;
    pc.clusterBuffer = clusterAddr;
    pc.pixelWorkBuffer = pixelWorkAddr_;
    pc.materialTileBuffer = materialTileAddr_;
    pc.counters = counterAddr_;
    pc.screenWidth = screenWidth;
    pc.screenHeight = screenHeight;
    pc.tileCountX = tileCountX;
    pc.tileCountY = tileCountY;
    
    vkCmdPushConstants(cmd, materialBinLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(MaterialBinPushConstants), &pc);
    
    vkCmdDispatch(cmd, tileCountX, tileCountY, 1);
    
    // Barrier for binning results
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void MaterialSystem::evaluateMaterials(VkCommandBuffer cmd,
                                       VkBuffer clusterBuffer,
                                       VkBuffer instanceBuffer,
                                       VkBuffer vertexBuffer,
                                       VkBuffer indexBuffer,
                                       const glm::mat4& viewProj,
                                       const glm::mat4& invViewProj,
                                       VkImageView gbufferPosition,
                                       VkImageView gbufferNormal,
                                       VkImageView gbufferAlbedo,
                                       VkImageView gbufferMaterial,
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
        writes[i].dstSet = materialEvalDescriptorSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &imageInfos[i];
    }
    
    vkUpdateDescriptorSets(context_->getDevice(), 4, writes, 0, nullptr);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, materialEvalPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, materialEvalLayout_,
                           0, 1, &materialEvalDescriptorSet_, 0, nullptr);
    
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
    
    // Dispatch per material
    // In production, would read back counters or use indirect dispatch
    for (uint32_t matId = 0; matId < materials_.size(); matId++) {
        MaterialEvalPushConstants pc;
        pc.viewProj = viewProj;
        pc.invViewProj = invViewProj;
        pc.clusterBuffer = clusterAddr;
        pc.instanceBuffer = instanceAddr;
        pc.vertexBuffer = vertexAddr;
        pc.indexBuffer = indexAddr;
        pc.pixelWorkBuffer = pixelWorkAddr_;
        pc.materialBuffer = materialAddr_;
        pc.workItemOffset = 0;  // Would be computed from counters
        pc.workItemCount = 0;   // Would be read from counters
        pc.materialId = matId;
        pc.screenWidth = screenWidth;
        pc.screenHeight = screenHeight;
        
        vkCmdPushConstants(cmd, materialEvalLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                          0, sizeof(MaterialEvalPushConstants), &pc);
        
        // Dispatch based on pixel count (would use indirect in production)
        // vkCmdDispatch(cmd, (pixelCount + 63) / 64, 1, 1);
    }
}

void MaterialSystem::performLighting(VkCommandBuffer cmd,
                                     VkImageView gbufferPosition,
                                     VkImageView gbufferNormal,
                                     VkImageView gbufferAlbedo,
                                     VkImageView gbufferMaterial,
                                     VkImageView outputImage,
                                     const glm::vec3& cameraPos,
                                     uint32_t screenWidth,
                                     uint32_t screenHeight,
                                     const LightingConfig& config) {
    // Update descriptor set
    VkDescriptorImageInfo imageInfos[5] = {};
    imageInfos[0].imageView = gbufferPosition;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[1].imageView = gbufferNormal;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[2].imageView = gbufferAlbedo;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[3].imageView = gbufferMaterial;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[4].imageView = outputImage;
    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet writes[5] = {};
    for (int i = 0; i < 5; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = lightingDescriptorSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &imageInfos[i];
    }
    
    vkUpdateDescriptorSets(context_->getDevice(), 5, writes, 0, nullptr);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, deferredLightingPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, deferredLightingLayout_,
                           0, 1, &lightingDescriptorSet_, 0, nullptr);
    
    LightingPushConstants pc;
    pc.cameraPos = cameraPos;
    pc.ambientIntensity = config.ambientIntensity;
    pc.screenWidth = screenWidth;
    pc.screenHeight = screenHeight;
    pc.exposure = config.exposure;
    
    vkCmdPushConstants(cmd, deferredLightingLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(LightingPushConstants), &pc);
    
    uint32_t groupsX = (screenWidth + 7) / 8;
    uint32_t groupsY = (screenHeight + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void MaterialSystem::setEnvironmentMaps(VkImageView irradianceMap,
                                        VkImageView prefilteredMap,
                                        VkImageView brdfLUT,
                                        VkSampler envSampler) {
    irradianceMap_ = irradianceMap;
    prefilteredMap_ = prefilteredMap;
    brdfLUT_ = brdfLUT;
    envSampler_ = envSampler;
    
    // Update descriptor set bindings 5-7
    VkDescriptorImageInfo imageInfos[3] = {};
    imageInfos[0].imageView = irradianceMap;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].sampler = envSampler;
    imageInfos[1].imageView = prefilteredMap;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler = envSampler;
    imageInfos[2].imageView = brdfLUT;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].sampler = envSampler;
    
    VkWriteDescriptorSet writes[3] = {};
    for (int i = 0; i < 3; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = lightingDescriptorSet_;
        writes[i].dstBinding = 5 + i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }
    
    vkUpdateDescriptorSets(context_->getDevice(), 3, writes, 0, nullptr);
}
