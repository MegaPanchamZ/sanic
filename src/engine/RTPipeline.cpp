#include "RTPipeline.h"
#include "ShaderManager.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstring>

RTPipeline::RTPipeline(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue)
    : device(device), physicalDevice(physicalDevice), commandPool(commandPool), queue(queue) {
    loadRayTracingFunctions();
}

RTPipeline::~RTPipeline() {
    if (sbtBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, sbtBuffer, nullptr);
    }
    if (sbtBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, sbtBufferMemory, nullptr);
    }
    if (rtPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, rtPipeline, nullptr);
    }
    if (rtPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, rtPipelineLayout, nullptr);
    }
}

void RTPipeline::loadRayTracingFunctions() {
    vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR");
    vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR");
    vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR");
    vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddress");

    if (!vkCreateRayTracingPipelinesKHR || !vkGetRayTracingShaderGroupHandlesKHR || 
        !vkCmdTraceRaysKHR || !vkGetBufferDeviceAddressKHR) {
        throw std::runtime_error("Failed to load Ray Tracing function pointers!");
    }
}

void RTPipeline::createPipeline(VkDescriptorSetLayout descriptorSetLayout) {
    // Load shaders using ShaderManager for runtime compilation
    VkShaderModule raygenModule = Sanic::ShaderManager::loadShader("shaders/simple.rgen", Sanic::ShaderStage::RayGen);
    VkShaderModule missModule = Sanic::ShaderManager::loadShader("shaders/simple.rmiss", Sanic::ShaderStage::Miss);
    VkShaderModule shadowMissModule = Sanic::ShaderManager::loadShader("shaders/shadow.rmiss", Sanic::ShaderStage::Miss);
    VkShaderModule chitModule = Sanic::ShaderManager::loadShader("shaders/simple.rchit", Sanic::ShaderStage::ClosestHit);
    
    if (!raygenModule || !missModule || !shadowMissModule || !chitModule) {
        throw std::runtime_error("failed to compile ray tracing shaders!");
    }

    // Shader stages
    // Index 0: raygen, 1: miss (primary), 2: miss (shadow), 3: closest hit
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    VkPipelineShaderStageCreateInfo raygenStage{};
    raygenStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    raygenStage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    raygenStage.module = raygenModule;
    raygenStage.pName = "main";
    shaderStages.push_back(raygenStage);

    VkPipelineShaderStageCreateInfo missStage{};
    missStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    missStage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    missStage.module = missModule;
    missStage.pName = "main";
    shaderStages.push_back(missStage);

    VkPipelineShaderStageCreateInfo shadowMissStage{};
    shadowMissStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadowMissStage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    shadowMissStage.module = shadowMissModule;
    shadowMissStage.pName = "main";
    shaderStages.push_back(shadowMissStage);

    VkPipelineShaderStageCreateInfo chitStage{};
    chitStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    chitStage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    chitStage.module = chitModule;
    chitStage.pName = "main";
    shaderStages.push_back(chitStage);

    // Shader groups (order: raygen, miss0, miss1, hit)
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;

    // Group 0: Raygen
    VkRayTracingShaderGroupCreateInfoKHR raygenGroup{};
    raygenGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    raygenGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    raygenGroup.generalShader = 0; // raygenStage index
    raygenGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    raygenGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    raygenGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
    shaderGroups.push_back(raygenGroup);

    // Group 1: Primary miss (sky)
    VkRayTracingShaderGroupCreateInfoKHR missGroup{};
    missGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    missGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    missGroup.generalShader = 1; // missStage index
    missGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    missGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    missGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
    shaderGroups.push_back(missGroup);

    // Group 2: Shadow miss
    VkRayTracingShaderGroupCreateInfoKHR shadowMissGroup{};
    shadowMissGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shadowMissGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shadowMissGroup.generalShader = 2; // shadowMissStage index
    shadowMissGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    shadowMissGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    shadowMissGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
    shaderGroups.push_back(shadowMissGroup);

    // Group 3: Closest hit
    VkRayTracingShaderGroupCreateInfoKHR hitGroup{};
    hitGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    hitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    hitGroup.generalShader = VK_SHADER_UNUSED_KHR;
    hitGroup.closestHitShader = 3; // chitStage index
    hitGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    hitGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
    shaderGroups.push_back(hitGroup);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &rtPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create RT pipeline layout!");
    }

    // Ray tracing pipeline - depth 2 for shadow rays from closest hit
    VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
    pipelineInfo.pGroups = shaderGroups.data();
    pipelineInfo.maxPipelineRayRecursionDepth = 2;  // Primary + shadow rays
    pipelineInfo.layout = rtPipelineLayout;

    if (vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rtPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create RT pipeline!");
    }

    // Cleanup shader modules
    vkDestroyShaderModule(device, raygenModule, nullptr);
    vkDestroyShaderModule(device, missModule, nullptr);
    vkDestroyShaderModule(device, shadowMissModule, nullptr);
    vkDestroyShaderModule(device, chitModule, nullptr);

    // Create SBT
    createShaderBindingTable();

    std::cout << "Ray Tracing Pipeline created successfully!" << std::endl;
}

