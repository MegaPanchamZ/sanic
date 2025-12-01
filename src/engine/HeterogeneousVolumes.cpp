/**
 * HeterogeneousVolumes.cpp
 * 
 * Heterogeneous volume rendering system implementation
 */

#include "HeterogeneousVolumes.h"
#include "VulkanContext.h"

#include <cmath>
#include <fstream>
#include <algorithm>
#include <random>

namespace Sanic {

// ============================================================================
// HETEROGENEOUS VOLUME METHODS
// ============================================================================

void HeterogeneousVolume::setResolution(uint32_t x, uint32_t y, uint32_t z) {
    resolution = glm::uvec3(x, y, z);
    brickCount = (resolution + glm::uvec3(VOLUME_BRICK_SIZE - 1)) / glm::uvec3(VOLUME_BRICK_SIZE);
    
    size_t totalVoxels = size_t(x) * y * z;
    densityData.resize(totalVoxels, 0.0f);
    isDirty = true;
}

void HeterogeneousVolume::setDensity(uint32_t x, uint32_t y, uint32_t z, float value) {
    if (x >= resolution.x || y >= resolution.y || z >= resolution.z) return;
    size_t idx = size_t(z) * resolution.x * resolution.y + size_t(y) * resolution.x + x;
    densityData[idx] = value;
    isDirty = true;
}

float HeterogeneousVolume::getDensity(uint32_t x, uint32_t y, uint32_t z) const {
    if (x >= resolution.x || y >= resolution.y || z >= resolution.z) return 0.0f;
    size_t idx = size_t(z) * resolution.x * resolution.y + size_t(y) * resolution.x + x;
    return densityData[idx];
}

void HeterogeneousVolume::setTemperature(uint32_t x, uint32_t y, uint32_t z, float value) {
    if (x >= resolution.x || y >= resolution.y || z >= resolution.z) return;
    size_t totalVoxels = size_t(resolution.x) * resolution.y * resolution.z;
    if (temperatureData.size() != totalVoxels) {
        temperatureData.resize(totalVoxels, 0.0f);
    }
    size_t idx = size_t(z) * resolution.x * resolution.y + size_t(y) * resolution.x + x;
    temperatureData[idx] = value;
    isDirty = true;
}

void HeterogeneousVolume::setEmission(uint32_t x, uint32_t y, uint32_t z, const glm::vec3& value) {
    if (x >= resolution.x || y >= resolution.y || z >= resolution.z) return;
    size_t totalVoxels = size_t(resolution.x) * resolution.y * resolution.z;
    if (emissionData.size() != totalVoxels) {
        emissionData.resize(totalVoxels, glm::vec3(0.0f));
    }
    size_t idx = size_t(z) * resolution.x * resolution.y + size_t(y) * resolution.x + x;
    emissionData[idx] = value;
    isDirty = true;
}

void HeterogeneousVolume::buildBricks() {
    uint32_t totalBricks = brickCount.x * brickCount.y * brickCount.z;
    bricks.resize(totalBricks);
    
    for (uint32_t bz = 0; bz < brickCount.z; ++bz) {
        for (uint32_t by = 0; by < brickCount.y; ++by) {
            for (uint32_t bx = 0; bx < brickCount.x; ++bx) {
                uint32_t brickIdx = bz * brickCount.x * brickCount.y + by * brickCount.x + bx;
                VolumeBrick& brick = bricks[brickIdx];
                
                float minDens = 1e10f, maxDens = -1e10f;
                
                // Sample all voxels in this brick
                for (uint32_t lz = 0; lz < VOLUME_BRICK_SIZE; ++lz) {
                    for (uint32_t ly = 0; ly < VOLUME_BRICK_SIZE; ++ly) {
                        for (uint32_t lx = 0; lx < VOLUME_BRICK_SIZE; ++lx) {
                            uint32_t gx = bx * VOLUME_BRICK_SIZE + lx;
                            uint32_t gy = by * VOLUME_BRICK_SIZE + ly;
                            uint32_t gz = bz * VOLUME_BRICK_SIZE + lz;
                            
                            float density = getDensity(gx, gy, gz);
                            minDens = std::min(minDens, density);
                            maxDens = std::max(maxDens, density);
                        }
                    }
                }
                
                brick.minDensity = minDens;
                brick.maxDensity = maxDens;
                brick.flags = (maxDens > 0.001f) ? 1u : 0u;  // Active if non-empty
            }
        }
    }
}

void HeterogeneousVolume::fillWithNoise(float baseFrequency, int octaves, float persistence) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, 1000.0f);
    float offsetX = dist(gen);
    float offsetY = dist(gen);
    float offsetZ = dist(gen);
    
    // Simple 3D noise using sin-based pseudo-noise
    auto noise3D = [](float x, float y, float z) -> float {
        float val = std::sin(x * 1.23f + y * 4.56f) * std::cos(y * 2.34f + z * 5.67f) * 
                   std::sin(z * 3.45f + x * 6.78f);
        return val * 0.5f + 0.5f;
    };
    
    for (uint32_t z = 0; z < resolution.z; ++z) {
        for (uint32_t y = 0; y < resolution.y; ++y) {
            for (uint32_t x = 0; x < resolution.x; ++x) {
                float fx = float(x) / float(resolution.x) + offsetX;
                float fy = float(y) / float(resolution.y) + offsetY;
                float fz = float(z) / float(resolution.z) + offsetZ;
                
                float value = 0.0f;
                float amplitude = 1.0f;
                float frequency = baseFrequency;
                float totalAmp = 0.0f;
                
                for (int o = 0; o < octaves; ++o) {
                    value += amplitude * noise3D(fx * frequency, fy * frequency, fz * frequency);
                    totalAmp += amplitude;
                    amplitude *= persistence;
                    frequency *= 2.0f;
                }
                
                value /= totalAmp;
                
                // Add falloff towards edges
                glm::vec3 centered = glm::vec3(fx - 0.5f, fy - 0.5f, fz - 0.5f) * 2.0f;
                float distSq = glm::dot(centered, centered);
                float falloff = std::max(0.0f, 1.0f - distSq);
                
                setDensity(x, y, z, value * falloff);
            }
        }
    }
    
    buildBricks();
}

