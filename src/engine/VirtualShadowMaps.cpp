/**
 * VirtualShadowMaps.cpp
 * 
 * Implementation of Virtual Shadow Maps with page streaming.
 * 
 * Turn 37-39: Virtual Shadow Maps
 */

#include "VirtualShadowMaps.h"
#include <algorithm>
#include <cstring>
#include <fstream>

VirtualShadowMaps::~VirtualShadowMaps() {
    cleanup();
}

bool VirtualShadowMaps::initialize(VulkanContext* context, const VSMConfig& config) {
    context_ = context;
    config_ = config;
    
    if (!createShadowAtlas()) {
        return false;
    }
    
    if (!createPageTable()) {
        return false;
    }
    
    if (!createClipMaps()) {
        return false;
    }
    
    if (!createPipelines()) {
        return false;
    }
    
    // Initialize free page slots
    uint32_t pagesPerRow = config_.physicalAtlasSize / config_.pageSize;
    uint32_t totalPages = pagesPerRow * pagesPerRow;
    
    freePageSlots_.resize(totalPages);
    for (uint32_t i = 0; i < totalPages; i++) {
        freePageSlots_[i] = i;
    }
    
    initialized_ = true;
    return true;
}

void VirtualShadowMaps::cleanup() {
    if (!context_) return;
    
    // In real implementation, destroy all Vulkan resources
    
    residentPages_.clear();
    residentPageHashes_.clear();
    freePageSlots_.clear();
    clipMapLevels_.clear();
    
    initialized_ = false;
}

bool VirtualShadowMaps::createShadowAtlas() {
    // Create shadow atlas image
    // In real implementation, use VulkanContext to create VkImage
    
    return true;
}

bool VirtualShadowMaps::createPageTable() {
    // Calculate total pages across all virtual mip levels
    uint32_t totalPages = 0;
    uint32_t resolution = config_.virtualResolution;
    
    for (uint32_t level = 0; level < config_.clipMapLevels; level++) {
        uint32_t pageCount = resolution / config_.pageSize;
        totalPages += pageCount * pageCount;
        resolution /= 2;
    }
    
    totalPages *= config_.maxLights;
    
    // Create page table buffer
    // In real implementation, use VulkanContext
    
    return true;
}

bool VirtualShadowMaps::createClipMaps() {
    clipMapLevels_.resize(config_.clipMapLevels);
    
    float extent = config_.clipMapBaseExtent;
    uint32_t resolution = config_.virtualResolution;
    uint32_t pageTableOffset = 0;
    
    for (uint32_t i = 0; i < config_.clipMapLevels; i++) {
        ShadowClipMapLevel& level = clipMapLevels_[i];
        level.center = glm::vec3(0.0f);
        level.texelSize = extent / static_cast<float>(resolution);
        level.resolution = resolution;
        level.pageTableOffset = pageTableOffset;
        level.needsUpdate = true;
        
        uint32_t pageCount = resolution / config_.pageSize;
        pageTableOffset += pageCount * pageCount;
        
        extent *= config_.clipMapScale;
        resolution /= 2;
    }
    
    return true;
}

bool VirtualShadowMaps::createPipelines() {
    // Create compute pipelines for page marking and sampling
    // Create graphics pipeline for shadow rendering
    
    return true;
}

bool VirtualShadowMaps::loadShader(const std::string& path, VkShaderModule& outModule) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    // In real implementation, create shader module
    
    return true;
}

void VirtualShadowMaps::update(VkCommandBuffer cmd,
                                const glm::vec3& cameraPos,
                                const glm::mat4& cameraViewProj,
                                const std::vector<ShadowLightInfo>& lights) {
    frameIndex_++;
    
    // Update clipmaps centered on camera
    updateClipMaps(cameraPos);
    
    // Stream in/out pages based on visibility
    streamPages(cmd);
    
    // Evict old unused pages
    evictOldPages();
}

void VirtualShadowMaps::updateClipMaps(const glm::vec3& cameraPos) {
    for (size_t i = 0; i < clipMapLevels_.size(); i++) {
        ShadowClipMapLevel& level = clipMapLevels_[i];
        
        // Snap center to texel grid
        float texelSize = level.texelSize;
        glm::vec3 snappedPos = glm::floor(cameraPos / texelSize) * texelSize;
        
        // Check if we need to update this level
        glm::vec3 delta = snappedPos - level.center;
        float threshold = texelSize * 4.0f;  // Update when moved 4 texels
        
        if (glm::length(delta) > threshold) {
            level.center = snappedPos;
            level.needsUpdate = true;
        }
    }
}

