#include "HiZBuffer.h"
#include "ShaderManager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>

HiZBuffer::HiZBuffer(VulkanContext& context, uint32_t width, uint32_t height, VkDescriptorPool descriptorPool)
    : context(context), width(width), height(height), descriptorPool(descriptorPool)
{
    mipLevels = calculateMipLevels(width, height);
    
    createPyramidImage();
    createSampler();
    createDescriptorSetLayout();
    createComputePipeline();
    createDescriptorSets();
    
    std::cout << "Hi-Z Buffer initialized: " << width << "x" << height << ", " << mipLevels << " mip levels" << std::endl;
}

HiZBuffer::~HiZBuffer() {
    destroyResources();
}

uint32_t HiZBuffer::calculateMipLevels(uint32_t w, uint32_t h) {
    uint32_t levels = 1;
    while (w > 1 || h > 1) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        levels++;
    }
    return std::min(levels, 12u); // Cap at 12 levels
}

void HiZBuffer::destroyResources() {
    VkDevice device = context.getDevice();
    vkDeviceWaitIdle(device);
    
    if (computePipeline) vkDestroyPipeline(device, computePipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    
    if (pyramidSampler) vkDestroySampler(device, pyramidSampler, nullptr);
    
    for (auto& view : mipViews) {
        if (view) vkDestroyImageView(device, view, nullptr);
    }
    mipViews.clear();
    
    if (pyramidView) vkDestroyImageView(device, pyramidView, nullptr);
    if (pyramidImage) vkDestroyImage(device, pyramidImage, nullptr);
    if (pyramidMemory) vkFreeMemory(device, pyramidMemory, nullptr);
    
    pyramidImage = VK_NULL_HANDLE;
    pyramidMemory = VK_NULL_HANDLE;
    pyramidView = VK_NULL_HANDLE;
    pyramidSampler = VK_NULL_HANDLE;
    computePipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
}

void HiZBuffer::createPyramidImage() {
    VkDevice device = context.getDevice();
    
    // Create image with mipmap chain
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;  // Single-channel float for depth
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &pyramidImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z pyramid image");
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, pyramidImage, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &pyramidMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Hi-Z pyramid memory");
    }
    
    vkBindImageMemory(device, pyramidImage, pyramidMemory, 0);
    
    // Create view for all mip levels (for sampling in SSR)
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = pyramidImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &pyramidView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z pyramid view");
    }
    
    // Create per-mip views for compute shader writes
    mipViews.resize(mipLevels);
    for (uint32_t i = 0; i < mipLevels; i++) {
        VkImageViewCreateInfo mipViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        mipViewInfo.image = pyramidImage;
        mipViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        mipViewInfo.format = VK_FORMAT_R32_SFLOAT;
        mipViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        mipViewInfo.subresourceRange.baseMipLevel = i;
        mipViewInfo.subresourceRange.levelCount = 1;
        mipViewInfo.subresourceRange.baseArrayLayer = 0;
        mipViewInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device, &mipViewInfo, nullptr, &mipViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Hi-Z mip view " + std::to_string(i));
        }
    }
}

void HiZBuffer::createSampler() {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;  // Use NEAREST for depth
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    
    if (vkCreateSampler(context.getDevice(), &samplerInfo, nullptr, &pyramidSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z sampler");
    }
}

void HiZBuffer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[2] = {};
    
    // Input depth texture (previous mip level)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Output depth image (current mip level)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(context.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z descriptor set layout");
    }
}

void HiZBuffer::createComputePipeline() {
    VkShaderModule shaderModule = Sanic::ShaderManager::loadShader("shaders/depth_downsample.comp");
    
    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);
    
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(context.getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z pipeline layout");
    }
    
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout;
    
    if (vkCreateComputePipelines(context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z compute pipeline");
    }
}

