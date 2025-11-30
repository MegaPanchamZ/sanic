#include "SSRSystem.h"
#include "ShaderManager.h"
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
    createHitUVImage();
    createUniformBuffer();
    createDescriptorSetLayout();
    createComputePipeline();
    createDescriptorSet();
    
    std::cout << "SSR System initialized at " << width << "x" << height << " with Hi-Z support" << std::endl;
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
    
    if (hitUVImageView) vkDestroyImageView(device, hitUVImageView, nullptr);
    if (hitUVImage) vkDestroyImage(device, hitUVImage, nullptr);
    if (hitUVMemory) vkFreeMemory(device, hitUVMemory, nullptr);
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
}

void SSRSystem::createHitUVImage() {
    VkDevice device = context.getDevice();
    
    // Create hit UV output image (RG16F for UV coordinates)
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16_SFLOAT;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &hitUVImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR hit UV image");
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, hitUVImage, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &hitUVMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSR hit UV image memory");
    }
    
    vkBindImageMemory(device, hitUVImage, hitUVMemory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = hitUVImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &hitUVImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR hit UV image view");
    }
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
    // Updated bindings for Hi-Z SSR:
    // 0-5: G-Buffer samplers (position, normal, albedo, pbr, depth, sceneColor)
    // 6: reflectionOutput (storage image)
    // 7: TLAS
    // 8: Uniforms
    // 9: hizBuffer (Hi-Z pyramid sampler)
    // 10: velocityBuffer (motion vectors sampler)
    // 11: hitUVOutput (storage image)
    
    VkDescriptorSetLayoutBinding bindings[12] = {};
    
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
    
    // 9: hizBuffer sampler (Hi-Z depth pyramid)
    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 10: velocityBuffer sampler (motion vectors)
    bindings[10].binding = 10;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // 11: hitUVOutput storage image
    bindings[11].binding = 11;
    bindings[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[11].descriptorCount = 1;
    bindings[11].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 12;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(context.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSR descriptor set layout");
    }
}

