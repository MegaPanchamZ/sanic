/**
 * PostProcess.cpp
 * 
 * Implementation of the post-processing pipeline.
 * 
 * Turn 37-39: Post-processing
 */

#include "PostProcess.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <fstream>

PostProcess::~PostProcess() {
    cleanup();
}

bool PostProcess::initialize(VulkanContext* context,
                             uint32_t width,
                             uint32_t height,
                             const PostProcessConfig& config) {
    context_ = context;
    width_ = width;
    height_ = height;
    config_ = config;
    
    if (!createRenderTargets()) {
        return false;
    }
    
    if (!createPipelines()) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void PostProcess::cleanup() {
    if (!context_) return;
    
    // Note: In real implementation, these would use VulkanContext methods
    // to properly destroy resources
    
    initialized_ = false;
}

void PostProcess::resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) return;
    
    width_ = width;
    height_ = height;
    
    // Recreate resolution-dependent resources
}

bool PostProcess::createRenderTargets() {
    // Create bloom mip chain
    uint32_t mipWidth = width_ / 2;
    uint32_t mipHeight = height_ / 2;
    
    // In real implementation, create images and views using VulkanContext
    
    return true;
}

bool PostProcess::createPipelines() {
    // Create compute pipelines for each post-processing effect
    // In real implementation, load shaders and create pipelines
    
    return true;
}

bool PostProcess::loadShader(const std::string& path, VkShaderModule& outModule) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    // In real implementation, create shader module using VulkanContext
    
    return true;
}

void PostProcess::process(VkCommandBuffer cmd,
                          VkImageView hdrInput,
                          VkImageView depthBuffer,
                          VkImageView velocityBuffer,
                          VkImageView outputLDR,
                          float deltaTime) {
    frameIndex_++;
    time_ += deltaTime;
    
    // 1. Compute auto-exposure
    if (config_.autoExposure) {
        computeAutoExposure(cmd, hdrInput);
    }
    
    // 2. Generate bloom
    if (config_.bloom.enabled) {
        computeBloom(cmd, hdrInput);
    }
    
    // 3. Depth of field
    VkImageView dofResult = hdrInput;
    if (config_.dof.enabled) {
        computeDOF(cmd, hdrInput, depthBuffer);
        dofResult = intermediateView_;
    }
    
    // 4. Motion blur
    VkImageView mbResult = dofResult;
    if (config_.motionBlur.enabled && velocityBuffer != VK_NULL_HANDLE) {
        computeMotionBlur(cmd, dofResult, velocityBuffer);
        mbResult = intermediateView_;
    }
    
    // 5. Final tonemap with bloom composite
    applyTonemap(cmd, mbResult, outputLDR);
}

void PostProcess::computeBloom(VkCommandBuffer cmd, VkImageView hdrInput) {
    // Downsample pass - extract bright pixels and create mip chain
    bloomDownsample(cmd);
    
    // Upsample pass - combine mips with blur
    bloomUpsample(cmd);
}

