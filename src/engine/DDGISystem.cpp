#include "DDGISystem.h"
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <cmath>
#include <random>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>

DDGISystem::DDGISystem(VulkanContext& context, VkDescriptorPool descriptorPool)
    : context(context)
    , device(context.getDevice())
    , physicalDevice(context.getPhysicalDevice())
    , descriptorPool(descriptorPool) {
}

DDGISystem::~DDGISystem() {
    vkDeviceWaitIdle(device);
    
    // Cleanup pipelines
    if (rayTracePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, rayTracePipeline, nullptr);
    }
    if (rayTracePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, rayTracePipelineLayout, nullptr);
    }
    if (probeUpdatePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, probeUpdatePipeline, nullptr);
    }
    if (probeUpdatePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, probeUpdatePipelineLayout, nullptr);
    }
    
    // Cleanup descriptor set layout
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }
    
    // Cleanup sampler
    if (probeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, probeSampler, nullptr);
    }
    
    // Cleanup uniform buffer
    if (uniformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, uniformBuffer, nullptr);
        vkFreeMemory(device, uniformBufferMemory, nullptr);
    }
    
    // Cleanup radiance buffer
    if (radianceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, radianceBuffer, nullptr);
        vkFreeMemory(device, radianceMemory, nullptr);
    }
    
    // Cleanup irradiance texture
    if (irradianceImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, irradianceImageView, nullptr);
    }
    if (irradianceImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, irradianceImage, nullptr);
        vkFreeMemory(device, irradianceMemory, nullptr);
    }
    
    // Cleanup depth texture
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
    }
    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthMemory, nullptr);
    }
}

void DDGISystem::initialize(const DDGIConfig& cfg) {
    config = cfg;
    
    std::cout << "Initializing DDGI System..." << std::endl;
    std::cout << "  Probe Grid: " << config.probeCount.x << "x" << config.probeCount.y << "x" << config.probeCount.z << std::endl;
    std::cout << "  Total Probes: " << (config.probeCount.x * config.probeCount.y * config.probeCount.z) << std::endl;
    std::cout << "  Rays per Probe: " << config.raysPerProbe << std::endl;
    
    createProbeTextures();
    createRadianceBuffer();
    createUniformBuffer();
    createSampler();
    createDescriptorSetLayout();
    createDescriptorSet();
    createRayTracePipeline();
    createProbeUpdatePipeline();
    
    std::cout << "DDGI System initialized successfully!" << std::endl;
}

void DDGISystem::createProbeTextures() {
    // Calculate texture atlas dimensions
    // Probes are laid out in a 2D atlas: (probeCount.x * probeCount.z) wide, probeCount.y tall
    // Each probe occupies (irradianceProbeSize x irradianceProbeSize) pixels
    
    int probesPerRow = config.probeCount.x * config.probeCount.z;
    int probeRows = config.probeCount.y;
    
    uint32_t irradianceWidth = probesPerRow * config.irradianceProbeSize;
    uint32_t irradianceHeight = probeRows * config.irradianceProbeSize;
    
    uint32_t depthWidth = probesPerRow * config.depthProbeSize;
    uint32_t depthHeight = probeRows * config.depthProbeSize;
    
    std::cout << "  Irradiance Atlas: " << irradianceWidth << "x" << irradianceHeight << std::endl;
    std::cout << "  Depth Atlas: " << depthWidth << "x" << depthHeight << std::endl;
    
    // Create irradiance texture (RGBA16F for HDR irradiance)
    createStorageImage(irradianceWidth, irradianceHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
                      irradianceImage, irradianceMemory, irradianceImageView);
    
    // Create depth texture (RG16F for mean depth and variance)
    createStorageImage(depthWidth, depthHeight, VK_FORMAT_R16G16_SFLOAT,
                      depthImage, depthMemory, depthImageView);
}

void DDGISystem::createStorageImage(uint32_t width, uint32_t height, VkFormat format,
                                    VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI storage image!");
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DDGI image memory!");
    }
    
    vkBindImageMemory(device, image, memory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI image view!");
    }
}