void HiZBuffer::createDescriptorSets() {
    // Allocate descriptor sets for each mip level transition
    descriptorSets.resize(mipLevels - 1);  // We need N-1 transitions for N mip levels
    
    std::vector<VkDescriptorSetLayout> layouts(mipLevels - 1, descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(mipLevels - 1);
    allocInfo.pSetLayouts = layouts.data();
    
    if (vkAllocateDescriptorSets(context.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Hi-Z descriptor sets");
    }
}

void HiZBuffer::generate(VkCommandBuffer cmd, VkImageView depthView, VkSampler depthSampler) {
    // Transition entire pyramid to GENERAL for writing
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pyramidImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    
    uint32_t currentWidth = width;
    uint32_t currentHeight = height;
    
    for (uint32_t mip = 0; mip < mipLevels - 1; mip++) {
        uint32_t nextWidth = std::max(1u, currentWidth / 2);
        uint32_t nextHeight = std::max(1u, currentHeight / 2);
        
        // Update descriptor set for this mip level
        VkDescriptorImageInfo inputInfo{};
        if (mip == 0) {
            // First mip: read from original depth buffer
            inputInfo.sampler = depthSampler;
            inputInfo.imageView = depthView;
            inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        } else {
            // Subsequent mips: read from previous pyramid level
            inputInfo.sampler = pyramidSampler;
            inputInfo.imageView = mipViews[mip - 1];
            inputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
        
        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageView = mipViews[mip];
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        
        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[mip];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &inputInfo;
        
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[mip];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &outputInfo;
        
        vkUpdateDescriptorSets(context.getDevice(), 2, writes, 0, nullptr);
        
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 
                                 0, 1, &descriptorSets[mip], 0, nullptr);
        
        // Push constants
        PushConstants pc{};
        pc.inputWidth = static_cast<int>(currentWidth);
        pc.inputHeight = static_cast<int>(currentHeight);
        pc.outputWidth = static_cast<int>(nextWidth);
        pc.outputHeight = static_cast<int>(nextHeight);
        pc.mipLevel = static_cast<int>(mip);
        
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                           0, sizeof(PushConstants), &pc);
        
        // Dispatch
        uint32_t groupCountX = (nextWidth + 7) / 8;
        uint32_t groupCountY = (nextHeight + 7) / 8;
        vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
        
        // Barrier between mip levels
        if (mip < mipLevels - 2) {
            VkImageMemoryBarrier mipBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            mipBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBarrier.image = pyramidImage;
            mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipBarrier.subresourceRange.baseMipLevel = mip;
            mipBarrier.subresourceRange.levelCount = 1;
            mipBarrier.subresourceRange.baseArrayLayer = 0;
            mipBarrier.subresourceRange.layerCount = 1;
            mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &mipBarrier);
        }
        
        currentWidth = nextWidth;
        currentHeight = nextHeight;
    }
    
    // Final barrier: transition to SHADER_READ_ONLY for SSR sampling
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void HiZBuffer::resize(uint32_t newWidth, uint32_t newHeight) {
    width = newWidth;
    height = newHeight;
    mipLevels = calculateMipLevels(width, height);
    
    // Destroy old resources
    VkDevice device = context.getDevice();
    vkDeviceWaitIdle(device);
    
    for (auto& view : mipViews) {
        if (view) vkDestroyImageView(device, view, nullptr);
    }
    mipViews.clear();
    
    if (pyramidView) vkDestroyImageView(device, pyramidView, nullptr);
    if (pyramidImage) vkDestroyImage(device, pyramidImage, nullptr);
    if (pyramidMemory) vkFreeMemory(device, pyramidMemory, nullptr);
    if (pyramidSampler) vkDestroySampler(device, pyramidSampler, nullptr);
    
    pyramidImage = VK_NULL_HANDLE;
    pyramidMemory = VK_NULL_HANDLE;
    pyramidView = VK_NULL_HANDLE;
    pyramidSampler = VK_NULL_HANDLE;
    
    // Recreate
    createPyramidImage();
    createSampler();
    createDescriptorSets();
    
    std::cout << "Hi-Z Buffer resized: " << width << "x" << height << ", " << mipLevels << " mip levels" << std::endl;
}

VkShaderModule HiZBuffer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(context.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z shader module");
    }
    return shaderModule;
}

std::vector<char> HiZBuffer::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open Hi-Z shader file: " + filename);
    }
    
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}