// ============================================================================
// HETEROGENEOUS VOLUMES SYSTEM
// ============================================================================

HeterogeneousVolumesSystem::HeterogeneousVolumesSystem(VulkanContext& context)
    : context_(context) {
}

HeterogeneousVolumesSystem::~HeterogeneousVolumesSystem() {
    shutdown();
}

bool HeterogeneousVolumesSystem::initialize(uint32_t width, uint32_t height,
                                           const HeterogeneousVolumesConfig& config) {
    screenWidth_ = width;
    screenHeight_ = height;
    config_ = config;
    
    atlasSlotUsed_.resize(MAX_HETEROGENEOUS_VOLUMES, false);
    
    createResources();
    createAtlas();
    createPipelines();
    
    return true;
}

void HeterogeneousVolumesSystem::shutdown() {
    VkDevice device = context_.getDevice();
    
    vkDeviceWaitIdle(device);
    
    // Cleanup pipelines
    if (raymarchPipeline_) vkDestroyPipeline(device, raymarchPipeline_, nullptr);
    if (compositePipeline_) vkDestroyPipeline(device, compositePipeline_, nullptr);
    if (lumenInjectPipeline_) vkDestroyPipeline(device, lumenInjectPipeline_, nullptr);
    if (computeLayout_) vkDestroyPipelineLayout(device, computeLayout_, nullptr);
    if (descriptorLayout_) vkDestroyDescriptorSetLayout(device, descriptorLayout_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    
    // Cleanup samplers
    if (linearSampler_) vkDestroySampler(device, linearSampler_, nullptr);
    if (volumeSampler_) vkDestroySampler(device, volumeSampler_, nullptr);
    
    // Cleanup images
    auto destroyImage = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (img) vkDestroyImage(device, img, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        view = VK_NULL_HANDLE;
        img = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    };
    
    destroyImage(volumeAtlas_, volumeAtlasMemory_, volumeAtlasView_);
    destroyImage(scatteringImage_, scatteringMemory_, scatteringView_);
    destroyImage(transmittanceImage_, transmittanceMemory_, transmittanceView_);
    
    for (int i = 0; i < 2; ++i) {
        destroyImage(historyImages_[i], historyMemory_[i], historyViews_[i]);
    }
    
    // Cleanup buffers
    if (volumeBuffer_) vkDestroyBuffer(device, volumeBuffer_, nullptr);
    if (volumeBufferMemory_) vkFreeMemory(device, volumeBufferMemory_, nullptr);
    if (uniformBuffer_) vkDestroyBuffer(device, uniformBuffer_, nullptr);
    if (uniformMemory_) vkFreeMemory(device, uniformMemory_, nullptr);
    
    volumes_.clear();
    idToIndex_.clear();
}

void HeterogeneousVolumesSystem::resize(uint32_t width, uint32_t height) {
    screenWidth_ = width;
    screenHeight_ = height;
    
    VkDevice device = context_.getDevice();
    
    // Recreate screen-sized resources
    auto destroyImage = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (img) vkDestroyImage(device, img, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
    };
    
    destroyImage(scatteringImage_, scatteringMemory_, scatteringView_);
    destroyImage(transmittanceImage_, transmittanceMemory_, transmittanceView_);
    for (int i = 0; i < 2; ++i) {
        destroyImage(historyImages_[i], historyMemory_[i], historyViews_[i]);
    }
    
    createResources();
}

void HeterogeneousVolumesSystem::setConfig(const HeterogeneousVolumesConfig& config) {
    config_ = config;
}

uint32_t HeterogeneousVolumesSystem::createVolume(const std::string& name) {
    auto volume = std::make_unique<HeterogeneousVolume>();
    volume->id = nextVolumeId_++;
    volume->name = name;
    volume->localBounds = { glm::vec3(-0.5f), glm::vec3(0.5f) };
    volume->setResolution(64, 64, 64);
    
    // Find free atlas slot
    for (uint32_t i = 0; i < atlasSlotUsed_.size(); ++i) {
        if (!atlasSlotUsed_[i]) {
            volume->atlasSlot = i;
            atlasSlotUsed_[i] = true;
            break;
        }
    }
    
    uint32_t id = volume->id;
    idToIndex_[id] = static_cast<uint32_t>(volumes_.size());
    volumes_.push_back(std::move(volume));
    
    return id;
}

void HeterogeneousVolumesSystem::updateVolume(uint32_t id, const HeterogeneousVolume& volume) {
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end()) return;
    
    HeterogeneousVolume* existing = volumes_[it->second].get();
    
    // Copy fields but preserve id and atlas slot
    uint32_t savedId = existing->id;
    uint32_t savedSlot = existing->atlasSlot;
    *existing = volume;
    existing->id = savedId;
    existing->atlasSlot = savedSlot;
    existing->isDirty = true;
}

