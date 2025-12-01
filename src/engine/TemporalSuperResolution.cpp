/**
 * TemporalSuperResolution.cpp
 * 
 * Temporal Super Resolution implementation
 */

#include "TemporalSuperResolution.h"
#include "VulkanContext.h"

#include <cstring>
#include <fstream>
#include <algorithm>

namespace Sanic {

TemporalSuperResolution::TemporalSuperResolution(VulkanContext& context)
    : context_(context) {
}

TemporalSuperResolution::~TemporalSuperResolution() {
    shutdown();
}

bool TemporalSuperResolution::initialize(uint32_t renderWidth, uint32_t renderHeight,
                                         uint32_t outputWidth, uint32_t outputHeight,
                                         const TSRConfig& config) {
    renderWidth_ = renderWidth;
    renderHeight_ = renderHeight;
    outputWidth_ = outputWidth;
    outputHeight_ = outputHeight;
    config_ = config;
    
    // Calculate actual upscale ratio
    config_.upscaleRatio = float(outputWidth) / float(renderWidth);
    
    // Generate Halton jitter sequence
    haltonSequence_ = Halton::generateSequence(config_.jitterPhases);
    
    createResources();
    createPipelines();
    
    return true;
}

void TemporalSuperResolution::shutdown() {
    VkDevice device = context_.getDevice();
    
    vkDeviceWaitIdle(device);
    
    // Destroy pipelines
    if (reprojectPipeline_) vkDestroyPipeline(device, reprojectPipeline_, nullptr);
    if (reconstructPipeline_) vkDestroyPipeline(device, reconstructPipeline_, nullptr);
    if (sharpenPipeline_) vkDestroyPipeline(device, sharpenPipeline_, nullptr);
    if (dilateMotionPipeline_) vkDestroyPipeline(device, dilateMotionPipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (descriptorLayout_) vkDestroyDescriptorSetLayout(device, descriptorLayout_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    
    // Destroy samplers
    if (linearSampler_) vkDestroySampler(device, linearSampler_, nullptr);
    if (pointSampler_) vkDestroySampler(device, pointSampler_, nullptr);
    
    // Destroy images
    for (size_t i = 0; i < TSR_HISTORY_COUNT; ++i) {
        destroyImage(historyImages_[i], historyMemory_[i], historyViews_[i]);
    }
    
    destroyImage(reprojectedImage_, reprojectedMemory_, reprojectedView_);
    destroyImage(reconstructedImage_, reconstructedMemory_, reconstructedView_);
    destroyImage(outputImage_, outputMemory_, outputView_);
    destroyImage(dilatedMotionImage_, dilatedMotionMemory_, dilatedMotionView_);
    
    // Destroy buffers
    if (uniformBuffer_) vkDestroyBuffer(device, uniformBuffer_, nullptr);
    if (uniformMemory_) vkFreeMemory(device, uniformMemory_, nullptr);
    
    // Destroy query pool
    if (queryPool_) vkDestroyQueryPool(device, queryPool_, nullptr);
}

void TemporalSuperResolution::setConfig(const TSRConfig& config) {
    config_ = config;
    config_.upscaleRatio = float(outputWidth_) / float(renderWidth_);
    haltonSequence_ = Halton::generateSequence(config_.jitterPhases);
}

void TemporalSuperResolution::setOutputResolution(uint32_t width, uint32_t height) {
    if (width == outputWidth_ && height == outputHeight_) return;
    
    outputWidth_ = width;
    outputHeight_ = height;
    config_.upscaleRatio = float(outputWidth_) / float(renderWidth_);
    
    // Recreate output-sized resources
    VkDevice device = context_.getDevice();
    
    for (size_t i = 0; i < TSR_HISTORY_COUNT; ++i) {
        destroyImage(historyImages_[i], historyMemory_[i], historyViews_[i]);
    }
    destroyImage(reprojectedImage_, reprojectedMemory_, reprojectedView_);
    destroyImage(reconstructedImage_, reconstructedMemory_, reconstructedView_);
    destroyImage(outputImage_, outputMemory_, outputView_);
    
    createResources();
    cameraReset_ = true;
}

void TemporalSuperResolution::setRenderResolution(uint32_t width, uint32_t height) {
    if (width == renderWidth_ && height == renderHeight_) return;
    
    renderWidth_ = width;
    renderHeight_ = height;
    config_.upscaleRatio = float(outputWidth_) / float(renderWidth_);
    
    // Recreate render-sized resources
    destroyImage(dilatedMotionImage_, dilatedMotionMemory_, dilatedMotionView_);
    createResources();
    cameraReset_ = true;
}

void TemporalSuperResolution::beginFrame(const glm::mat4& view, const glm::mat4& proj,
                                         const glm::mat4& prevViewProj) {
    frameIndex_++;
    historyIndex_ = frameIndex_ % TSR_HISTORY_COUNT;
    
    // Update jitter
    prevJitter_ = currentJitter_;
    updateJitter();
    
    // Build frame data
    frameData_.viewMatrix = view;
    frameData_.projMatrix = proj;
    frameData_.viewProjMatrix = proj * view;
    frameData_.invViewProjMatrix = glm::inverse(frameData_.viewProjMatrix);
    frameData_.prevViewProjMatrix = prevViewProj;
    
    frameData_.jitterOffset = glm::vec4(currentJitter_, prevJitter_);
    frameData_.screenParams = glm::vec4(
        float(renderWidth_), float(renderHeight_),
        1.0f / renderWidth_, 1.0f / renderHeight_
    );
    frameData_.outputParams = glm::vec4(
        float(outputWidth_), float(outputHeight_),
        1.0f / outputWidth_, 1.0f / outputHeight_
    );
    
    frameData_.upscaleRatio = config_.upscaleRatio;
    frameData_.historyBlend = cameraReset_ ? 0.0f : config_.historyBlend;
    frameData_.sharpening = config_.sharpening;
    frameData_.frameIndex = frameIndex_;
    
    frameData_.flags = 0;
    if (config_.enableAntiGhosting) frameData_.flags |= 1;
    if (config_.enableSubpixelReconstruction) frameData_.flags |= 2;
    if (config_.dilateMotionVectors) frameData_.flags |= 4;
    if (cameraReset_) frameData_.flags |= 8;
    
    cameraReset_ = false;
    
    updateFrameData();
}

glm::vec2 TemporalSuperResolution::getJitterOffsetNDC() const {
    return currentJitter_ * glm::vec2(2.0f / renderWidth_, 2.0f / renderHeight_);
}

void TemporalSuperResolution::execute(VkCommandBuffer cmd,
                                      VkImageView colorInput,
                                      VkImageView depthInput,
                                      VkImageView motionVectors,
                                      VkImageView reactivityMask) {
    // Pass 1: Dilate motion vectors (optional)
    if (config_.dilateMotionVectors) {
        // Dilate motion vectors to handle disocclusion
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dilateMotionPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                               pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
        
        uint32_t groupsX = (renderWidth_ + 7) / 8;
        uint32_t groupsY = (renderHeight_ + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
        
        // Barrier
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    
    // Pass 2: Reproject history
    passReproject(cmd, colorInput, depthInput, motionVectors);
    
    // Barrier
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // Pass 3: Reconstruct at output resolution
    passReconstruct(cmd);
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // Pass 4: Sharpening
    if (config_.sharpening > 0.0f) {
        passSharpen(cmd);
    }
}

void TemporalSuperResolution::passReproject(VkCommandBuffer cmd, VkImageView color,
                                            VkImageView depth, VkImageView motion) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, reprojectPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (outputWidth_ + 7) / 8;
    uint32_t groupsY = (outputHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void TemporalSuperResolution::passReconstruct(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, reconstructPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (outputWidth_ + 7) / 8;
    uint32_t groupsY = (outputHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void TemporalSuperResolution::passSharpen(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sharpenPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (outputWidth_ + 7) / 8;
    uint32_t groupsY = (outputHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

VkImageView TemporalSuperResolution::getHistoryForLumen() const {
    // Return the previous frame's history for Lumen to sample
    uint32_t prevIndex = (historyIndex_ + TSR_HISTORY_COUNT - 1) % TSR_HISTORY_COUNT;
    return historyViews_[prevIndex];
}

void TemporalSuperResolution::onCameraCut() {
    cameraReset_ = true;
}

void TemporalSuperResolution::createResources() {
    VkDevice device = context_.getDevice();
    
    // History buffers at output resolution
    for (size_t i = 0; i < TSR_HISTORY_COUNT; ++i) {
        createImage(historyImages_[i], historyMemory_[i], historyViews_[i],
                   outputWidth_, outputHeight_,
                   VK_FORMAT_R16G16B16A16_SFLOAT,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    
    // Intermediate buffers
    createImage(reprojectedImage_, reprojectedMemory_, reprojectedView_,
               outputWidth_, outputHeight_,
               VK_FORMAT_R16G16B16A16_SFLOAT,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    createImage(reconstructedImage_, reconstructedMemory_, reconstructedView_,
               outputWidth_, outputHeight_,
               VK_FORMAT_R16G16B16A16_SFLOAT,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Output
    createImage(outputImage_, outputMemory_, outputView_,
               outputWidth_, outputHeight_,
               VK_FORMAT_R16G16B16A16_SFLOAT,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Dilated motion at render resolution
    createImage(dilatedMotionImage_, dilatedMotionMemory_, dilatedMotionView_,
               renderWidth_, renderHeight_,
               VK_FORMAT_R16G16_SFLOAT,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Uniform buffer
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = sizeof(TSRFrameData);
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &bufInfo, nullptr, &uniformBuffer_);
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, uniformBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = 0; // TODO: Find host visible memory
    vkAllocateMemory(device, &allocInfo, nullptr, &uniformMemory_);
    vkBindBufferMemory(device, uniformBuffer_, uniformMemory_, 0);
    vkMapMemory(device, uniformMemory_, 0, sizeof(TSRFrameData), 0, &uniformMapped_);
    
    // Samplers
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    vkCreateSampler(device, &samplerInfo, nullptr, &linearSampler_);
    
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    vkCreateSampler(device, &samplerInfo, nullptr, &pointSampler_);
    
    // Query pool for timing
    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 8;
    vkCreateQueryPool(device, &queryInfo, nullptr, &queryPool_);
}

void TemporalSuperResolution::createPipelines() {
    VkDevice device = context_.getDevice();
    
    // Descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // Color input
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // Depth
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // Motion
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // History
        {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},          // Reprojected
        {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},          // Reconstructed
        {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},          // Output
        {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},          // New history
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout_);
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout_;
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout_);
    
    // Load shaders
    VkShaderModule reprojectShader = loadShader("shaders/tsr_reproject.comp.spv");
    VkShaderModule reconstructShader = loadShader("shaders/tsr_reconstruct.comp.spv");
    VkShaderModule sharpenShader = loadShader("shaders/tsr_sharpen.comp.spv");
    VkShaderModule dilateShader = loadShader("shaders/tsr_dilate_motion.comp.spv");
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.pName = "main";
    
    if (reprojectShader) {
        pipelineInfo.stage.module = reprojectShader;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &reprojectPipeline_);
        vkDestroyShaderModule(device, reprojectShader, nullptr);
    }
    
    if (reconstructShader) {
        pipelineInfo.stage.module = reconstructShader;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &reconstructPipeline_);
        vkDestroyShaderModule(device, reconstructShader, nullptr);
    }
    
    if (sharpenShader) {
        pipelineInfo.stage.module = sharpenShader;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sharpenPipeline_);
        vkDestroyShaderModule(device, sharpenShader, nullptr);
    }
    
    if (dilateShader) {
        pipelineInfo.stage.module = dilateShader;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &dilateMotionPipeline_);
        vkDestroyShaderModule(device, dilateShader, nullptr);
    }
    
    // Descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16},
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 4;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorLayout_;
    vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet_);
}

void TemporalSuperResolution::updateJitter() {
    int index = frameIndex_ % config_.jitterPhases;
    currentJitter_ = haltonSequence_[index] * config_.jitterSpread;
}

void TemporalSuperResolution::updateFrameData() {
    memcpy(uniformMapped_, &frameData_, sizeof(TSRFrameData));
}

void TemporalSuperResolution::createImage(VkImage& image, VkDeviceMemory& memory, 
                                          VkImageView& view,
                                          uint32_t width, uint32_t height,
                                          VkFormat format, VkImageUsageFlags usage,
                                          uint32_t mipLevels) {
    VkDevice device = context_.getDevice();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device, &imageInfo, nullptr, &image);
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, image, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = 0; // TODO: Find device local memory
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    vkBindImageMemory(device, image, memory, 0);
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &viewInfo, nullptr, &view);
}

void TemporalSuperResolution::destroyImage(VkImage& image, VkDeviceMemory& memory, 
                                           VkImageView& view) {
    VkDevice device = context_.getDevice();
    
    if (view) vkDestroyImageView(device, view, nullptr);
    if (image) vkDestroyImage(device, image, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
    
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}

VkShaderModule TemporalSuperResolution::loadShader(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return VK_NULL_HANDLE;
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> code((fileSize + 3) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), fileSize);
    
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = fileSize;
    createInfo.pCode = code.data();
    
    VkShaderModule module;
    if (vkCreateShaderModule(context_.getDevice(), &createInfo, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return module;
}

} // namespace Sanic
