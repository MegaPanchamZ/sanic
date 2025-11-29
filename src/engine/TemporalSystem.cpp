/**
 * TemporalSystem.cpp
 * 
 * Implementation of temporal anti-aliasing and motion vector generation.
 */

#include "TemporalSystem.h"
#include "VulkanContext.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

// Push constants for motion vector generation
struct MotionVectorPushConstants {
    glm::mat4 viewProj;
    glm::mat4 prevViewProj;
    glm::mat4 invViewProj;
    
    uint64_t visibilityAddr;
    uint64_t vertexAddr;
    uint64_t indexAddr;
    uint64_t clusterAddr;
    uint64_t instanceAddr;
    
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t vertexStride;
    uint32_t pad;
};

// Push constants for TAA
struct TAAPushConstants {
    glm::vec2 screenSize;
    glm::vec2 invScreenSize;
    float feedbackMin;
    float feedbackMax;
    float motionScale;
    float jitterX;
    float jitterY;
    float sharpness;
    uint32_t frameIndex;
    uint32_t flags;
};

TemporalSystem::~TemporalSystem() {
    cleanup();
}

bool TemporalSystem::initialize(VulkanContext* context,
                                uint32_t width, uint32_t height,
                                const TAAConfig& config) {
    if (initialized_) {
        return true;
    }
    
    context_ = context;
    config_ = config;
    width_ = width;
    height_ = height;
    frameIndex_ = 0;
    
    if (!createImages()) {
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

void TemporalSystem::cleanup() {
    if (!context_) return;
    
    VkDevice device = context_->getDevice();
    
    // Destroy pipelines
    if (motionVectorPipeline_) vkDestroyPipeline(device, motionVectorPipeline_, nullptr);
    if (motionVectorLayout_) vkDestroyPipelineLayout(device, motionVectorLayout_, nullptr);
    if (taaPipeline_) vkDestroyPipeline(device, taaPipeline_, nullptr);
    if (taaLayout_) vkDestroyPipelineLayout(device, taaLayout_, nullptr);
    
    // Destroy descriptor resources
    if (motionVectorDescPool_) vkDestroyDescriptorPool(device, motionVectorDescPool_, nullptr);
    if (motionVectorDescLayout_) vkDestroyDescriptorSetLayout(device, motionVectorDescLayout_, nullptr);
    if (taaDescPool_) vkDestroyDescriptorPool(device, taaDescPool_, nullptr);
    if (taaDescLayout0_) vkDestroyDescriptorSetLayout(device, taaDescLayout0_, nullptr);
    if (taaDescLayout1_) vkDestroyDescriptorSetLayout(device, taaDescLayout1_, nullptr);
    
    // Destroy samplers
    if (historySampler_) vkDestroySampler(device, historySampler_, nullptr);
    if (pointSampler_) vkDestroySampler(device, pointSampler_, nullptr);
    
    // Destroy motion vectors image
    if (motionVectorsView_) vkDestroyImageView(device, motionVectorsView_, nullptr);
    if (motionVectorsImage_) vkDestroyImage(device, motionVectorsImage_, nullptr);
    if (motionVectorsMemory_) vkFreeMemory(device, motionVectorsMemory_, nullptr);
    
    // Destroy history frames
    for (auto& frame : historyFrames_) {
        if (frame.colorView) vkDestroyImageView(device, frame.colorView, nullptr);
        if (frame.colorImage) vkDestroyImage(device, frame.colorImage, nullptr);
        if (frame.colorMemory) vkFreeMemory(device, frame.colorMemory, nullptr);
    }
    
    initialized_ = false;
}

bool TemporalSystem::resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) {
        return true;
    }
    
    cleanup();
    return initialize(context_, width, height, config_);
}

void TemporalSystem::setConfig(const TAAConfig& config) {
    config_ = config;
}

bool TemporalSystem::createImages() {
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
    
    // Helper to create 2D image
    auto createImage = [&](VkFormat format, VkImageUsageFlags usage,
                          VkImage& image, VkImageView& view, VkDeviceMemory& memory) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {width_, height_, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            return false;
        }
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, image, &memReqs);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, 
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            return false;
        }
        
        vkBindImageMemory(device, image, memory, 0);
        
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
            return false;
        }
        
        return true;
    };
    
    // Create motion vectors image (RG16F for 2D motion)
    if (!createImage(VK_FORMAT_R16G16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    motionVectorsImage_, motionVectorsView_, motionVectorsMemory_)) {
        return false;
    }
    
    // Create history frames (RGBA16F for HDR color)
    for (auto& frame : historyFrames_) {
        if (!createImage(VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        frame.colorImage, frame.colorView, frame.colorMemory)) {
            return false;
        }
    }
    
    // Create samplers
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &historySampler_) != VK_SUCCESS) {
        return false;
    }
    
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &pointSampler_) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