void SSRSystem::createComputePipeline() {
    VkShaderModule shaderModule = ShaderManager::loadShader("shaders/ssr.comp");
    
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

// New update function with Hi-Z and velocity buffer support
void SSRSystem::update(VkCommandBuffer cmd,
                       const glm::mat4& view,
                       const glm::mat4& projection,
                       const glm::mat4& prevViewProjMatrix,
                       const glm::vec3& cameraPos,
                       const glm::vec2& jitter,
                       VkImageView positionView,
                       VkImageView normalView,
                       VkImageView albedoView,
                       VkImageView pbrView,
                       VkImageView depthView,
                       VkImageView sceneColorView,
                       VkImageView hizView,
                       VkImageView velocityView,
                       VkSampler sampler,
                       VkSampler hizSampler,
                       uint32_t hizMipLevels)
{
    // On first call, transition images to GENERAL layout
    if (needsImageTransition) {
        VkImageMemoryBarrier barriers[2] = {};
        
        // Reflection image
        barriers[0] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = reflectionImage;
        barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.baseMipLevel = 0;
        barriers[0].subresourceRange.levelCount = 1;
        barriers[0].subresourceRange.baseArrayLayer = 0;
        barriers[0].subresourceRange.layerCount = 1;
        barriers[0].srcAccessMask = 0;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        
        // Hit UV image
        barriers[1] = barriers[0];
        barriers[1].image = hitUVImage;
        
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 2, barriers);
        
        needsImageTransition = false;
    }
    
    // Update uniforms with new Hi-Z data
    SSRUniforms uniforms{};
    uniforms.view = view;
    uniforms.projection = projection;
    uniforms.invView = glm::inverse(view);
    uniforms.invProjection = glm::inverse(projection);
    uniforms.prevViewProj = prevViewProjMatrix;
    uniforms.cameraPos = glm::vec4(cameraPos, 1.0f);
    uniforms.screenSize = glm::vec2(width, height);
    uniforms.maxDistance = config.maxDistance;
    uniforms.thickness = config.thickness;
    uniforms.maxSteps = config.maxSteps;
    uniforms.roughnessThreshold = config.roughnessThreshold;
    uniforms.rtFallbackEnabled = config.rtFallbackEnabled ? 1.0f : 0.0f;
    uniforms.hizMipLevels = static_cast<float>(hizMipLevels);
    uniforms.jitter = jitter;
    uniforms.temporalWeight = config.temporalWeight;
    
    memcpy(uniformMapped, &uniforms, sizeof(uniforms));
    
    // Store current view-projection for next frame
    prevViewProj = projection * view;
    
    // Update descriptor set with current frame's resources
    VkDescriptorImageInfo imageInfos[8] = {};
    imageInfos[0] = {sampler, positionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {sampler, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {sampler, albedoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[3] = {sampler, pbrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[4] = {sampler, depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    imageInfos[5] = {sampler, sceneColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[6] = {hizSampler, hizView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[7] = {sampler, velocityView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    
    VkDescriptorImageInfo reflectionOutputInfo = {VK_NULL_HANDLE, reflectionImageView, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo hitUVOutputInfo = {VK_NULL_HANDLE, hitUVImageView, VK_IMAGE_LAYOUT_GENERAL};
    
    VkDescriptorBufferInfo bufferInfo = {uniformBuffer, 0, sizeof(SSRUniforms)};
    
    std::vector<VkWriteDescriptorSet> writes;
    
    // Samplers (0-5 for G-Buffer)
    for (int i = 0; i < 6; i++) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = i;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos[i];
        writes.push_back(write);
    }
    
    // Reflection output image (6)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 6;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &reflectionOutputInfo;
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
    
    // Hi-Z buffer (9)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 9;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos[6];
        writes.push_back(write);
    }
    
    // Velocity buffer (10)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 10;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos[7];
        writes.push_back(write);
    }
    
    // Hit UV output (11)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 11;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &hitUVOutputInfo;
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

// Legacy update for backwards compatibility (uses identity for missing params)
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
    // Legacy path - transition images if needed
    if (needsImageTransition) {
        VkImageMemoryBarrier barriers[2] = {};
        
        barriers[0] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = reflectionImage;
        barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.baseMipLevel = 0;
        barriers[0].subresourceRange.levelCount = 1;
        barriers[0].subresourceRange.baseArrayLayer = 0;
        barriers[0].subresourceRange.layerCount = 1;
        barriers[0].srcAccessMask = 0;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        
        barriers[1] = barriers[0];
        barriers[1].image = hitUVImage;
        
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 2, barriers);
        
        needsImageTransition = false;
    }
    
    // Update uniforms (legacy - no Hi-Z or velocity)
    SSRUniforms uniforms{};
    uniforms.view = view;
    uniforms.projection = projection;
    uniforms.invView = glm::inverse(view);
    uniforms.invProjection = glm::inverse(projection);
    uniforms.prevViewProj = prevViewProj;
    uniforms.cameraPos = glm::vec4(cameraPos, 1.0f);
    uniforms.screenSize = glm::vec2(width, height);
    uniforms.maxDistance = config.maxDistance;
    uniforms.thickness = config.thickness;
    uniforms.maxSteps = config.maxSteps;
    uniforms.roughnessThreshold = config.roughnessThreshold;
    uniforms.rtFallbackEnabled = config.rtFallbackEnabled ? 1.0f : 0.0f;
    uniforms.hizMipLevels = 1.0f;  // Fallback - no Hi-Z
    uniforms.jitter = glm::vec2(0.0f);
    uniforms.temporalWeight = config.temporalWeight;
    
    memcpy(uniformMapped, &uniforms, sizeof(uniforms));
    
    prevViewProj = projection * view;
    
    // Update descriptor set (legacy bindings only)
    VkDescriptorImageInfo imageInfos[6] = {};
    imageInfos[0] = {sampler, positionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {sampler, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {sampler, albedoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[3] = {sampler, pbrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[4] = {sampler, depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    imageInfos[5] = {sampler, sceneColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    
    VkDescriptorImageInfo reflectionOutputInfo = {VK_NULL_HANDLE, reflectionImageView, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo hitUVOutputInfo = {VK_NULL_HANDLE, hitUVImageView, VK_IMAGE_LAYOUT_GENERAL};
    
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
    
    // Reflection output image (6)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 6;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &reflectionOutputInfo;
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
    
    // Use depth buffer as Hi-Z fallback (9)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 9;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos[4];  // Use depth buffer as Hi-Z fallback
        writes.push_back(write);
    }
    
    // Use position buffer as velocity fallback (10)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 10;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos[0];  // Use position buffer as velocity fallback
        writes.push_back(write);
    }
    
    // Hit UV output (11)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 11;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &hitUVOutputInfo;
        writes.push_back(write);
    }
    
    vkUpdateDescriptorSets(context.getDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
    
    // Dispatch compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
    uint32_t groupCountX = (width + 7) / 8;
    uint32_t groupCountY = (height + 7) / 8;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
    
    // Barrier
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
    
    // Destroy old images
    if (reflectionImageView) vkDestroyImageView(device, reflectionImageView, nullptr);
    if (reflectionImage) vkDestroyImage(device, reflectionImage, nullptr);
    if (reflectionMemory) vkFreeMemory(device, reflectionMemory, nullptr);
    
    if (hitUVImageView) vkDestroyImageView(device, hitUVImageView, nullptr);
    if (hitUVImage) vkDestroyImage(device, hitUVImage, nullptr);
    if (hitUVMemory) vkFreeMemory(device, hitUVMemory, nullptr);
    
    // Recreate
    createReflectionImage();
    createHitUVImage();
    needsImageTransition = true;
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
