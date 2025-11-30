#include "HZBPipeline.h"
#include "VulkanContext.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <array>

// ============================================================================
// Helper functions
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

static uint32_t calculateMipLevels(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

// ============================================================================
// HZBPipeline Implementation
// ============================================================================

HZBPipeline::HZBPipeline(VulkanContext& ctx, const Config& cfg)
    : context(ctx)
    , config(cfg)
{
    createPipeline();
    createSampler();
}

HZBPipeline::~HZBPipeline() {
    destroyResources();
}

void HZBPipeline::destroyResources() {
    VkDevice device = context.getDevice();
    
    vkDeviceWaitIdle(device);
    
    // Destroy mip views
    for (auto view : hzbMipViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    hzbMipViews.clear();
    
    if (hzbImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, hzbImageView, nullptr);
        hzbImageView = VK_NULL_HANDLE;
    }
    
    if (hzbImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, hzbImage, nullptr);
        hzbImage = VK_NULL_HANDLE;
    }
    
    if (hzbMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, hzbMemory, nullptr);
        hzbMemory = VK_NULL_HANDLE;
    }
    
    if (hzbSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, hzbSampler, nullptr);
        hzbSampler = VK_NULL_HANDLE;
    }
    
    // Destroy descriptor sets
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    descriptorSets.clear();
    
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    if (hzbPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, hzbPipelineLayout, nullptr);
        hzbPipelineLayout = VK_NULL_HANDLE;
    }
    
    if (hzbGeneratePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, hzbGeneratePipeline, nullptr);
        hzbGeneratePipeline = VK_NULL_HANDLE;
    }
}

void HZBPipeline::createPipeline() {
    VkDevice device = context.getDevice();
    
    // Load HZB generation shader
    auto shaderCode = readShaderFile("shaders/hzb_generate.spv");
    VkShaderModule shaderModule = createShaderModule(device, shaderCode);
    
    // Descriptor set layout
    // Binding 0: Source texture (sampler2D)
    // Binding 1: Destination image (image2D, writeonly)
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    
    // Source texture
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Destination image
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB descriptor set layout");
    }
    
    // Push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(HZBPushConstants);
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &hzbPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB pipeline layout");
    }
    
    // Compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = hzbPipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &hzbGeneratePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB compute pipeline");
    }
    
    vkDestroyShaderModule(device, shaderModule, nullptr);
}

void HZBPipeline::createSampler() {
    VkDevice device = context.getDevice();
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &hzbSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB sampler");
    }
}

