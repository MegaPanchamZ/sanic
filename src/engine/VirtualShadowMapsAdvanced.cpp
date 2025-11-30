/**
 * VirtualShadowMapsAdvanced.cpp
 * 
 * Implementation of enhanced Virtual Shadow Maps system with:
 * - GPU-driven page feedback and allocation
 * - Clipmap support for directional lights
 * - Multi-light shadow management
 * - Per-page HZB for efficient culling
 * 
 * Based on Unreal Engine 5's Virtual Shadow Maps architecture
 */

#include "VirtualShadowMapsAdvanced.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sanic {

// ============================================================================
// CONSTANTS
// ============================================================================

static constexpr uint32_t VSM_PAGE_SIZE = 128;
static constexpr uint32_t VSM_PHYSICAL_POOL_PAGES = 4096;
static constexpr uint32_t VSM_MAX_CLIPMAP_LEVELS = 16;
static constexpr uint32_t VSM_MAX_CACHED_PAGES = 8192;

// ============================================================================
// INITIALIZATION
// ============================================================================

VirtualShadowMapsAdvanced::VirtualShadowMapsAdvanced(VulkanContext& context)
    : context_(context)
    , physicalPoolSize_(0)
    , virtualPageTableSize_(0)
    , maxLights_(32)
    , currentFrame_(0)
{
}

VirtualShadowMapsAdvanced::~VirtualShadowMapsAdvanced() {
    shutdown();
}

void VirtualShadowMapsAdvanced::initialize(uint32_t maxLights, uint32_t physicalPoolPages) {
    maxLights_ = maxLights;
    physicalPoolSize_ = physicalPoolPages;
    
    createPhysicalPool();
    createPageTables();
    createFeedbackBuffers();
    createShaders();
    createPipelines();
    createDescriptorSets();
    
    // Initialize page pool
    pagePool_.initialize(physicalPoolPages);
}

void VirtualShadowMapsAdvanced::shutdown() {
    VkDevice device = context_.getDevice();
    
    // Cleanup pipelines
    if (markPagesPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, markPagesPipeline_, nullptr);
        markPagesPipeline_ = VK_NULL_HANDLE;
    }
    if (allocatePagesPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, allocatePagesPipeline_, nullptr);
        allocatePagesPipeline_ = VK_NULL_HANDLE;
    }
    if (renderPagesPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, renderPagesPipeline_, nullptr);
        renderPagesPipeline_ = VK_NULL_HANDLE;
    }
    if (buildHZBPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, buildHZBPipeline_, nullptr);
        buildHZBPipeline_ = VK_NULL_HANDLE;
    }
    
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    
    // Cleanup images and buffers
    if (physicalPool_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, physicalPool_, nullptr);
        physicalPool_ = VK_NULL_HANDLE;
    }
    if (physicalPoolView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, physicalPoolView_, nullptr);
        physicalPoolView_ = VK_NULL_HANDLE;
    }
    if (pageTableBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, pageTableBuffer_, nullptr);
        pageTableBuffer_ = VK_NULL_HANDLE;
    }
    if (feedbackBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, feedbackBuffer_, nullptr);
        feedbackBuffer_ = VK_NULL_HANDLE;
    }
    if (allocationBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, allocationBuffer_, nullptr);
        allocationBuffer_ = VK_NULL_HANDLE;
    }
    
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// LIGHT MANAGEMENT
// ============================================================================

void VirtualShadowMapsAdvanced::addDirectionalLight(uint32_t lightId, const DirectionalLightData& light) {
    VSMLight vsmLight = {};
    vsmLight.lightId = lightId;
    vsmLight.type = LightType::Directional;
    vsmLight.direction = light.direction;
    vsmLight.color = light.color;
    vsmLight.intensity = light.intensity;
    vsmLight.shadowBias = light.shadowBias;
    vsmLight.cascadeCount = light.cascadeCount;
    
    // Initialize clipmap for directional light
    initializeClipmapForLight(vsmLight, light);
    
    lights_[lightId] = vsmLight;
}

void VirtualShadowMapsAdvanced::addPointLight(uint32_t lightId, const PointLightData& light) {
    VSMLight vsmLight = {};
    vsmLight.lightId = lightId;
    vsmLight.type = LightType::Point;
    vsmLight.position = light.position;
    vsmLight.color = light.color;
    vsmLight.intensity = light.intensity;
    vsmLight.radius = light.radius;
    vsmLight.shadowBias = light.shadowBias;
    
    // Point lights use cube map pages
    vsmLight.virtualPagesX = 64; // Per face
    vsmLight.virtualPagesY = 64;
    vsmLight.virtualPagesZ = 6;  // 6 faces
    
    lights_[lightId] = vsmLight;
}