void VirtualShadowMaps::markVisiblePages(VkCommandBuffer cmd,
                                          VkImageView depthBuffer,
                                          VkImageView normalBuffer,
                                          const glm::mat4& invViewProj) {
    if (!markPagesPipeline_) return;
    
    // Clear page request count
    // vkCmdFillBuffer(cmd, pageRequestBuffer_, 0, sizeof(uint32_t), 0);
    
    // Bind compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, markPagesPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, markPagesLayout_,
                            0, 1, &descSet_, 0, nullptr);
    
    // Push constants with camera matrices
    struct PushData {
        glm::mat4 invViewProj;
        uint32_t width;
        uint32_t height;
        uint32_t pageSize;
        uint32_t virtualResolution;
    } pushData;
    
    pushData.invViewProj = invViewProj;
    pushData.width = config_.virtualResolution;  // Actual screen size in real impl
    pushData.height = config_.virtualResolution;
    pushData.pageSize = config_.pageSize;
    pushData.virtualResolution = config_.virtualResolution;
    
    vkCmdPushConstants(cmd, markPagesLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 
                       0, sizeof(pushData), &pushData);
    
    // Dispatch
    uint32_t groupsX = (pushData.width + 7) / 8;
    uint32_t groupsY = (pushData.height + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Barrier before reading results
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VirtualShadowMaps::renderPages(VkCommandBuffer cmd,
                                     VkBuffer vertexBuffer,
                                     VkBuffer indexBuffer,
                                     VkBuffer drawCommands,
                                     uint32_t drawCount) {
    if (!renderShadowPipeline_ || !shadowFramebuffer_) return;
    
    // Begin render pass
    VkRenderPassBeginInfo rpBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBeginInfo.renderPass = shadowRenderPass_;
    rpBeginInfo.framebuffer = shadowFramebuffer_;
    rpBeginInfo.renderArea.offset = {0, 0};
    rpBeginInfo.renderArea.extent = {config_.physicalAtlasSize, config_.physicalAtlasSize};
    
    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};
    rpBeginInfo.clearValueCount = 1;
    rpBeginInfo.pClearValues = &clearValue;
    
    vkCmdBeginRenderPass(cmd, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderShadowPipeline_);
    
    // For each dirty page that needs rendering
    for (const auto& page : residentPages_) {
        if (!page.dirty) continue;
        
        // Set viewport to page region
        VkViewport viewport{};
        viewport.x = static_cast<float>(page.pageX * config_.pageSize);
        viewport.y = static_cast<float>(page.pageY * config_.pageSize);
        viewport.width = static_cast<float>(config_.pageSize);
        viewport.height = static_cast<float>(config_.pageSize);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        
        VkRect2D scissor{};
        scissor.offset = {static_cast<int32_t>(page.pageX * config_.pageSize),
                          static_cast<int32_t>(page.pageY * config_.pageSize)};
        scissor.extent = {config_.pageSize, config_.pageSize};
        
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        
        // Push light view-proj for this page
        vkCmdPushConstants(cmd, renderShadowLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(glm::mat4), &page.lightViewProj);
        
        // Draw shadow casters
        // In real implementation, bind buffers and draw
    }
    
    vkCmdEndRenderPass(cmd);
}

void VirtualShadowMaps::bindForSampling(VkCommandBuffer cmd,
                                         VkPipelineLayout layout,
                                         uint32_t setIndex) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                            setIndex, 1, &descSet_, 0, nullptr);
}

float VirtualShadowMaps::getShadowFactor(const glm::vec3& worldPos,
                                          const glm::vec3& normal,
                                          uint32_t lightIndex) const {
    // CPU-side shadow lookup (for debugging/validation)
    // In real usage, shadows are computed on GPU
    return 1.0f;
}

void VirtualShadowMaps::streamPages(VkCommandBuffer cmd) {
    // In real implementation:
    // 1. Read back page requests from GPU
    // 2. Allocate physical pages for requested virtual pages
    // 3. Update page table
    // 4. Mark pages as dirty for rendering
    
    // This is where you'd process requests and allocate from freePageSlots_
}

void VirtualShadowMaps::evictOldPages() {
    // Find pages not used recently and evict them
    const uint32_t evictThreshold = 16;  // Frames unused before eviction
    
    auto it = residentPages_.begin();
    while (it != residentPages_.end()) {
        if (frameIndex_ - it->lastUsedFrame > evictThreshold) {
            // Return page to free list
            uint32_t pagesPerRow = config_.physicalAtlasSize / config_.pageSize;
            uint32_t slotIndex = it->pageY * pagesPerRow + it->pageX;
            freePageSlots_.push_back(slotIndex);
            
            // Remove from hash set
            uint64_t hash = getPageHash(it->lightIndex, it->mipLevel, it->pageX, it->pageY);
            residentPageHashes_.erase(hash);
            
            it = residentPages_.erase(it);
        } else {
            ++it;
        }
    }
}

uint64_t VirtualShadowMaps::getPageHash(uint32_t lightIndex, uint32_t level, 
                                         uint32_t x, uint32_t y) const {
    return (static_cast<uint64_t>(lightIndex) << 48) |
           (static_cast<uint64_t>(level) << 40) |
           (static_cast<uint64_t>(x) << 20) |
           static_cast<uint64_t>(y);
}