bool TemporalSystem::createDescriptorSets() {
    VkDevice device = context_->getDevice();
    
    // Motion vector descriptor layout
    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &motionVectorDescLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};
        
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &motionVectorDescPool_) != VK_SUCCESS) {
            return false;
        }
        
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = motionVectorDescPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &motionVectorDescLayout_;
        
        if (vkAllocateDescriptorSets(device, &allocInfo, &motionVectorDescSet_) != VK_SUCCESS) {
            return false;
        }
    }
    
    // TAA descriptor layouts
    {
        // Set 0: Storage images (current, motion, history, depth, output)
        VkDescriptorSetLayoutBinding bindings0[5] = {};
        for (int i = 0; i < 5; i++) {
            bindings0[i].binding = i;
            bindings0[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings0[i].descriptorCount = 1;
            bindings0[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        
        VkDescriptorSetLayoutCreateInfo layoutInfo0{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo0.bindingCount = 5;
        layoutInfo0.pBindings = bindings0;
        
        if (vkCreateDescriptorSetLayout(device, &layoutInfo0, nullptr, &taaDescLayout0_) != VK_SUCCESS) {
            return false;
        }
        
        // Set 1: History sampler
        VkDescriptorSetLayoutBinding binding1{};
        binding1.binding = 0;
        binding1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding1.descriptorCount = 1;
        binding1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo1.bindingCount = 1;
        layoutInfo1.pBindings = &binding1;
        
        if (vkCreateDescriptorSetLayout(device, &layoutInfo1, nullptr, &taaDescLayout1_) != VK_SUCCESS) {
            return false;
        }
        
        // Create pool
        VkDescriptorPoolSize poolSizes[2] = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[0].descriptorCount = 5;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 1;
        
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &taaDescPool_) != VK_SUCCESS) {
            return false;
        }
        
        // Allocate sets
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = taaDescPool_;
        allocInfo.descriptorSetCount = 1;
        
        allocInfo.pSetLayouts = &taaDescLayout0_;
        if (vkAllocateDescriptorSets(device, &allocInfo, &taaDescSet0_) != VK_SUCCESS) {
            return false;
        }
        
        allocInfo.pSetLayouts = &taaDescLayout1_;
        if (vkAllocateDescriptorSets(device, &allocInfo, &taaDescSet1_) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

bool TemporalSystem::loadShader(const std::string& path, VkShaderModule& outModule) {
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

bool TemporalSystem::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Motion vector pipeline
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(MotionVectorPushConstants);
        
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &motionVectorDescLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &motionVectorLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkShaderModule shaderModule;
        if (!loadShader("shaders/motion_vectors.comp.spv", shaderModule)) {
            return false;
        }
        
        VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = motionVectorLayout_;
        
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &motionVectorPipeline_);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    // TAA pipeline
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TAAPushConstants);
        
        VkDescriptorSetLayout layouts[2] = {taaDescLayout0_, taaDescLayout1_};
        
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = layouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &taaLayout_) != VK_SUCCESS) {
            return false;
        }
        
        VkShaderModule shaderModule;
        if (!loadShader("shaders/temporal_aa.comp.spv", shaderModule)) {
            return false;
        }
        
        VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = taaLayout_;
        
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &taaPipeline_);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

glm::mat4 TemporalSystem::beginFrame(const glm::mat4& viewProj) {
    // Store previous frame data
    prevViewProj_ = currentViewProj_;
    currentViewProj_ = viewProj;
    
    // Get jitter for this frame
    int jitterIndex = frameIndex_ % config_.jitterSequenceLength;
    currentJitter_ = HaltonSequence::sample(jitterIndex);
    
    // Apply jitter to projection (in pixels, then convert to NDC)
    glm::mat4 jitteredViewProj = viewProj;
    jitteredViewProj[2][0] += currentJitter_.x * 2.0f / static_cast<float>(width_);
    jitteredViewProj[2][1] += currentJitter_.y * 2.0f / static_cast<float>(height_);
    
    currentJitteredViewProj_ = jitteredViewProj;
    
    // Swap history buffers
    currentHistoryIndex_ = (currentHistoryIndex_ + 1) % 2;
    
    return jitteredViewProj;
}

glm::vec2 TemporalSystem::getJitterOffset() const {
    return currentJitter_;
}

glm::vec2 TemporalSystem::getJitterUV() const {
    return currentJitter_ / glm::vec2(static_cast<float>(width_), static_cast<float>(height_));
}