void VirtualShadowMapsAdvanced::addSpotLight(uint32_t lightId, const SpotLightData& light) {
    VSMLight vsmLight = {};
    vsmLight.lightId = lightId;
    vsmLight.type = LightType::Spot;
    vsmLight.position = light.position;
    vsmLight.direction = light.direction;
    vsmLight.color = light.color;
    vsmLight.intensity = light.intensity;
    vsmLight.radius = light.radius;
    vsmLight.innerAngle = light.innerAngle;
    vsmLight.outerAngle = light.outerAngle;
    vsmLight.shadowBias = light.shadowBias;
    
    // Calculate virtual page dimensions based on cone angle
    float tanAngle = std::tan(light.outerAngle * 0.5f);
    uint32_t pageSize = static_cast<uint32_t>(std::ceil(tanAngle * light.radius / VSM_PAGE_SIZE));
    vsmLight.virtualPagesX = std::max(16u, pageSize);
    vsmLight.virtualPagesY = std::max(16u, pageSize);
    vsmLight.virtualPagesZ = 1;
    
    lights_[lightId] = vsmLight;
}

void VirtualShadowMapsAdvanced::removeLight(uint32_t lightId) {
    auto it = lights_.find(lightId);
    if (it != lights_.end()) {
        // Mark all pages from this light as evictable
        invalidateLightPages(lightId);
        lights_.erase(it);
    }
}

void VirtualShadowMapsAdvanced::updateLight(uint32_t lightId, const glm::mat4& viewProj) {
    auto it = lights_.find(lightId);
    if (it != lights_.end()) {
        // Check if light moved significantly
        glm::mat4 oldViewProj = it->second.viewProjection;
        it->second.viewProjection = viewProj;
        
        // If movement is large enough, invalidate cached pages
        if (hasLightMovedSignificantly(oldViewProj, viewProj)) {
            invalidateLightPages(lightId);
        }
    }
}

// ============================================================================
// CLIPMAP INITIALIZATION
// ============================================================================

void VirtualShadowMapsAdvanced::initializeClipmapForLight(VSMLight& light, 
                                                          const DirectionalLightData& dirLight) {
    light.clipmapLevels.clear();
    light.clipmapLevels.resize(dirLight.cascadeCount);
    
    float currentRadius = dirLight.nearPlane;
    
    for (uint32_t i = 0; i < dirLight.cascadeCount; i++) {
        VSMClipmapLevel& level = light.clipmapLevels[i];
        
        // Calculate cascade bounds
        float nextRadius = currentRadius * dirLight.cascadeDistanceExponent;
        
        level.worldOrigin = dirLight.cameraPosition;
        level.levelRadius = nextRadius;
        level.levelIndex = i;
        level.resolutionScale = 1.0f / (1 << i); // Halve resolution per level
        
        // Virtual page dimensions for this level
        level.virtualPagesX = static_cast<uint32_t>(64 / (1 << std::min(i, 3u)));
        level.virtualPagesY = level.virtualPagesX;
        
        // Snapping to prevent swimming
        float snapSize = (2.0f * nextRadius) / (level.virtualPagesX * VSM_PAGE_SIZE);
        level.worldOrigin = glm::floor(level.worldOrigin / snapSize) * snapSize;
        
        currentRadius = nextRadius;
    }
    
    // Total virtual pages for this light
    uint32_t totalPages = 0;
    for (const auto& level : light.clipmapLevels) {
        totalPages += level.virtualPagesX * level.virtualPagesY;
    }
    
    light.virtualPagesX = 64;  // Max per level
    light.virtualPagesY = 64;
    light.virtualPagesZ = static_cast<uint32_t>(light.clipmapLevels.size());
}

// ============================================================================
// MAIN UPDATE
// ============================================================================

void VirtualShadowMapsAdvanced::update(VkCommandBuffer cmd, const CameraData& camera) {
    currentFrame_++;
    
    // Phase 1: Process feedback from previous frame
    processFeedbackBuffer(cmd);
    
    // Phase 2: Mark requested pages based on visible geometry
    markRequestedPages(cmd, camera);
    
    // Phase 3: Allocate pages from pool
    allocatePages(cmd);
    
    // Phase 4: Render shadow pages
    for (auto& [lightId, light] : lights_) {
        renderLightPages(cmd, light, camera);
    }
    
    // Phase 5: Build per-page HZB for culling
    buildPageHZB(cmd);
}