void HeterogeneousVolumesSystem::deleteVolume(uint32_t id) {
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end()) return;
    
    uint32_t index = it->second;
    
    // Free atlas slot
    if (volumes_[index]->atlasSlot < atlasSlotUsed_.size()) {
        atlasSlotUsed_[volumes_[index]->atlasSlot] = false;
    }
    
    // Remove and update indices
    volumes_.erase(volumes_.begin() + index);
    idToIndex_.erase(it);
    
    for (auto& pair : idToIndex_) {
        if (pair.second > index) {
            pair.second--;
        }
    }
}

HeterogeneousVolume* HeterogeneousVolumesSystem::getVolume(uint32_t id) {
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end()) return nullptr;
    return volumes_[it->second].get();
}

const HeterogeneousVolume* HeterogeneousVolumesSystem::getVolume(uint32_t id) const {
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end()) return nullptr;
    return volumes_[it->second].get();
}

void HeterogeneousVolumesSystem::beginFrame(const glm::mat4& view, const glm::mat4& proj,
                                           const glm::vec3& cameraPos) {
    prevViewProjMatrix_ = viewProjMatrix_;
    viewMatrix_ = view;
    projMatrix_ = proj;
    viewProjMatrix_ = proj * view;
    cameraPosition_ = cameraPos;
    
    frameIndex_++;
    currentHistoryIndex_ = frameIndex_ % 2;
}