void DDGISystem::createRadianceBuffer() {
    // Buffer to store ray trace results: vec4(radiance.rgb, hitDistance) per ray
    int totalProbes = config.probeCount.x * config.probeCount.y * config.probeCount.z;
    VkDeviceSize bufferSize = totalProbes * config.raysPerProbe * sizeof(glm::vec4);
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &radianceBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI radiance buffer!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, radianceBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &radianceMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DDGI radiance buffer memory!");
    }
    
    vkBindBufferMemory(device, radianceBuffer, radianceMemory, 0);
    
    std::cout << "  Radiance Buffer: " << (bufferSize / 1024) << " KB" << std::endl;
}

void DDGISystem::createUniformBuffer() {
    VkDeviceSize bufferSize = sizeof(DDGIUniforms);
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &uniformBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI uniform buffer!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, uniformBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &uniformBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DDGI uniform buffer memory!");
    }
    
    vkBindBufferMemory(device, uniformBuffer, uniformBufferMemory, 0);
    vkMapMemory(device, uniformBufferMemory, 0, bufferSize, 0, &uniformBufferMapped);
}

void DDGISystem::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &probeSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI sampler!");
    }
}

void DDGISystem::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    
    // Binding 0: TLAS for ray tracing
    VkDescriptorSetLayoutBinding tlasBinding{};
    tlasBinding.binding = 0;
    tlasBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    tlasBinding.descriptorCount = 1;
    tlasBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(tlasBinding);
    
    // Binding 1: DDGI Uniforms
    VkDescriptorSetLayoutBinding uniformBinding{};
    uniformBinding.binding = 1;
    uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBinding.descriptorCount = 1;
    uniformBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(uniformBinding);
    
    // Binding 2: Radiance buffer (storage)
    VkDescriptorSetLayoutBinding radianceBinding{};
    radianceBinding.binding = 2;
    radianceBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    radianceBinding.descriptorCount = 1;
    radianceBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(radianceBinding);
    
    // Binding 3: Irradiance texture (storage image for write, sampled for read)
    VkDescriptorSetLayoutBinding irradianceBinding{};
    irradianceBinding.binding = 3;
    irradianceBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    irradianceBinding.descriptorCount = 1;
    irradianceBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(irradianceBinding);
    
    // Binding 4: Depth texture (storage image for write)
    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 4;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(depthBinding);
    
    // Binding 5: Irradiance texture sampler (for reading in composition)
    VkDescriptorSetLayoutBinding irradianceSamplerBinding{};
    irradianceSamplerBinding.binding = 5;
    irradianceSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    irradianceSamplerBinding.descriptorCount = 1;
    irradianceSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(irradianceSamplerBinding);
    
    // Binding 6: Depth texture sampler (for reading in composition)
    VkDescriptorSetLayoutBinding depthSamplerBinding{};
    depthSamplerBinding.binding = 6;
    depthSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthSamplerBinding.descriptorCount = 1;
    depthSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(depthSamplerBinding);
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI descriptor set layout!");
    }
}