// ============================================================================
// PAGE MARKING
// ============================================================================

void VirtualShadowMapsAdvanced::markRequestedPages(VkCommandBuffer cmd, const CameraData& camera) {
    // Clear request buffer
    vkCmdFillBuffer(cmd, feedbackBuffer_, 0, feedbackBufferSize_, 0);
    
    VkBufferMemoryBarrier clearBarrier = {};
    clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    clearBarrier.buffer = feedbackBuffer_;
    clearBarrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 1, &clearBarrier, 0, nullptr);
    
    // Dispatch page marking compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, markPagesPipeline_);
    
    struct MarkPagesPush {
        glm::mat4 viewProj;
        glm::vec4 cameraPos;
        glm::vec4 screenSize;
        uint32_t frameIndex;
        uint32_t lightCount;
        uint32_t pad0, pad1;
    } pushData;
    
    pushData.viewProj = camera.viewProjection;
    pushData.cameraPos = glm::vec4(camera.position, 1.0f);
    pushData.screenSize = glm::vec4(camera.width, camera.height,
                                    1.0f / camera.width, 1.0f / camera.height);
    pushData.frameIndex = currentFrame_;
    pushData.lightCount = static_cast<uint32_t>(lights_.size());
    
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(pushData), &pushData);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                           0, 1, &markPagesDescriptorSet_, 0, nullptr);
    
    // Dispatch based on screen resolution
    uint32_t groupsX = (camera.width + 7) / 8;
    uint32_t groupsY = (camera.height + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

// ============================================================================
// PAGE ALLOCATION
// ============================================================================

void VirtualShadowMapsAdvanced::allocatePages(VkCommandBuffer cmd) {
    // Barrier for feedback buffer read
    VkBufferMemoryBarrier feedbackBarrier = {};
    feedbackBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    feedbackBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    feedbackBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    feedbackBarrier.buffer = feedbackBuffer_;
    feedbackBarrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 1, &feedbackBarrier, 0, nullptr);
    
    // Dispatch allocation compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, allocatePagesPipeline_);
    
    struct AllocatePagesPush {
        uint32_t maxPages;
        uint32_t frameIndex;
        uint32_t evictionThreshold;
        uint32_t pad;
    } pushData;
    
    pushData.maxPages = physicalPoolSize_;
    pushData.frameIndex = currentFrame_;
    pushData.evictionThreshold = 30; // Frames before eviction
    
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(pushData), &pushData);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                           0, 1, &allocateDescriptorSet_, 0, nullptr);
    
    // Single workgroup handles allocation decisions
    vkCmdDispatch(cmd, 1, 1, 1);
}

// ============================================================================
// PAGE RENDERING
// ============================================================================

void VirtualShadowMapsAdvanced::renderLightPages(VkCommandBuffer cmd,
                                                  VSMLight& light,
                                                  const CameraData& camera) {
    // Barrier for allocation completion
    VkBufferMemoryBarrier allocBarrier = {};
    allocBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    allocBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    allocBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    allocBarrier.buffer = allocationBuffer_;
    allocBarrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 1, &allocBarrier, 0, nullptr);
    
    // For each clipmap level (directional) or face (point)
    if (light.type == LightType::Directional) {
        for (uint32_t level = 0; level < light.clipmapLevels.size(); level++) {
            renderClipmapLevel(cmd, light, level, camera);
        }
    } else if (light.type == LightType::Point) {
        for (uint32_t face = 0; face < 6; face++) {
            renderCubeFace(cmd, light, face, camera);
        }
    } else {
        // Spot light - single view
        renderSpotLight(cmd, light, camera);
    }
}

