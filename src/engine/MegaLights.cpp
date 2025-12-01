/**
 * MegaLights.cpp
 * 
 * Implementation of the MegaLights scalable lighting system.
 * Based on Unreal Engine 5's approach to handling many dynamic lights.
 */

#include "MegaLights.h"
#include "VulkanContext.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace Sanic {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

MegaLights::MegaLights(VulkanContext& context) : context_(context) {
}

MegaLights::~MegaLights() {
    shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool MegaLights::initialize(uint32_t width, uint32_t height, const MegaLightsConfig& config) {
    screenWidth_ = width;
    screenHeight_ = height;
    config_ = config;
    
    createResources();
    createPipelines();
    createDescriptorSets();
    
    return true;
}

void MegaLights::shutdown() {
    VkDevice device = context_.getDevice();
    
    // Wait for device to be idle
    vkDeviceWaitIdle(device);
    
    // Destroy images
    auto destroyImage = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (img) vkDestroyImage(device, img, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        view = VK_NULL_HANDLE;
        img = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    };
    
    destroyImage(shadowMask_, shadowMaskMemory_, shadowMaskView_);
    destroyImage(lightingBuffer_, lightingBufferMemory_, lightingBufferView_);
    destroyImage(denoisedShadow_, denoisedShadowMemory_, denoisedShadowView_);
    destroyImage(varianceBuffer_, varianceMemory_, varianceView_);
    
    for (int i = 0; i < 2; ++i) {
        destroyImage(historyBuffers_[i], historyMemory_[i], historyViews_[i]);
    }
    
    // Destroy buffers
    auto destroyBuffer = [&](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf) vkDestroyBuffer(device, buf, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        buf = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    };
    
    destroyBuffer(lightBuffer_, lightBufferMemory_);
    destroyBuffer(clusterBuffer_, clusterBufferMemory_);
    destroyBuffer(lightIndexBuffer_, lightIndexBufferMemory_);
    destroyBuffer(sampleBuffer_, sampleBufferMemory_);
    destroyBuffer(uniformBuffer_, uniformMemory_);
    
    // Destroy samplers
    if (linearSampler_) vkDestroySampler(device, linearSampler_, nullptr);
    if (pointSampler_) vkDestroySampler(device, pointSampler_, nullptr);
    
    // Destroy pipelines
    if (clusterBuildPipeline_) vkDestroyPipeline(device, clusterBuildPipeline_, nullptr);
    if (lightSamplePipeline_) vkDestroyPipeline(device, lightSamplePipeline_, nullptr);
    if (shadowEvalPipeline_) vkDestroyPipeline(device, shadowEvalPipeline_, nullptr);
    if (spatialDenoisePipeline_) vkDestroyPipeline(device, spatialDenoisePipeline_, nullptr);
    if (temporalDenoisePipeline_) vkDestroyPipeline(device, temporalDenoisePipeline_, nullptr);
    if (resolvePipeline_) vkDestroyPipeline(device, resolvePipeline_, nullptr);
    
    if (computeLayout_) vkDestroyPipelineLayout(device, computeLayout_, nullptr);
    if (descriptorLayout_) vkDestroyDescriptorSetLayout(device, descriptorLayout_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
}

void MegaLights::resize(uint32_t width, uint32_t height) {
    screenWidth_ = width;
    screenHeight_ = height;
    
    // Recreate screen-sized resources
    VkDevice device = context_.getDevice();
    
    auto recreateImage = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view,
                            VkFormat format, VkImageUsageFlags usage) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (img) vkDestroyImage(device, img, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        createImage2D(img, mem, view, width, height, format, usage);
    };
    
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    recreateImage(shadowMask_, shadowMaskMemory_, shadowMaskView_, 
                  VK_FORMAT_R16G16B16A16_SFLOAT, usage);
    recreateImage(lightingBuffer_, lightingBufferMemory_, lightingBufferView_,
                  VK_FORMAT_R16G16B16A16_SFLOAT, usage);
    recreateImage(denoisedShadow_, denoisedShadowMemory_, denoisedShadowView_,
                  VK_FORMAT_R16G16B16A16_SFLOAT, usage);
    recreateImage(varianceBuffer_, varianceMemory_, varianceView_,
                  VK_FORMAT_R16G16_SFLOAT, usage);
    
    for (int i = 0; i < 2; ++i) {
        recreateImage(historyBuffers_[i], historyMemory_[i], historyViews_[i],
                     VK_FORMAT_R16G16B16A16_SFLOAT, usage);
    }
}

void MegaLights::setConfig(const MegaLightsConfig& config) {
    config_ = config;
}

// ============================================================================
// LIGHT MANAGEMENT
// ============================================================================

uint32_t MegaLights::addLight(const MegaLight& light) {
    MegaLight newLight = light;
    newLight.id = nextLightId_++;
    lights_.push_back(newLight);
    return newLight.id;
}

void MegaLights::updateLight(uint32_t id, const MegaLight& light) {
    for (auto& l : lights_) {
        if (l.id == id) {
            l = light;
            l.id = id;  // Preserve ID
            return;
        }
    }
}

void MegaLights::removeLight(uint32_t id) {
    lights_.erase(
        std::remove_if(lights_.begin(), lights_.end(),
            [id](const MegaLight& l) { return l.id == id; }),
        lights_.end()
    );
}

void MegaLights::clearLights() {
    lights_.clear();
}

MegaLight* MegaLights::getLight(uint32_t id) {
    for (auto& l : lights_) {
        if (l.id == id) return &l;
    }
    return nullptr;
}

// ============================================================================
// PER-FRAME UPDATE
// ============================================================================

void MegaLights::beginFrame(const glm::mat4& view, const glm::mat4& proj,
                            const glm::vec3& cameraPos) {
    prevViewProjMatrix_ = viewProjMatrix_;
    viewMatrix_ = view;
    projMatrix_ = proj;
    viewProjMatrix_ = proj * view;
    cameraPosition_ = cameraPos;
    
    // Calculate light importance for this frame
    calculateLightImportance(cameraPos, viewProjMatrix_);
    
    // Allocate VSM pages based on importance
    if (config_.enableVSM) {
        allocateVSMPages();
    }
    
    // Update GPU buffers
    updateLightBuffer();
    
    // Update uniform buffer
    MegaLightsUniforms uniforms;
    uniforms.viewMatrix = viewMatrix_;
    uniforms.projMatrix = projMatrix_;
    uniforms.invViewMatrix = glm::inverse(viewMatrix_);
    uniforms.invProjMatrix = glm::inverse(projMatrix_);
    uniforms.viewProjMatrix = viewProjMatrix_;
    uniforms.prevViewProjMatrix = prevViewProjMatrix_;
    
    uniforms.cameraPosition = glm::vec4(cameraPosition_, 1.0f);
    uniforms.screenParams = glm::vec4(
        float(screenWidth_), float(screenHeight_),
        1.0f / float(screenWidth_), 1.0f / float(screenHeight_)
    );
    
    uint32_t totalClusters = config_.clusterCountX * config_.clusterCountY * config_.clusterCountZ;
    uniforms.clusterDims = glm::ivec4(
        config_.clusterCountX, config_.clusterCountY, config_.clusterCountZ, totalClusters
    );
    uniforms.depthParams = glm::vec4(
        config_.nearPlane, config_.farPlane, config_.depthExponent, 0.0f
    );
    
    uniforms.lightCount = static_cast<uint32_t>(lights_.size());
    uniforms.samplesPerPixel = config_.samplesPerPixel;
    uniforms.frameIndex = frameIndex_;
    uniforms.flags = (config_.useImportanceSampling ? 1 : 0) |
                    (config_.enableDenoising ? 2 : 0) |
                    (config_.useBlueNoise ? 4 : 0);
    
    memcpy(uniformMapped_, &uniforms, sizeof(uniforms));
    
    frameIndex_++;
}

void MegaLights::calculateLightImportance(const glm::vec3& cameraPos, const glm::mat4& viewProj) {
    for (auto& light : lights_) {
        if (!light.enabled) {
            light.importance = 0.0f;
            continue;
        }
        
        // Distance-based importance
        glm::vec3 toLight = light.position - cameraPos;
        float distance = glm::length(toLight);
        
        if (distance > light.range * 10.0f) {
            light.importance = 0.0f;
            continue;
        }
        
        // Screen-space coverage estimation
        glm::vec4 clipPos = viewProj * glm::vec4(light.position, 1.0f);
        if (clipPos.w <= 0.0f) {
            // Behind camera - still might affect visible geometry
            light.importance = 0.1f;
            continue;
        }
        
        float ndcX = clipPos.x / clipPos.w;
        float ndcY = clipPos.y / clipPos.w;
        
        // Frustum check (with margin for light range)
        bool inFrustum = ndcX >= -1.5f && ndcX <= 1.5f && 
                         ndcY >= -1.5f && ndcY <= 1.5f;
        
        if (!inFrustum && distance > light.range) {
            light.importance = 0.0f;
            continue;
        }
        
        // Calculate importance based on power and screen coverage
        float luminance = glm::dot(light.color, glm::vec3(0.299f, 0.587f, 0.114f));
        float power = luminance * light.intensity;
        
        // Approximate solid angle
        float solidAngle = (light.range * light.range) / (distance * distance + 1.0f);
        solidAngle = std::min(solidAngle, 4.0f);  // Cap at ~hemisphere
        
        light.importance = power * solidAngle;
        light.samplingWeight = light.importance;
    }
    
    // Normalize sampling weights
    float totalWeight = 0.0f;
    for (const auto& light : lights_) {
        totalWeight += light.samplingWeight;
    }
    
    if (totalWeight > 0.0f) {
        for (auto& light : lights_) {
            light.samplingWeight /= totalWeight;
        }
    }
}

void MegaLights::allocateVSMPages() {
    // Sort lights by importance
    std::vector<size_t> sortedIndices(lights_.size());
    for (size_t i = 0; i < lights_.size(); ++i) {
        sortedIndices[i] = i;
    }
    
    std::sort(sortedIndices.begin(), sortedIndices.end(),
        [this](size_t a, size_t b) {
            return lights_[a].importance > lights_[b].importance;
        });
    
    // Allocate pages based on importance
    uint32_t pagesUsed = 0;
    for (size_t idx : sortedIndices) {
        MegaLight& light = lights_[idx];
        
        if (!light.castsShadow || light.importance < config_.importanceThreshold) {
            light.vsmPageStart = 0;
            light.vsmPageCount = 0;
            continue;
        }
        
        // Calculate pages based on importance
        uint32_t desiredPages = static_cast<uint32_t>(
            std::ceil(light.importance * float(config_.maxVSMPagesPerLight))
        );
        desiredPages = std::clamp(desiredPages, 1u, config_.maxVSMPagesPerLight);
        
        // Check budget
        if (pagesUsed + desiredPages > config_.totalVSMBudget) {
            desiredPages = config_.totalVSMBudget - pagesUsed;
            if (desiredPages == 0) {
                light.vsmPageStart = 0;
                light.vsmPageCount = 0;
                continue;
            }
        }
        
        light.vsmPageStart = pagesUsed;
        light.vsmPageCount = desiredPages;
        pagesUsed += desiredPages;
    }
    
    stats_.vsmPagesUsed = pagesUsed;
}

void MegaLights::updateLightBuffer() {
    if (lights_.empty()) return;
    
    std::vector<GPUMegaLight> gpuLights(lights_.size());
    
    for (size_t i = 0; i < lights_.size(); ++i) {
        const MegaLight& light = lights_[i];
        GPUMegaLight& gpu = gpuLights[i];
        
        gpu.positionAndType = glm::vec4(light.position, static_cast<float>(light.type));
        gpu.directionAndRange = glm::vec4(light.direction, light.range);
        gpu.colorAndIntensity = glm::vec4(light.color, light.intensity);
        gpu.spotParams = glm::vec4(
            std::cos(glm::radians(light.innerConeAngle)),
            std::cos(glm::radians(light.outerConeAngle)),
            light.falloffExponent,
            light.importance
        );
    }
    
    // Upload to GPU
    void* mapped;
    vkMapMemory(context_.getDevice(), lightBufferMemory_, 0, 
                gpuLights.size() * sizeof(GPUMegaLight), 0, &mapped);
    memcpy(mapped, gpuLights.data(), gpuLights.size() * sizeof(GPUMegaLight));
    vkUnmapMemory(context_.getDevice(), lightBufferMemory_);
}

// ============================================================================
// RENDERING PHASES
// ============================================================================

void MegaLights::buildLightClusters(VkCommandBuffer cmd) {
    if (lights_.empty()) return;
    
    // Bind pipeline and descriptors
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, clusterBuildPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);
    
    // Dispatch: one thread per cluster
    uint32_t groupsX = (config_.clusterCountX + 7) / 8;
    uint32_t groupsY = (config_.clusterCountY + 7) / 8;
    uint32_t groupsZ = (config_.clusterCountZ + 3) / 4;
    
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
    
    // Barrier for cluster buffer
    VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, 
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void MegaLights::sampleLights(VkCommandBuffer cmd,
                              VkImageView depthBuffer,
                              VkImageView normalBuffer,
                              VkImageView blueNoiseTexture) {
    if (lights_.empty()) return;
    
    // TODO: Update descriptor sets with input textures
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lightSamplePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);
    
    // Dispatch: one thread per pixel
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Barrier
    VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void MegaLights::evaluateShadows(VkCommandBuffer cmd,
                                  VkImageView depthBuffer,
                                  VirtualShadowMap* vsm) {
    if (lights_.empty()) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadowEvalPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Barrier for shadow mask
    VkImageMemoryBarrier imageBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.image = shadowMask_;
    imageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
}

void MegaLights::denoise(VkCommandBuffer cmd,
                         VkImageView velocityBuffer,
                         VkImageView depthBuffer) {
    if (!config_.enableDenoising) return;
    
    // Spatial denoise pass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatialDenoisePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Barrier
    VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // Temporal denoise pass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporalDenoisePipeline_);
    
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Swap history buffers
    currentHistoryIndex_ = 1 - currentHistoryIndex_;
    
    // Barrier
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void MegaLights::resolve(VkCommandBuffer cmd,
                         VkImageView albedoBuffer,
                         VkImageView normalBuffer,
                         VkImageView pbrBuffer,
                         VkImageView outputBuffer) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolvePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void MegaLights::debugVisualize(VkCommandBuffer cmd, VkImageView output, int mode) {
    // TODO: Implement debug visualization
    // mode 0: Light clusters
    // mode 1: Sampled lights per pixel
    // mode 2: Raw shadows
    // mode 3: Denoised shadows
    // mode 4: VSM page allocation
}

// ============================================================================
// RESOURCE CREATION
// ============================================================================

void MegaLights::createResources() {
    VkDevice device = context_.getDevice();
    
    // Calculate buffer sizes
    size_t maxLights = 4096;
    size_t lightBufferSize = maxLights * sizeof(GPUMegaLight);
    
    uint32_t totalClusters = config_.clusterCountX * config_.clusterCountY * config_.clusterCountZ;
    size_t clusterBufferSize = totalClusters * sizeof(GPULightCluster);
    size_t lightIndexBufferSize = totalClusters * LightCluster::MAX_LIGHTS_PER_CLUSTER * sizeof(uint32_t);
    size_t sampleBufferSize = screenWidth_ * screenHeight_ * config_.samplesPerPixel * sizeof(LightSample);
    
    // Create buffers
    createBuffer(lightBuffer_, lightBufferMemory_, lightBufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    createBuffer(clusterBuffer_, clusterBufferMemory_, clusterBufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    createBuffer(lightIndexBuffer_, lightIndexBufferMemory_, lightIndexBufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    createBuffer(sampleBuffer_, sampleBufferMemory_, sampleBufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    // Uniform buffer
    createBuffer(uniformBuffer_, uniformMemory_, sizeof(MegaLightsUniforms),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    vkMapMemory(device, uniformMemory_, 0, sizeof(MegaLightsUniforms), 0, &uniformMapped_);
    
    // Create images
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    createImage2D(shadowMask_, shadowMaskMemory_, shadowMaskView_,
                 screenWidth_, screenHeight_, VK_FORMAT_R16G16B16A16_SFLOAT, usage);
    
    createImage2D(lightingBuffer_, lightingBufferMemory_, lightingBufferView_,
                 screenWidth_, screenHeight_, VK_FORMAT_R16G16B16A16_SFLOAT, usage);
    
    createImage2D(denoisedShadow_, denoisedShadowMemory_, denoisedShadowView_,
                 screenWidth_, screenHeight_, VK_FORMAT_R16G16B16A16_SFLOAT, usage);
    
    createImage2D(varianceBuffer_, varianceMemory_, varianceView_,
                 screenWidth_, screenHeight_, VK_FORMAT_R16G16_SFLOAT, usage);
    
    for (int i = 0; i < 2; ++i) {
        createImage2D(historyBuffers_[i], historyMemory_[i], historyViews_[i],
                     screenWidth_, screenHeight_, VK_FORMAT_R16G16B16A16_SFLOAT, usage);
    }
    
    // Create samplers
    VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    vkCreateSampler(device, &samplerInfo, nullptr, &linearSampler_);
    
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(device, &samplerInfo, nullptr, &pointSampler_);
}

void MegaLights::createPipelines() {
    VkDevice device = context_.getDevice();
    
    // Create descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Uniform buffer
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Light buffer
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Cluster buffer
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Light index buffer
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Sample buffer
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Shadow mask (output)
        {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Lighting buffer (output)
        {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Denoised shadow (output)
        {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // History buffers
        {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Variance buffer
        {9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Input textures (depth, normal, etc.)
        {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout_);
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout_;
    
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &computeLayout_);
    
    // Create compute pipelines
    auto createComputePipeline = [&](const std::string& shaderPath, VkPipeline& pipeline) {
        VkShaderModule shaderModule = loadShader(shaderPath);
        if (shaderModule == VK_NULL_HANDLE) return;
        
        VkPipelineShaderStageCreateInfo stageInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";
        
        VkComputePipelineCreateInfo pipelineInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = computeLayout_;
        
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        
        vkDestroyShaderModule(device, shaderModule, nullptr);
    };
    
    createComputePipeline("shaders/megalights_cluster.comp.spv", clusterBuildPipeline_);
    createComputePipeline("shaders/megalights_sample.comp.spv", lightSamplePipeline_);
    createComputePipeline("shaders/megalights_shadow.comp.spv", shadowEvalPipeline_);
    createComputePipeline("shaders/megalights_spatial_denoise.comp.spv", spatialDenoisePipeline_);
    createComputePipeline("shaders/megalights_temporal_denoise.comp.spv", temporalDenoisePipeline_);
    createComputePipeline("shaders/megalights_resolve.comp.spv", resolvePipeline_);
}

void MegaLights::createDescriptorSets() {
    VkDevice device = context_.getDevice();
    
    // Create descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6},
    };
    
    VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_);
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorLayout_;
    
    vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet_);
    
    // Update descriptor set with buffer bindings
    std::vector<VkWriteDescriptorSet> writes;
    
    VkDescriptorBufferInfo uniformBufferInfo = {uniformBuffer_, 0, sizeof(MegaLightsUniforms)};
    VkDescriptorBufferInfo lightBufferInfo = {lightBuffer_, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo clusterBufferInfo = {clusterBuffer_, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo lightIndexBufferInfo = {lightIndexBuffer_, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo sampleBufferInfo = {sampleBuffer_, 0, VK_WHOLE_SIZE};
    
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.descriptorCount = 1;
    
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &uniformBufferInfo;
    writes.push_back(write);
    
    write.dstBinding = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &lightBufferInfo;
    writes.push_back(write);
    
    write.dstBinding = 2;
    write.pBufferInfo = &clusterBufferInfo;
    writes.push_back(write);
    
    write.dstBinding = 3;
    write.pBufferInfo = &lightIndexBufferInfo;
    writes.push_back(write);
    
    write.dstBinding = 4;
    write.pBufferInfo = &sampleBufferInfo;
    writes.push_back(write);
    
    // Image bindings
    VkDescriptorImageInfo shadowMaskInfo = {VK_NULL_HANDLE, shadowMaskView_, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo lightingBufferInfo = {VK_NULL_HANDLE, lightingBufferView_, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo denoisedShadowInfo = {VK_NULL_HANDLE, denoisedShadowView_, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo varianceInfo = {VK_NULL_HANDLE, varianceView_, VK_IMAGE_LAYOUT_GENERAL};
    
    write.pBufferInfo = nullptr;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    
    write.dstBinding = 5;
    write.pImageInfo = &shadowMaskInfo;
    writes.push_back(write);
    
    write.dstBinding = 6;
    write.pImageInfo = &lightingBufferInfo;
    writes.push_back(write);
    
    write.dstBinding = 7;
    write.pImageInfo = &denoisedShadowInfo;
    writes.push_back(write);
    
    write.dstBinding = 9;
    write.pImageInfo = &varianceInfo;
    writes.push_back(write);
    
    // History samplers
    std::array<VkDescriptorImageInfo, 2> historyInfos;
    for (int i = 0; i < 2; ++i) {
        historyInfos[i] = {linearSampler_, historyViews_[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    
    write.dstBinding = 8;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 2;
    write.pImageInfo = historyInfos.data();
    writes.push_back(write);
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void MegaLights::createImage2D(VkImage& image, VkDeviceMemory& memory, VkImageView& view,
                               uint32_t width, uint32_t height, VkFormat format,
                               VkImageUsageFlags usage) {
    VkDevice device = context_.getDevice();
    
    // Create image
    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateImage(device, &imageInfo, nullptr, &image);
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, image, &memReqs);
    
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_.findMemoryType(memReqs.memoryTypeBits, 
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    vkBindImageMemory(device, image, memory, 0);
    
    // Create view
    VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    vkCreateImageView(device, &viewInfo, nullptr, &view);
}

void MegaLights::createBuffer(VkBuffer& buffer, VkDeviceMemory& memory,
                              VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags properties) {
    VkDevice device = context_.getDevice();
    
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, buffer, &memReqs);
    
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_.findMemoryType(memReqs.memoryTypeBits, properties);
    
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    vkBindBufferMemory(device, buffer, memory, 0);
}

VkShaderModule MegaLights::loadShader(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return VK_NULL_HANDLE;
    }
    
    size_t fileSize = file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    
    VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(context_.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return shaderModule;
}

} // namespace Sanic