void RTPipeline::createShaderBindingTable() {
    // Query properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{};
    rtProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    
    VkPhysicalDeviceProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &rtProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &props);

    uint32_t handleSize = rtProperties.shaderGroupHandleSize;
    uint32_t handleAlignment = rtProperties.shaderGroupHandleAlignment;
    uint32_t baseAlignment = rtProperties.shaderGroupBaseAlignment;

    // Calculate aligned sizes
    uint32_t handleSizeAligned = (handleSize + (handleAlignment - 1)) & ~(handleAlignment - 1);

    // Get shader group handles
    // Groups: 0=raygen, 1=primary miss, 2=shadow miss, 3=hit
    uint32_t groupCount = 4;
    uint32_t sbtSize = groupCount * handleSizeAligned;
    
    std::vector<uint8_t> handleData(sbtSize);
    if (vkGetRayTracingShaderGroupHandlesKHR(device, rtPipeline, 0, groupCount, sbtSize, handleData.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to get shader group handles!");
    }

    // Create SBT buffer
    VkDeviceSize bufferSize = sbtSize;
    createBuffer(bufferSize, 
                 VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 sbtBuffer, sbtBufferMemory);

    // Map and copy handles
    void* data;
    vkMapMemory(device, sbtBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, handleData.data(), sbtSize);
    vkUnmapMemory(device, sbtBufferMemory);

    VkDeviceAddress sbtAddress = getBufferDeviceAddress(sbtBuffer);

    // Setup regions
    // Raygen: 1 shader at offset 0
    raygenRegion.deviceAddress = sbtAddress;
    raygenRegion.stride = handleSizeAligned;
    raygenRegion.size = handleSizeAligned;

    // Miss: 2 shaders (primary + shadow) starting at offset 1
    missRegion.deviceAddress = sbtAddress + handleSizeAligned;
    missRegion.stride = handleSizeAligned;
    missRegion.size = 2 * handleSizeAligned;  // 2 miss shaders

    // Hit: 1 shader at offset 3
    hitRegion.deviceAddress = sbtAddress + 3 * handleSizeAligned;
    hitRegion.stride = handleSizeAligned;
    hitRegion.size = handleSizeAligned;

    callableRegion = {}; // Not used
}

void RTPipeline::trace(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    vkCmdTraceRaysKHR(commandBuffer, &raygenRegion, &missRegion, &hitRegion, &callableRegion, width, height, 1);
}

// Helper implementations
uint64_t RTPipeline::getBufferDeviceAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return vkGetBufferDeviceAddressKHR(device, &info);
}

void RTPipeline::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    
    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocInfo.pNext = &flagsInfo;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

uint32_t RTPipeline::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

VkShaderModule RTPipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

std::vector<char> RTPipeline::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
}