void VirtualShadowMapsAdvanced::renderClipmapLevel(VkCommandBuffer cmd,
                                                    VSMLight& light,
                                                    uint32_t level,
                                                    const CameraData& camera) {
    const VSMClipmapLevel& clipmap = light.clipmapLevels[level];
    
    // Calculate view-projection for this clipmap level
    glm::mat4 lightView = glm::lookAt(
        clipmap.worldOrigin + light.direction * clipmap.levelRadius,
        clipmap.worldOrigin,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    
    float orthoSize = clipmap.levelRadius;
    glm::mat4 lightProj = glm::ortho(
        -orthoSize, orthoSize,
        -orthoSize, orthoSize,
        0.1f, clipmap.levelRadius * 2.0f
    );
    
    glm::mat4 lightViewProj = lightProj * lightView;
    
    // Bind render pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, renderPagesPipeline_);
    
    struct RenderPagesPush {
        glm::mat4 lightViewProj;
        glm::vec4 lightParams;
        uint32_t lightId;
        uint32_t levelIndex;
        uint32_t virtualPagesX;
        uint32_t virtualPagesY;
    } pushData;
    
    pushData.lightViewProj = lightViewProj;
    pushData.lightParams = glm::vec4(light.shadowBias, 0.0f, 0.0f, 0.0f);
    pushData.lightId = light.lightId;
    pushData.levelIndex = level;
    pushData.virtualPagesX = clipmap.virtualPagesX;
    pushData.virtualPagesY = clipmap.virtualPagesY;
    
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(pushData), &pushData);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                           0, 1, &renderDescriptorSet_, 0, nullptr);
    
    // Dispatch one thread per virtual page
    uint32_t groupsX = (clipmap.virtualPagesX + 7) / 8;
    uint32_t groupsY = (clipmap.virtualPagesY + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void VirtualShadowMapsAdvanced::renderCubeFace(VkCommandBuffer cmd,
                                                VSMLight& light,
                                                uint32_t face,
                                                const CameraData& camera) {
    // Cube face directions
    static const glm::vec3 faceDirections[6] = {
        { 1.0f,  0.0f,  0.0f}, // +X
        {-1.0f,  0.0f,  0.0f}, // -X
        { 0.0f,  1.0f,  0.0f}, // +Y
        { 0.0f, -1.0f,  0.0f}, // -Y
        { 0.0f,  0.0f,  1.0f}, // +Z
        { 0.0f,  0.0f, -1.0f}, // -Z
    };
    
    static const glm::vec3 faceUpVectors[6] = {
        { 0.0f, -1.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  0.0f, -1.0f},
        { 0.0f, -1.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
    };
    
    glm::mat4 faceView = glm::lookAt(
        light.position,
        light.position + faceDirections[face],
        faceUpVectors[face]
    );
    
    glm::mat4 faceProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, light.radius);
    glm::mat4 faceViewProj = faceProj * faceView;
    
    // Same rendering dispatch as clipmap level
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, renderPagesPipeline_);
    
    struct RenderPagesPush {
        glm::mat4 lightViewProj;
        glm::vec4 lightParams;
        uint32_t lightId;
        uint32_t faceIndex;
        uint32_t virtualPagesX;
        uint32_t virtualPagesY;
    } pushData;
    
    pushData.lightViewProj = faceViewProj;
    pushData.lightParams = glm::vec4(light.shadowBias, light.radius, 0.0f, 0.0f);
    pushData.lightId = light.lightId;
    pushData.faceIndex = face;
    pushData.virtualPagesX = light.virtualPagesX;
    pushData.virtualPagesY = light.virtualPagesY;
    
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(pushData), &pushData);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                           0, 1, &renderDescriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (light.virtualPagesX + 7) / 8;
    uint32_t groupsY = (light.virtualPagesY + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void VirtualShadowMapsAdvanced::renderSpotLight(VkCommandBuffer cmd,
                                                 VSMLight& light,
                                                 const CameraData& camera) {
    glm::mat4 lightView = glm::lookAt(
        light.position,
        light.position + light.direction,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    
    glm::mat4 lightProj = glm::perspective(
        light.outerAngle,
        1.0f,
        0.1f,
        light.radius
    );
    
    glm::mat4 lightViewProj = lightProj * lightView;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, renderPagesPipeline_);
    
    struct RenderPagesPush {
        glm::mat4 lightViewProj;
        glm::vec4 lightParams;
        uint32_t lightId;
        uint32_t unused;
        uint32_t virtualPagesX;
        uint32_t virtualPagesY;
    } pushData;
    
    pushData.lightViewProj = lightViewProj;
    pushData.lightParams = glm::vec4(light.shadowBias, light.radius, 
                                     light.innerAngle, light.outerAngle);
    pushData.lightId = light.lightId;
    pushData.virtualPagesX = light.virtualPagesX;
    pushData.virtualPagesY = light.virtualPagesY;
    
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(pushData), &pushData);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                           0, 1, &renderDescriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (light.virtualPagesX + 7) / 8;
    uint32_t groupsY = (light.virtualPagesY + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

// ============================================================================
// HZB GENERATION
// ============================================================================

void VirtualShadowMapsAdvanced::buildPageHZB(VkCommandBuffer cmd) {
    // Barrier for shadow rendering completion
    VkImageMemoryBarrier renderBarrier = {};
    renderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    renderBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    renderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    renderBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    renderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    renderBarrier.image = physicalPool_;
    renderBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    
    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &renderBarrier);
    
    // Generate HZB mip chain for each allocated page
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, buildHZBPipeline_);
    
    struct HZBPush {
        uint32_t pageSize;
        uint32_t mipLevel;
        uint32_t poolWidth;
        uint32_t poolHeight;
    } pushData;
    
    pushData.pageSize = VSM_PAGE_SIZE;
    pushData.poolWidth = physicalPoolWidth_;
    pushData.poolHeight = physicalPoolHeight_;
    
    // Generate mips
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(VSM_PAGE_SIZE))) + 1;
    
    for (uint32_t mip = 0; mip < mipLevels; mip++) {
        pushData.mipLevel = mip;
        
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                          0, sizeof(pushData), &pushData);
        
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                               0, 1, &hzbDescriptorSet_, 0, nullptr);
        
        uint32_t mipSize = VSM_PAGE_SIZE >> mip;
        uint32_t groupsX = (physicalPoolWidth_ * mipSize + 7) / 8;
        uint32_t groupsY = (physicalPoolHeight_ * mipSize + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
        
        // Barrier between mip levels
        if (mip < mipLevels - 1) {
            VkImageMemoryBarrier mipBarrier = {};
            mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            mipBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mipBarrier.image = pageHZB_;
            mipBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
            
            vkCmdPipelineBarrier(cmd,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                0, 0, nullptr, 0, nullptr, 1, &mipBarrier);
        }
    }
}