void HeterogeneousVolumesSystem::updateAtlas(VkCommandBuffer cmd) {
    // Upload dirty volumes to GPU atlas
    for (auto& volume : volumes_) {
        if (volume->isDirty) {
            uploadVolumeToAtlas(*volume);
            volume->isDirty = false;
        }
    }
    
    updateVolumeBuffer();
}

void HeterogeneousVolumesSystem::raymarch(VkCommandBuffer cmd,
                                          VkImageView depthBuffer,
                                          VkImageView shadowMap) {
    if (volumes_.empty()) return;
    
    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, raymarchPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           computeLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    
    // Dispatch
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Barrier for results
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void HeterogeneousVolumesSystem::injectToLumen(VkCommandBuffer cmd,
                                               VkBuffer radianceCacheBuffer) {
    if (!config_.injectToLumen || volumes_.empty()) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lumenInjectPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           computeLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    
    // Dispatch for radiance cache injection
    uint32_t numProbes = 1024; // TODO: Get from Lumen system
    uint32_t groups = (numProbes + 63) / 64;
    vkCmdDispatch(cmd, groups, 1, 1);
}

void HeterogeneousVolumesSystem::composite(VkCommandBuffer cmd,
                                           VkImageView sceneColor,
                                           VkImageView outputColor) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compositePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           computeLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void HeterogeneousVolumesSystem::createResources() {
    // Create 2D result textures
    createImage2D(scatteringImage_, scatteringMemory_, scatteringView_,
                 screenWidth_, screenHeight_, VK_FORMAT_R16G16B16A16_SFLOAT,
                 VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    createImage2D(transmittanceImage_, transmittanceMemory_, transmittanceView_,
                 screenWidth_, screenHeight_, VK_FORMAT_R16G16B16A16_SFLOAT,
                 VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // History buffers for temporal
    for (int i = 0; i < 2; ++i) {
        createImage2D(historyImages_[i], historyMemory_[i], historyViews_[i],
                     screenWidth_, screenHeight_, VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    
    // Create buffers
    VkDevice device = context_.getDevice();
    
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = sizeof(GPUHeterogeneousVolume) * MAX_HETEROGENEOUS_VOLUMES;
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &bufInfo, nullptr, &volumeBuffer_);
    
    bufInfo.size = sizeof(VolumeUniforms);
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    vkCreateBuffer(device, &bufInfo, nullptr, &uniformBuffer_);
    
    // Allocate memory (simplified - real implementation would use allocator)
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, volumeBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = 0; // TODO: Find proper memory type
    vkAllocateMemory(device, &allocInfo, nullptr, &volumeBufferMemory_);
    vkBindBufferMemory(device, volumeBuffer_, volumeBufferMemory_, 0);
    
    vkGetBufferMemoryRequirements(device, uniformBuffer_, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    vkAllocateMemory(device, &allocInfo, nullptr, &uniformMemory_);
    vkBindBufferMemory(device, uniformBuffer_, uniformMemory_, 0);
    vkMapMemory(device, uniformMemory_, 0, sizeof(VolumeUniforms), 0, &uniformMapped_);
    
    // Create samplers
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
    
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    vkCreateSampler(device, &samplerInfo, nullptr, &volumeSampler_);
}

void HeterogeneousVolumesSystem::createAtlas() {
    createImage3D(volumeAtlas_, volumeAtlasMemory_, volumeAtlasView_,
                 VOLUME_ATLAS_SIZE, VOLUME_ATLAS_SIZE, VOLUME_ATLAS_SIZE,
                 VK_FORMAT_R16_SFLOAT,
                 VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | 
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT);
}

void HeterogeneousVolumesSystem::createPipelines() {
    VkDevice device = context_.getDevice();
    
    // Descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
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
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &computeLayout_);
    
    // Load shaders and create pipelines
    VkShaderModule raymarchShader = loadShader("shaders/volume_raymarch.comp.spv");
    VkShaderModule compositeShader = loadShader("shaders/volume_composite.comp.spv");
    VkShaderModule lumenShader = loadShader("shaders/volume_lumen_inject.comp.spv");
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = computeLayout_;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.pName = "main";
    
    if (raymarchShader) {
        pipelineInfo.stage.module = raymarchShader;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &raymarchPipeline_);
        vkDestroyShaderModule(device, raymarchShader, nullptr);
    }
    
    if (compositeShader) {
        pipelineInfo.stage.module = compositeShader;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipeline_);
        vkDestroyShaderModule(device, compositeShader, nullptr);
    }
    
    if (lumenShader) {
        pipelineInfo.stage.module = lumenShader;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &lumenInjectPipeline_);
        vkDestroyShaderModule(device, lumenShader, nullptr);
    }
    
    // Descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8},
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

void HeterogeneousVolumesSystem::updateVolumeBuffer() {
    std::vector<GPUHeterogeneousVolume> gpuData(volumes_.size());
    
    for (size_t i = 0; i < volumes_.size(); ++i) {
        const HeterogeneousVolume& vol = *volumes_[i];
        GPUHeterogeneousVolume& gpu = gpuData[i];
        
        gpu.worldMatrix = vol.worldMatrix;
        gpu.invWorldMatrix = vol.invWorldMatrix;
        
        gpu.boundsMin = glm::vec4(vol.localBounds.min, vol.densityScale);
        gpu.boundsMax = glm::vec4(vol.localBounds.max, vol.temperatureScale);
        
        gpu.scatteringAbsorption = glm::vec4(vol.scattering, vol.absorption.r);
        gpu.absorptionEmission = glm::vec4(vol.absorption.g, vol.absorption.b, 
                                           vol.emission.r, vol.emission.g);
        gpu.emissionPhase = glm::vec4(vol.emission.b, vol.phaseG, 
                                      float(vol.phaseFunction),
                                      vol.castsShadow ? 1.0f : 0.0f);
        
        gpu.resolutionBrickCount = glm::uvec4(vol.resolution, 
            vol.brickCount.x * vol.brickCount.y * vol.brickCount.z);
        gpu.atlasParams = glm::uvec4(vol.atlasSlot, 0, 0, 0);
    }
    
    // Copy to GPU (simplified - real implementation would use staging buffer)
    if (!gpuData.empty()) {
        void* mapped;
        vkMapMemory(context_.getDevice(), volumeBufferMemory_, 0, 
                   gpuData.size() * sizeof(GPUHeterogeneousVolume), 0, &mapped);
        memcpy(mapped, gpuData.data(), gpuData.size() * sizeof(GPUHeterogeneousVolume));
        vkUnmapMemory(context_.getDevice(), volumeBufferMemory_);
    }
    
    // Update uniforms
    VolumeUniforms uniforms;
    uniforms.viewMatrix = viewMatrix_;
    uniforms.projMatrix = projMatrix_;
    uniforms.invViewMatrix = glm::inverse(viewMatrix_);
    uniforms.invProjMatrix = glm::inverse(projMatrix_);
    uniforms.viewProjMatrix = viewProjMatrix_;
    uniforms.prevViewProjMatrix = prevViewProjMatrix_;
    uniforms.cameraPosition = glm::vec4(cameraPosition_, 1.0f);
    uniforms.screenParams = glm::vec4(screenWidth_, screenHeight_, 
                                      1.0f / screenWidth_, 1.0f / screenHeight_);
    uniforms.volumeCount = static_cast<uint32_t>(volumes_.size());
    uniforms.maxSteps = config_.maxRaymarchSteps;
    uniforms.stepSize = config_.stepSize;
    uniforms.time = 0.0f; // TODO: Get from engine time
    uniforms.flags = (config_.enableTemporal ? 1 : 0) | 
                    (config_.useJitter ? 2 : 0) |
                    (config_.enableShadows ? 4 : 0);
    uniforms.temporalBlend = config_.temporalBlend;
    uniforms.jitterScale = config_.useBlueNoise ? 1.0f : 0.5f;
    uniforms.frameIndex = frameIndex_;
    
    memcpy(uniformMapped_, &uniforms, sizeof(uniforms));
}

void HeterogeneousVolumesSystem::uploadVolumeToAtlas(HeterogeneousVolume& volume) {
    // TODO: Implement actual upload using staging buffer and vkCmdCopyBufferToImage
    // This would upload the densityData to the 3D atlas at the volume's atlasSlot
}

void HeterogeneousVolumesSystem::createImage2D(VkImage& image, VkDeviceMemory& memory, 
                                               VkImageView& view,
                                               uint32_t width, uint32_t height,
                                               VkFormat format, VkImageUsageFlags usage) {
    VkDevice device = context_.getDevice();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
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
    allocInfo.memoryTypeIndex = 0; // TODO: Find device local memory type
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    vkBindImageMemory(device, image, memory, 0);
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &viewInfo, nullptr, &view);
}

void HeterogeneousVolumesSystem::createImage3D(VkImage& image, VkDeviceMemory& memory, 
                                               VkImageView& view,
                                               uint32_t width, uint32_t height, uint32_t depth,
                                               VkFormat format, VkImageUsageFlags usage) {
    VkDevice device = context_.getDevice();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, depth};
    imageInfo.mipLevels = 1;
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
    allocInfo.memoryTypeIndex = 0; // TODO: Find device local memory type
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    vkBindImageMemory(device, image, memory, 0);
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &viewInfo, nullptr, &view);
}