void DDGISystem::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DDGI descriptor set!");
    }
    
    std::vector<VkWriteDescriptorSet> writes;
    
    // We'll update TLAS binding when setAccelerationStructure is called
    
    // Binding 1: Uniforms
    VkDescriptorBufferInfo uniformInfo{};
    uniformInfo.buffer = uniformBuffer;
    uniformInfo.offset = 0;
    uniformInfo.range = sizeof(DDGIUniforms);
    
    VkWriteDescriptorSet uniformWrite{};
    uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uniformWrite.dstSet = descriptorSet;
    uniformWrite.dstBinding = 1;
    uniformWrite.descriptorCount = 1;
    uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformWrite.pBufferInfo = &uniformInfo;
    writes.push_back(uniformWrite);
    
    // Binding 2: Radiance buffer
    int totalProbes = config.probeCount.x * config.probeCount.y * config.probeCount.z;
    VkDescriptorBufferInfo radianceInfo{};
    radianceInfo.buffer = radianceBuffer;
    radianceInfo.offset = 0;
    radianceInfo.range = totalProbes * config.raysPerProbe * sizeof(glm::vec4);
    
    VkWriteDescriptorSet radianceWrite{};
    radianceWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    radianceWrite.dstSet = descriptorSet;
    radianceWrite.dstBinding = 2;
    radianceWrite.descriptorCount = 1;
    radianceWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    radianceWrite.pBufferInfo = &radianceInfo;
    writes.push_back(radianceWrite);
    
    // Binding 3: Irradiance storage image
    VkDescriptorImageInfo irradianceStorageInfo{};
    irradianceStorageInfo.imageView = irradianceImageView;
    irradianceStorageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet irradianceStorageWrite{};
    irradianceStorageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    irradianceStorageWrite.dstSet = descriptorSet;
    irradianceStorageWrite.dstBinding = 3;
    irradianceStorageWrite.descriptorCount = 1;
    irradianceStorageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    irradianceStorageWrite.pImageInfo = &irradianceStorageInfo;
    writes.push_back(irradianceStorageWrite);
    
    // Binding 4: Depth storage image
    VkDescriptorImageInfo depthStorageInfo{};
    depthStorageInfo.imageView = depthImageView;
    depthStorageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet depthStorageWrite{};
    depthStorageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthStorageWrite.dstSet = descriptorSet;
    depthStorageWrite.dstBinding = 4;
    depthStorageWrite.descriptorCount = 1;
    depthStorageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    depthStorageWrite.pImageInfo = &depthStorageInfo;
    writes.push_back(depthStorageWrite);
    
    // Binding 5: Irradiance sampler
    VkDescriptorImageInfo irradianceSamplerInfo{};
    irradianceSamplerInfo.imageView = irradianceImageView;
    irradianceSamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    irradianceSamplerInfo.sampler = probeSampler;
    
    VkWriteDescriptorSet irradianceSamplerWrite{};
    irradianceSamplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    irradianceSamplerWrite.dstSet = descriptorSet;
    irradianceSamplerWrite.dstBinding = 5;
    irradianceSamplerWrite.descriptorCount = 1;
    irradianceSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    irradianceSamplerWrite.pImageInfo = &irradianceSamplerInfo;
    writes.push_back(irradianceSamplerWrite);
    
    // Binding 6: Depth sampler
    VkDescriptorImageInfo depthSamplerInfo{};
    depthSamplerInfo.imageView = depthImageView;
    depthSamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthSamplerInfo.sampler = probeSampler;
    
    VkWriteDescriptorSet depthSamplerWrite{};
    depthSamplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthSamplerWrite.dstSet = descriptorSet;
    depthSamplerWrite.dstBinding = 6;
    depthSamplerWrite.descriptorCount = 1;
    depthSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthSamplerWrite.pImageInfo = &depthSamplerInfo;
    writes.push_back(depthSamplerWrite);
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void DDGISystem::setAccelerationStructure(VkAccelerationStructureKHR newTlas) {
    tlas = newTlas;
    
    // Update descriptor set with TLAS
    VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
    asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount = 1;
    asInfo.pAccelerationStructures = &tlas;
    
    VkWriteDescriptorSet tlasWrite{};
    tlasWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    tlasWrite.pNext = &asInfo;
    tlasWrite.dstSet = descriptorSet;
    tlasWrite.dstBinding = 0;
    tlasWrite.descriptorCount = 1;
    tlasWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    
    vkUpdateDescriptorSets(device, 1, &tlasWrite, 0, nullptr);
}

void DDGISystem::createRayTracePipeline() {
    // Load compute shader for ray tracing probes
    std::ifstream file("shaders/ddgi_probe_trace.comp.spv", std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cout << "Warning: DDGI ray trace shader not found, skipping pipeline creation" << std::endl;
        return;
    }
    
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> code(fileSize);
    file.seekg(0);
    file.read(code.data(), fileSize);
    file.close();
    
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI ray trace shader module!");
    }
    
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &rayTracePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI ray trace pipeline layout!");
    }
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = rayTracePipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rayTracePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI ray trace pipeline!");
    }
    
    vkDestroyShaderModule(device, shaderModule, nullptr);
    std::cout << "  DDGI Ray Trace Pipeline created" << std::endl;
}