// ============================================================================
// FEEDBACK PROCESSING
// ============================================================================

void VirtualShadowMapsAdvanced::processFeedbackBuffer(VkCommandBuffer cmd) {
    // Read back feedback from GPU (async readback from previous frame)
    // In production, this would use timeline semaphores or async compute
    
    // Update page pool LRU based on access patterns
    pagePool_.updateAccessTimes(currentFrame_);
}

// ============================================================================
// SHADOW SAMPLING
// ============================================================================

float VirtualShadowMapsAdvanced::sampleShadow(uint32_t lightId, 
                                              const glm::vec3& worldPos,
                                              const glm::vec3& normal) {
    // This is called from shaders via buffer data
    // Implementation is in the shadow sampling compute/fragment shader
    return 1.0f; // CPU-side placeholder
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

bool VirtualShadowMapsAdvanced::hasLightMovedSignificantly(const glm::mat4& oldVP, 
                                                            const glm::mat4& newVP) {
    // Check if view-projection changed significantly
    float threshold = 0.01f;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (std::abs(oldVP[i][j] - newVP[i][j]) > threshold) {
                return true;
            }
        }
    }
    
    return false;
}

void VirtualShadowMapsAdvanced::invalidateLightPages(uint32_t lightId) {
    // Mark all pages belonging to this light as candidates for eviction
    pagePool_.markLightPagesForEviction(lightId);
}

// ============================================================================
// RESOURCE CREATION
// ============================================================================