void TemporalSystem::generateMotionVectors(VkCommandBuffer cmd,
                                           VkBuffer visibilityBuffer,
                                           VkBuffer vertexBuffer,
                                           VkBuffer indexBuffer,
                                           VkBuffer clusterBuffer,
                                           VkBuffer instanceBuffer,
                                           VkImageView positionBuffer,
                                           const glm::mat4& invViewProj) {
    // Update descriptor set
    VkDescriptorImageInfo imageInfos[2] = {};
    imageInfos[0].imageView = motionVectorsView_;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[1].imageView = positionBuffer;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet writes[2] = {};
    for (int i = 0; i < 2; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = motionVectorDescSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &imageInfos[i];
    }
    
    vkUpdateDescriptorSets(context_->getDevice(), 2, writes, 0, nullptr);
    
    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, motionVectorPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, motionVectorLayout_,
                           0, 1, &motionVectorDescSet_, 0, nullptr);
    
    // Get buffer addresses
    VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    
    MotionVectorPushConstants pc;
    pc.viewProj = currentViewProj_;
    pc.prevViewProj = prevViewProj_;
    pc.invViewProj = invViewProj;
    
    addrInfo.buffer = visibilityBuffer;
    pc.visibilityAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = vertexBuffer;
    pc.vertexAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = indexBuffer;
    pc.indexAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = clusterBuffer;
    pc.clusterAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    addrInfo.buffer = instanceBuffer;
    pc.instanceAddr = vkGetBufferDeviceAddress(context_->getDevice(), &addrInfo);
    
    pc.screenWidth = width_;
    pc.screenHeight = height_;
    pc.vertexStride = 14;  // pos(3) + normal(3) + tangent(4) + uv(2) + prev_pos(2) optional
    
    vkCmdPushConstants(cmd, motionVectorLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(MotionVectorPushConstants), &pc);
    
    uint32_t groupsX = (width_ + 7) / 8;
    uint32_t groupsY = (height_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Barrier for motion vectors
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = motionVectorsImage_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void TemporalSystem::applyTAA(VkCommandBuffer cmd,
                              VkImageView currentFrame,
                              VkImageView depthBuffer,
                              VkImageView outputFrame) {
    // Get history frame
    uint32_t historyIndex = (currentHistoryIndex_ + 1) % 2;
    VkImageView historyView = historyFrames_[historyIndex].colorView;
    
    // Update descriptor set 0 (storage images)
    VkDescriptorImageInfo imageInfos[5] = {};
    imageInfos[0].imageView = currentFrame;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[1].imageView = motionVectorsView_;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[2].imageView = historyView;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[3].imageView = depthBuffer;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[4].imageView = outputFrame;
    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet writes0[5] = {};
    for (int i = 0; i < 5; i++) {
        writes0[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes0[i].dstSet = taaDescSet0_;
        writes0[i].dstBinding = i;
        writes0[i].descriptorCount = 1;
        writes0[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes0[i].pImageInfo = &imageInfos[i];
    }
    
    // Update descriptor set 1 (history sampler)
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.imageView = historyView;
    samplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    samplerInfo.sampler = historySampler_;
    
    VkWriteDescriptorSet write1{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write1.dstSet = taaDescSet1_;
    write1.dstBinding = 0;
    write1.descriptorCount = 1;
    write1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write1.pImageInfo = &samplerInfo;
    
    vkUpdateDescriptorSets(context_->getDevice(), 5, writes0, 0, nullptr);
    vkUpdateDescriptorSets(context_->getDevice(), 1, &write1, 0, nullptr);
    
    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, taaPipeline_);
    
    VkDescriptorSet sets[2] = {taaDescSet0_, taaDescSet1_};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, taaLayout_,
                           0, 2, sets, 0, nullptr);
    
    // Push constants
    TAAPushConstants pc;
    pc.screenSize = glm::vec2(static_cast<float>(width_), static_cast<float>(height_));
    pc.invScreenSize = glm::vec2(1.0f / width_, 1.0f / height_);
    pc.feedbackMin = config_.feedbackMin;
    pc.feedbackMax = config_.feedbackMax;
    pc.motionScale = config_.motionScale;
    pc.jitterX = currentJitter_.x;
    pc.jitterY = currentJitter_.y;
    pc.sharpness = config_.sharpness;
    pc.frameIndex = frameIndex_;
    pc.flags = (config_.varianceClipping ? 1u : 0u);
    
    vkCmdPushConstants(cmd, taaLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(TAAPushConstants), &pc);
    
    uint32_t groupsX = (width_ + 7) / 8;
    uint32_t groupsY = (height_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void TemporalSystem::endFrame(VkCommandBuffer cmd, VkImageView currentResult) {
    // Copy current result to history buffer for next frame
    // Note: In a production implementation, this would be a proper blit/copy
    // For now, we assume the output is already in the correct format
    
    frameIndex_++;
    
    // The TAA output should be copied to historyFrames_[currentHistoryIndex_]
    // This is typically done as part of the present/composite pass
}