void PostProcess::bloomDownsample(VkCommandBuffer cmd) {
    if (!bloomDownPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bloomDownPipeline_);
    
    for (size_t i = 0; i < bloomMipViews_.size(); i++) {
        PostProcessPushConstants pc{};
        pc.bloomParams.x = config_.bloom.threshold;
        pc.bloomParams.y = config_.bloom.intensity;
        pc.bloomParams.z = config_.bloom.scatter;
        pc.bloomParams.w = static_cast<float>(i);
        
        uint32_t mipWidth = std::max(1u, width_ >> (i + 1));
        uint32_t mipHeight = std::max(1u, height_ >> (i + 1));
        
        pc.screenSize = glm::vec4(mipWidth, mipHeight, 1.0f / mipWidth, 1.0f / mipHeight);
        
        vkCmdPushConstants(cmd, bloomLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        
        uint32_t groupsX = (mipWidth + 7) / 8;
        uint32_t groupsY = (mipHeight + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
        
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
}

void PostProcess::bloomUpsample(VkCommandBuffer cmd) {
    if (!bloomUpPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bloomUpPipeline_);
    
    for (int i = static_cast<int>(bloomMipViews_.size()) - 2; i >= 0; i--) {
        PostProcessPushConstants pc{};
        pc.bloomParams.y = config_.bloom.intensity;
        pc.bloomParams.z = config_.bloom.scatter;
        pc.bloomParams.w = static_cast<float>(i);
        
        uint32_t mipWidth = std::max(1u, width_ >> (i + 1));
        uint32_t mipHeight = std::max(1u, height_ >> (i + 1));
        
        pc.screenSize = glm::vec4(mipWidth, mipHeight, 1.0f / mipWidth, 1.0f / mipHeight);
        
        vkCmdPushConstants(cmd, bloomLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        
        uint32_t groupsX = (mipWidth + 7) / 8;
        uint32_t groupsY = (mipHeight + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
        
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
}

void PostProcess::computeDOF(VkCommandBuffer cmd, VkImageView input, VkImageView depth) {
    if (!dofCocPipeline_) return;
    
    // Pass 1: Calculate Circle of Confusion
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dofCocPipeline_);
    
    PostProcessPushConstants pc{};
    pc.dofParams.x = config_.dof.focusDistance;
    pc.dofParams.y = config_.dof.focusRange;
    pc.dofParams.z = config_.dof.maxBlur;
    pc.dofParams.w = config_.dof.aperture;
    pc.screenSize = glm::vec4(width_, height_, 1.0f / width_, 1.0f / height_);
    
    vkCmdPushConstants(cmd, dofLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    
    uint32_t groupsX = (width_ + 7) / 8;
    uint32_t groupsY = (height_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // Pass 2: Blur
    if (dofBlurPipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dofBlurPipeline_);
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    
    // Pass 3: Composite
    if (dofCompositePipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dofCompositePipeline_);
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }
}

void PostProcess::computeMotionBlur(VkCommandBuffer cmd, VkImageView input, VkImageView velocity) {
    if (!motionBlurPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, motionBlurPipeline_);
    
    PostProcessPushConstants pc{};
    pc.screenSize = glm::vec4(width_, height_, 1.0f / width_, 1.0f / height_);
    pc.frameIndex = frameIndex_;
    
    vkCmdPushConstants(cmd, motionBlurLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    
    uint32_t groupsX = (width_ + 7) / 8;
    uint32_t groupsY = (height_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void PostProcess::computeAutoExposure(VkCommandBuffer cmd, VkImageView hdrInput) {
    // Pass 1: Build histogram
    if (histogramPipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, histogramPipeline_);
        
        uint32_t groupsX = (width_ + 15) / 16;
        uint32_t groupsY = (height_ + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
        
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    
    // Pass 2: Compute exposure from histogram
    if (exposurePipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, exposurePipeline_);
        vkCmdDispatch(cmd, 1, 1, 1);
        
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
}

void PostProcess::applyTonemap(VkCommandBuffer cmd, VkImageView hdrInput, VkImageView output) {
    if (!tonemapPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tonemapPipeline_);
    
    PostProcessPushConstants pc{};
    pc.screenSize = glm::vec4(width_, height_, 1.0f / width_, 1.0f / height_);
    pc.tonemapParams.x = config_.colorGrading.exposure;
    pc.tonemapParams.y = config_.gamma;
    pc.tonemapParams.z = static_cast<float>(static_cast<int>(config_.tonemap));
    
    pc.bloomParams.y = config_.bloom.enabled ? config_.bloom.intensity : 0.0f;
    
    pc.vignetteParams.x = config_.vignette.enabled ? config_.vignette.intensity : 0.0f;
    pc.vignetteParams.y = config_.vignette.smoothness;
    pc.vignetteParams.z = config_.vignette.center.x;
    pc.vignetteParams.w = config_.vignette.center.y;
    
    pc.colorFilter = glm::vec4(config_.colorGrading.colorFilter, 1.0f);
    
    pc.time = time_;
    pc.frameIndex = frameIndex_;
    
    pc.flags = 0;
    if (config_.filmGrain.enabled) pc.flags |= 1;
    if (config_.chromaticAberration.enabled) pc.flags |= 2;
    if (config_.sharpen.enabled) pc.flags |= 4;
    
    vkCmdPushConstants(cmd, tonemapLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    
    uint32_t groupsX = (width_ + 7) / 8;
    uint32_t groupsY = (height_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void PostProcess::applyFXAA(VkCommandBuffer cmd, VkImageView input, VkImageView output) {
    if (!fxaaPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fxaaPipeline_);
    
    PostProcessPushConstants pc{};
    pc.screenSize = glm::vec4(width_, height_, 1.0f / width_, 1.0f / height_);
    
    vkCmdPushConstants(cmd, fxaaLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    
    uint32_t groupsX = (width_ + 7) / 8;
    uint32_t groupsY = (height_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}