void DDGISystem::createProbeUpdatePipeline() {
    // Load compute shader for updating probes
    std::ifstream file("shaders/ddgi_probe_update.comp.spv", std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cout << "Warning: DDGI probe update shader not found, skipping pipeline creation" << std::endl;
        return;
    }
    
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> code(fileSize);
    file.seekg(0);
    file.read(code.data(), fileSize);
    file.close();
    
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI probe update shader module!");
    }
    
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &probeUpdatePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI probe update pipeline layout!");
    }
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = probeUpdatePipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &probeUpdatePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DDGI probe update pipeline!");
    }
    
    vkDestroyShaderModule(device, shaderModule, nullptr);
    std::cout << "  DDGI Probe Update Pipeline created" << std::endl;
}

void DDGISystem::updateUniforms(uint32_t frameIndex) {
    DDGIUniforms uniforms{};
    
    int totalProbes = config.probeCount.x * config.probeCount.y * config.probeCount.z;
    uniforms.probeCount = glm::ivec4(config.probeCount, totalProbes);
    uniforms.probeSpacing = glm::vec4(config.probeSpacing, 1.0f / config.maxRayDistance);
    uniforms.gridOrigin = glm::vec4(config.gridOrigin, config.hysteresis);
    
    // Calculate texture atlas dimensions
    int probesPerRow = config.probeCount.x * config.probeCount.z;
    int probeRows = config.probeCount.y;
    
    uniforms.irradianceTextureSize = glm::ivec4(
        probesPerRow * config.irradianceProbeSize,
        probeRows * config.irradianceProbeSize,
        config.irradianceProbeSize,
        config.irradianceProbeSize
    );
    
    uniforms.depthTextureSize = glm::ivec4(
        probesPerRow * config.depthProbeSize,
        probeRows * config.depthProbeSize,
        config.depthProbeSize,
        config.depthProbeSize
    );
    
    uniforms.rayParams = glm::vec4(
        static_cast<float>(config.raysPerProbe),
        config.maxRayDistance,
        config.normalBias,
        config.viewBias
    );
    
    // Generate random rotation matrix for ray directions (Fibonacci sphere + rotation)
    // This ensures uniform distribution of rays across the hemisphere
    static std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    float angle = dist(rng) * 6.28318530718f;
    float c = cos(angle);
    float s = sin(angle);
    
    // Random rotation around Y axis primarily, with slight tilt
    uniforms.randomRotation = glm::mat4(
        c, 0.0f, s, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        -s, 0.0f, c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    
    memcpy(uniformBufferMapped, &uniforms, sizeof(DDGIUniforms));
}

void DDGISystem::update(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (rayTracePipeline == VK_NULL_HANDLE || probeUpdatePipeline == VK_NULL_HANDLE) {
        return; // Pipelines not ready
    }
    
    updateUniforms(frameIndex);
    frameCounter++;
    
    int totalProbes = config.probeCount.x * config.probeCount.y * config.probeCount.z;
    
    // Transition irradiance and depth images to GENERAL for compute writes
    VkImageMemoryBarrier barriers[2] = {};
    
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = irradianceImage;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    barriers[1] = barriers[0];
    barriers[1].image = depthImage;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);
    
    // Phase 1: Ray trace from probes
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rayTracePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, 
                            rayTracePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
    // Dispatch: one workgroup per probe, rays processed in parallel within workgroup
    uint32_t workgroupsX = (totalProbes + 31) / 32;  // 32 probes per workgroup
    vkCmdDispatch(commandBuffer, workgroupsX, 1, 1);
    
    // Memory barrier between phases
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
    
    // Phase 2: Update probe textures
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, probeUpdatePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            probeUpdatePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
    // Dispatch: one workgroup per probe's irradiance texel block
    int probesPerRow = config.probeCount.x * config.probeCount.z;
    int probeRows = config.probeCount.y;
    vkCmdDispatch(commandBuffer, probesPerRow, probeRows, 1);
    
    // Transition images to shader read for composition pass
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);
}

glm::vec3 DDGISystem::getProbeWorldPosition(int probeIndex) const {
    int totalXZ = config.probeCount.x * config.probeCount.z;
    int y = probeIndex / totalXZ;
    int remainder = probeIndex % totalXZ;
    int z = remainder / config.probeCount.x;
    int x = remainder % config.probeCount.x;
    
    return config.gridOrigin + glm::vec3(x, y, z) * config.probeSpacing;
}
