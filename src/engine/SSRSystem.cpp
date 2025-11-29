#include "SSRSystem.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

SSRSystem::SSRSystem(VulkanContext& context,
                     uint32_t width, uint32_t height,
                     VkDescriptorPool descriptorPool)
    : context(context),
      width(width), height(height), descriptorPool(descriptorPool)
{
    createReflectionImage();
    createUniformBuffer();
    createDescriptorSetLayout();
    createComputePipeline();
    createDescriptorSet();
    
    std::cout << "SSR System initialized at " << width << "x" << height << std::endl;
}

SSRSystem::~SSRSystem() {
    destroyResources();
}

void SSRSystem::destroyResources() {
    VkDevice device = context.getDevice();
    
    if (computePipeline) vkDestroyPipeline(device, computePipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    
    if (uniformBuffer) {
        vkUnmapMemory(device, uniformMemory);
        vkDestroyBuffer(device, uniformBuffer, nullptr);
        vkFreeMemory(device, uniformMemory, nullptr);
    }
    
    if (reflectionImageView) vkDestroyImageView(device, reflectionImageView, nullptr);
    if (reflectionImage) vkDestroyImage(device, reflectionImage, nullptr);
    if (reflectionMemory) vkFreeMemory(device, reflectionMemory, nullptr);
}

void SSRSystem::createReflectionImage() {
    VkDevice device = context.getDevice();
    
    // Create reflection output image (RGBA16F for HDR)
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &reflectionImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR reflection image");
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, reflectionImage, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &reflectionMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSR reflection image memory");
    }
    
    vkBindImageMemory(device, reflectionImage, reflectionMemory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = reflectionImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &reflectionImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR reflection image view");
    }
    
    // Image transition will be done lazily in first update()
}

void SSRSystem::createUniformBuffer() {
    VkDeviceSize bufferSize = sizeof(SSRUniforms);
    
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(context.getDevice(), &bufferInfo, nullptr, &uniformBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR uniform buffer");
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(context.getDevice(), uniformBuffer, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &uniformMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSR uniform buffer memory");
    }
    
    vkBindBufferMemory(context.getDevice(), uniformBuffer, uniformMemory, 0);
    vkMapMemory(context.getDevice(), uniformMemory, 0, bufferSize, 0, &uniformMapped);
}

void SSRSystem::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[9] = {};
    
    // 0: gPosition sampler
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 1: gNormal sampler
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 2: gAlbedo sampler
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 3: gPBR sampler
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 4: depthBuffer sampler
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 5: sceneColor sampler
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 6: reflectionOutput storage image
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 7: TLAS
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 8: Uniforms
    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 9;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(context.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR descriptor set layout");
    }
}

void SSRSystem::createComputePipeline() {
    auto shaderCode = readFile("shaders/ssr.comp.spv");
    VkShaderModule shaderModule = createShaderModule(shaderCode);
    
    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkCreatePipelineLayout(context.getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR pipeline layout");
    }
    
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout;
    
    if (vkCreateComputePipelines(context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR compute pipeline");
    }
    
    vkDestroyShaderModule(context.getDevice(), shaderModule, nullptr);
}

void SSRSystem::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkAllocateDescriptorSets(context.getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSR descriptor set");
    }
}

void SSRSystem::update(VkCommandBuffer cmd,
                       const glm::mat4& view,
                       const glm::mat4& projection,
                       const glm::vec3& cameraPos,
                       VkImageView positionView,
                       VkImageView normalView,
                       VkImageView albedoView,
                       VkImageView pbrView,
                       VkImageView depthView,
                       VkImageView sceneColorView,
                       VkSampler sampler)
{
    // On first call, transition the reflection image to GENERAL layout
    if (needsImageTransition) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = reflectionImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        needsImageTransition = false;
    }
    
    // Update uniforms
    SSRUniforms uniforms{};
    uniforms.view = view;
    uniforms.projection = projection;
    uniforms.invView = glm::inverse(view);
    uniforms.invProjection = glm::inverse(projection);
    uniforms.cameraPos = glm::vec4(cameraPos, 1.0f);
    uniforms.screenSize = glm::vec2(width, height);
    uniforms.maxDistance = config.maxDistance;
    uniforms.thickness = config.thickness;
    uniforms.maxSteps = config.maxSteps;
    uniforms.roughnessThreshold = config.roughnessThreshold;
    uniforms.rtFallbackEnabled = config.rtFallbackEnabled ? 1.0f : 0.0f;
    
    memcpy(uniformMapped, &uniforms, sizeof(uniforms));
    
    // Update descriptor set with current frame's resources
    VkDescriptorImageInfo imageInfos[6] = {};
    imageInfos[0] = {sampler, positionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {sampler, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {sampler, albedoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[3] = {sampler, pbrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[4] = {sampler, depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    imageInfos[5] = {sampler, sceneColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    
    VkDescriptorImageInfo outputInfo = {VK_NULL_HANDLE, reflectionImageView, VK_IMAGE_LAYOUT_GENERAL};
    
    VkDescriptorBufferInfo bufferInfo = {uniformBuffer, 0, sizeof(SSRUniforms)};
    
    std::vector<VkWriteDescriptorSet> writes;
    
    // Samplers (0-5)
    for (int i = 0; i < 6; i++) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = i;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos[i];
        writes.push_back(write);
    }
    
    // Output image (6)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 6;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &outputInfo;
        writes.push_back(write);
    }
    
    // TLAS (7)
    VkWriteDescriptorSetAccelerationStructureKHR asWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    if (tlas != VK_NULL_HANDLE) {
        asWrite.accelerationStructureCount = 1;
        asWrite.pAccelerationStructures = &tlas;
        
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.pNext = &asWrite;
        write.dstSet = descriptorSet;
        write.dstBinding = 7;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        writes.push_back(write);
    }
    
    // Uniforms (8)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 8;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        writes.push_back(write);
    }
    
    vkUpdateDescriptorSets(context.getDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
    
    // Dispatch compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
    uint32_t groupCountX = (width + 7) / 8;
    uint32_t groupCountY = (height + 7) / 8;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
    
    // Barrier for reflection image to be read by composition pass
    // Note: We leave it in GENERAL layout since we need it for next frame's compute write
    // The shader can read from GENERAL layout images
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = reflectionImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void SSRSystem::resize(uint32_t newWidth, uint32_t newHeight) {
    width = newWidth;
    height = newHeight;
    
    VkDevice device = context.getDevice();
    vkDeviceWaitIdle(device);
    
    // Destroy old image
    if (reflectionImageView) vkDestroyImageView(device, reflectionImageView, nullptr);
    if (reflectionImage) vkDestroyImage(device, reflectionImage, nullptr);
    if (reflectionMemory) vkFreeMemory(device, reflectionMemory, nullptr);
    
    // Recreate
    createReflectionImage();
}

VkShaderModule SSRSystem::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(context.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR shader module");
    }
    return shaderModule;
}

std::vector<char> SSRSystem::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SSR shader file: " + filename);
    }
    
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}