void VirtualShadowMapsAdvanced::createPhysicalPool() {
    VkDevice device = context_.getDevice();
    
    // Calculate pool dimensions (square-ish arrangement of pages)
    uint32_t pagesPerRow = static_cast<uint32_t>(std::ceil(std::sqrt(physicalPoolSize_)));
    physicalPoolWidth_ = pagesPerRow;
    physicalPoolHeight_ = (physicalPoolSize_ + pagesPerRow - 1) / pagesPerRow;
    
    uint32_t poolWidthPixels = physicalPoolWidth_ * VSM_PAGE_SIZE;
    uint32_t poolHeightPixels = physicalPoolHeight_ * VSM_PAGE_SIZE;
    
    // Create depth image for shadow storage
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.extent = {poolWidthPixels, poolHeightPixels, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | 
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_STORAGE_BIT;
    
    vkCreateImage(device, &imageInfo, nullptr, &physicalPool_);
    
    // Allocate memory and create view...
}

void VirtualShadowMapsAdvanced::createPageTables() {
    VkDevice device = context_.getDevice();
    
    // Page table maps virtual pages to physical pages
    // Format: [lightId][level/face][virtualY][virtualX] -> physicalPageIndex
    size_t pageTableSize = maxLights_ * VSM_MAX_CLIPMAP_LEVELS * 64 * 64 * sizeof(uint32_t);
    
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = pageTableSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    vkCreateBuffer(device, &bufferInfo, nullptr, &pageTableBuffer_);
    
    virtualPageTableSize_ = pageTableSize;
}

void VirtualShadowMapsAdvanced::createFeedbackBuffers() {
    VkDevice device = context_.getDevice();
    
    // Feedback buffer for page requests from rendering
    feedbackBufferSize_ = maxLights_ * VSM_MAX_CLIPMAP_LEVELS * 64 * 64 * sizeof(uint32_t);
    
    VkBufferCreateInfo feedbackInfo = {};
    feedbackInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    feedbackInfo.size = feedbackBufferSize_;
    feedbackInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    
    vkCreateBuffer(device, &feedbackInfo, nullptr, &feedbackBuffer_);
    
    // Allocation result buffer
    VkBufferCreateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    allocInfo.size = physicalPoolSize_ * sizeof(VSMPageAllocation);
    allocInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    
    vkCreateBuffer(device, &allocInfo, nullptr, &allocationBuffer_);
}

void VirtualShadowMapsAdvanced::createShaders() {
    // Load shader modules
}

void VirtualShadowMapsAdvanced::createPipelines() {
    // Create compute pipelines
}

void VirtualShadowMapsAdvanced::createDescriptorSets() {
    // Create descriptor sets
}

// ============================================================================
// PAGE POOL IMPLEMENTATION
// ============================================================================

void VSMPagePool::initialize(uint32_t maxPages) {
    maxPages_ = maxPages;
    pages_.resize(maxPages);
    freeList_.reserve(maxPages);
    
    // Initialize all pages as free
    for (uint32_t i = 0; i < maxPages; i++) {
        pages_[i].physicalIndex = i;
        pages_[i].state = VSMPageState::Free;
        pages_[i].lastAccessFrame = 0;
        freeList_.push_back(i);
    }
}

uint32_t VSMPagePool::allocatePage(uint32_t lightId, uint32_t virtualX, uint32_t virtualY, 
                                    uint32_t level, uint32_t currentFrame) {
    // Try to get from free list
    if (!freeList_.empty()) {
        uint32_t pageIndex = freeList_.back();
        freeList_.pop_back();
        
        VSMPage& page = pages_[pageIndex];
        page.state = VSMPageState::Rendering;
        page.lightId = lightId;
        page.virtualX = virtualX;
        page.virtualY = virtualY;
        page.level = level;
        page.lastAccessFrame = currentFrame;
        
        return pageIndex;
    }
    
    // Need to evict - find LRU page
    uint32_t evictIndex = findEvictionCandidate(currentFrame);
    if (evictIndex != UINT32_MAX) {
        VSMPage& page = pages_[evictIndex];
        page.state = VSMPageState::Rendering;
        page.lightId = lightId;
        page.virtualX = virtualX;
        page.virtualY = virtualY;
        page.level = level;
        page.lastAccessFrame = currentFrame;
        
        return evictIndex;
    }
    
    return UINT32_MAX; // Allocation failed
}

void VSMPagePool::freePage(uint32_t pageIndex) {
    if (pageIndex < pages_.size()) {
        pages_[pageIndex].state = VSMPageState::Free;
        freeList_.push_back(pageIndex);
    }
}

void VSMPagePool::updateAccessTimes(uint32_t currentFrame) {
    // This would be called with GPU feedback data
}

uint32_t VSMPagePool::findEvictionCandidate(uint32_t currentFrame) {
    uint32_t bestCandidate = UINT32_MAX;
    uint32_t oldestFrame = currentFrame;
    
    for (uint32_t i = 0; i < pages_.size(); i++) {
        if (pages_[i].state == VSMPageState::Cached) {
            if (pages_[i].lastAccessFrame < oldestFrame) {
                oldestFrame = pages_[i].lastAccessFrame;
                bestCandidate = i;
            }
        }
    }
    
    return bestCandidate;
}

void VSMPagePool::markLightPagesForEviction(uint32_t lightId) {
    for (auto& page : pages_) {
        if (page.lightId == lightId && page.state == VSMPageState::Cached) {
            page.lastAccessFrame = 0; // Make it the oldest
        }
    }
}

} // namespace sanic