VkShaderModule HeterogeneousVolumesSystem::loadShader(const std::string& path) {
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

// ============================================================================
// BLACKBODY IMPLEMENTATION
// ============================================================================

namespace Blackbody {

glm::vec3 temperatureToRGB(float kelvin) {
    // Based on approximation by Tanner Helland
    // Optimized for 1000K to 40000K range
    
    kelvin = std::clamp(kelvin, 1000.0f, 40000.0f);
    float temp = kelvin / 100.0f;
    
    float r, g, b;
    
    if (temp <= 66.0f) {
        r = 255.0f;
        g = 99.4708025861f * std::log(temp) - 161.1195681661f;
    } else {
        r = 329.698727446f * std::pow(temp - 60.0f, -0.1332047592f);
        g = 288.1221695283f * std::pow(temp - 60.0f, -0.0755148492f);
    }
    
    if (temp >= 66.0f) {
        b = 255.0f;
    } else if (temp <= 19.0f) {
        b = 0.0f;
    } else {
        b = 138.5177312231f * std::log(temp - 10.0f) - 305.0447927307f;
    }
    
    r = std::clamp(r, 0.0f, 255.0f) / 255.0f;
    g = std::clamp(g, 0.0f, 255.0f) / 255.0f;
    b = std::clamp(b, 0.0f, 255.0f) / 255.0f;
    
    return glm::vec3(r, g, b);
}

float emissionIntensity(float kelvin, float baseIntensity) {
    // Stefan-Boltzmann law approximation
    float normalized = kelvin / 6500.0f;
    return baseIntensity * std::pow(normalized, 4.0f);
}

} // namespace Blackbody

} // namespace Sanic