void HZBPipeline::createHZBImage(uint32_t width, uint32_t height) {
    VkDevice device = context.getDevice();
    
    // Destroy existing resources
    for (auto view : hzbMipViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    hzbMipViews.clear();
    
    if (hzbImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, hzbImageView, nullptr);
        hzbImageView = VK_NULL_HANDLE;
    }
    
    if (hzbImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, hzbImage, nullptr);
        hzbImage = VK_NULL_HANDLE;
    }
    
    if (hzbMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, hzbMemory, nullptr);
        hzbMemory = VK_NULL_HANDLE;
    }
    
    // Calculate mip levels
    hzbMipLevels = calculateMipLevels(width, height);
    currentWidth = width;
    currentHeight = height;
    
    // Create HZB image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = config.format;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = hzbMipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &hzbImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB image");
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, hzbImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, 
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &hzbMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate HZB memory");
    }
    
    vkBindImageMemory(device, hzbImage, hzbMemory, 0);
    
    // Create image view for all mips
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = hzbImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = config.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = hzbMipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &hzbImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB image view");
    }
    
    // Create per-mip views
    hzbMipViews.resize(hzbMipLevels);
    for (uint32_t i = 0; i < hzbMipLevels; i++) {
        viewInfo.subresourceRange.baseMipLevel = i;
        viewInfo.subresourceRange.levelCount = 1;
        
        if (vkCreateImageView(device, &viewInfo, nullptr, &hzbMipViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create HZB mip view");
        }
    }
    
    // Recreate descriptor pool for new mip count
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    descriptorSets.clear();
    
    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = hzbMipLevels;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = hzbMipLevels;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = hzbMipLevels;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB descriptor pool");
    }
    
    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(hzbMipLevels, descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool;
    allocateInfo.descriptorSetCount = hzbMipLevels;
    allocateInfo.pSetLayouts = layouts.data();
    
    descriptorSets.resize(hzbMipLevels);
    if (vkAllocateDescriptorSets(device, &allocateInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate HZB descriptor sets");
    }
}

void HZBPipeline::updateDescriptorSets(VkImageView depthView) {
    VkDevice device = context.getDevice();
    
    for (uint32_t i = 0; i < hzbMipLevels; i++) {
        std::array<VkWriteDescriptorSet, 2> writes{};
        
        // Source image (previous mip or depth buffer)
        VkDescriptorImageInfo srcImageInfo{};
        srcImageInfo.sampler = hzbSampler;
        srcImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        
        if (i == 0) {
            srcImageInfo.imageView = depthView;  // First pass reads depth
        } else {
            srcImageInfo.imageView = hzbMipViews[i - 1];  // Read previous mip
        }
        
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &srcImageInfo;
        
        // Destination image (current mip)
        VkDescriptorImageInfo dstImageInfo{};
        dstImageInfo.imageView = hzbMipViews[i];
        dstImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &dstImageInfo;
        
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void HZBPipeline::generateHZB(VkCommandBuffer cmd, VkImage depthImage, VkImageView depthView,
                               uint32_t width, uint32_t height) {
    // Resize HZB if needed
    if (width != currentWidth || height != currentHeight) {
        createHZBImage(width, height);
    }
    
    // Update descriptor sets
    updateDescriptorSets(depthView);
    
    // Transition HZB image to general layout
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = hzbImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = hzbMipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
    
    // Transition depth buffer for reading
    VkImageMemoryBarrier depthBarrier{};
    depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.image = depthImage;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBarrier.subresourceRange.baseMipLevel = 0;
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.baseArrayLayer = 0;
    depthBarrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &depthBarrier);
    
    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, hzbGeneratePipeline);
    
    // Generate each mip level
    uint32_t srcWidth = width;
    uint32_t srcHeight = height;
    
    for (uint32_t i = 0; i < hzbMipLevels; i++) {
        uint32_t dstWidth = std::max(srcWidth / 2, 1u);
        uint32_t dstHeight = std::max(srcHeight / 2, 1u);
        
        // Bind descriptor set
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, hzbPipelineLayout,
                                0, 1, &descriptorSets[i], 0, nullptr);
        
        // Push constants
        HZBPushConstants pushConstants{};
        pushConstants.srcSize = glm::vec2(static_cast<float>(srcWidth), static_cast<float>(srcHeight));
        pushConstants.dstSize = glm::vec2(static_cast<float>(dstWidth), static_cast<float>(dstHeight));
        pushConstants.srcMipLevel = (i == 0) ? 0 : static_cast<int32_t>(i - 1);
        pushConstants.isFirstPass = (i == 0) ? 1 : 0;
        
        vkCmdPushConstants(cmd, hzbPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(HZBPushConstants), &pushConstants);
        
        // Dispatch
        uint32_t groupsX = (dstWidth + 7) / 8;
        uint32_t groupsY = (dstHeight + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
        
        // Barrier between mip levels
        if (i < hzbMipLevels - 1) {
            VkImageMemoryBarrier mipBarrier{};
            mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            mipBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBarrier.image = hzbImage;
            mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipBarrier.subresourceRange.baseMipLevel = i;
            mipBarrier.subresourceRange.levelCount = 1;
            mipBarrier.subresourceRange.baseArrayLayer = 0;
            mipBarrier.subresourceRange.layerCount = 1;
            
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &mipBarrier);
        }
        
        srcWidth = dstWidth;
        srcHeight = dstHeight;
    }
    
    // Final transition to shader read
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

glm::uvec2 HZBPipeline::getMipSize(uint32_t mipLevel) const {
    uint32_t w = std::max(currentWidth >> mipLevel, 1u);
    uint32_t h = std::max(currentHeight >> mipLevel, 1u);
    return glm::uvec2(w, h);
}
