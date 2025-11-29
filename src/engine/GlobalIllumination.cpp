/**
 * GlobalIllumination.cpp
 * 
 * Implementation of the global illumination system coordinator.
 */

#include "GlobalIllumination.h"
#include "ScreenProbes.h"
#include "RadianceCache.h"
#include "VulkanContext.h"
#include <fstream>
#include <cmath>

GlobalIllumination::~GlobalIllumination() {
    cleanup();
}

bool GlobalIllumination::initialize(VulkanContext* context,
                                     uint32_t screenWidth,
                                     uint32_t screenHeight,
                                     const GIConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    
    // Create subsystems
    screenProbes_ = std::make_unique<ScreenProbes>();
    radianceCache_ = std::make_unique<RadianceCache>();
    
    ScreenProbeConfig probeConfig{};
    probeConfig.tileSize = 8;
    probeConfig.octahedralResolution = 8;
    probeConfig.raysPerProbe = config.raysPerProbe;
    probeConfig.maxProbesPerTile = 4;
    
    if (!screenProbes_->initialize(context, screenWidth, screenHeight, probeConfig)) { cleanup(); return false; }
    
    RadianceCacheConfig cacheConfig{};
    cacheConfig.clipMapLevels = config.clipMapLevels;
    cacheConfig.baseCellSize = config.baseCellSize;
    
    if (!radianceCache_->initialize(context, cacheConfig)) { cleanup(); return false; }
    
    if (!createOutputTextures()) { cleanup(); return false; }
    if (!createPipelines()) { cleanup(); return false; }
    
    initialized_ = true;
    return true;
}

void GlobalIllumination::cleanup() {
    if (!context_) return;
    VkDevice device = context_->getDevice();
    
    // Subsystems
    if (screenProbes_) screenProbes_->cleanup();
    if (radianceCache_) radianceCache_->cleanup();
    
    // Pipelines
    if (finalGatherPipeline_) vkDestroyPipeline(device, finalGatherPipeline_, nullptr);
    if (finalGatherLayout_) vkDestroyPipelineLayout(device, finalGatherLayout_, nullptr);
    if (temporalFilterPipeline_) vkDestroyPipeline(device, temporalFilterPipeline_, nullptr);
    if (temporalFilterLayout_) vkDestroyPipelineLayout(device, temporalFilterLayout_, nullptr);
    if (compositePipeline_) vkDestroyPipeline(device, compositePipeline_, nullptr);
    if (compositeLayout_) vkDestroyPipelineLayout(device, compositeLayout_, nullptr);
    
    // Descriptors
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (descLayout_) vkDestroyDescriptorSetLayout(device, descLayout_, nullptr);
    if (giSampler_) vkDestroySampler(device, giSampler_, nullptr);
    
    // Output images
    if (diffuseGIView_) vkDestroyImageView(device, diffuseGIView_, nullptr);
    if (diffuseGIImage_) vkDestroyImage(device, diffuseGIImage_, nullptr);
    if (diffuseGIMemory_) vkFreeMemory(device, diffuseGIMemory_, nullptr);
    
    if (specularGIView_) vkDestroyImageView(device, specularGIView_, nullptr);
    if (specularGIImage_) vkDestroyImage(device, specularGIImage_, nullptr);
    if (specularGIMemory_) vkFreeMemory(device, specularGIMemory_, nullptr);
    
    if (aoView_) vkDestroyImageView(device, aoView_, nullptr);
    if (aoImage_) vkDestroyImage(device, aoImage_, nullptr);
    if (aoMemory_) vkFreeMemory(device, aoMemory_, nullptr);
    
    if (bentNormalsView_) vkDestroyImageView(device, bentNormalsView_, nullptr);
    if (bentNormalsImage_) vkDestroyImage(device, bentNormalsImage_, nullptr);
    if (bentNormalsMemory_) vkFreeMemory(device, bentNormalsMemory_, nullptr);
    
    // History buffers
    for (int i = 0; i < 2; i++) {
        if (historyDiffuseView_[i]) vkDestroyImageView(device, historyDiffuseView_[i], nullptr);
        if (historyDiffuse_[i]) vkDestroyImage(device, historyDiffuse_[i], nullptr);
        if (historyDiffuseMemory_[i]) vkFreeMemory(device, historyDiffuseMemory_[i], nullptr);
        
        if (historySpecularView_[i]) vkDestroyImageView(device, historySpecularView_[i], nullptr);
        if (historySpecular_[i]) vkDestroyImage(device, historySpecular_[i], nullptr);
        if (historySpecularMemory_[i]) vkFreeMemory(device, historySpecularMemory_[i], nullptr);
    }
    
    initialized_ = false;
}

void GlobalIllumination::resize(uint32_t width, uint32_t height) {
    if (screenWidth_ == width && screenHeight_ == height) return;
    screenWidth_ = width;
    screenHeight_ = height;
    // Would recreate size-dependent resources
}

bool GlobalIllumination::createOutputTextures() {
    VkDevice device = context_->getDevice();
    
    auto findMemoryType = [this](uint32_t typeFilter, VkMemoryPropertyFlags properties) -> uint32_t {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(context_->getPhysicalDevice(), &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    };
    
    auto createImage = [&](VkImage& image, VkDeviceMemory& memory, VkImageView& view, VkFormat format) -> bool {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {screenWidth_, screenHeight_, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        
        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) return false;
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, image, &memReqs);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, image, memory, 0);
        
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        return vkCreateImageView(device, &viewInfo, nullptr, &view) == VK_SUCCESS;
    };
    
    // Main outputs
    if (!createImage(diffuseGIImage_, diffuseGIMemory_, diffuseGIView_, VK_FORMAT_R16G16B16A16_SFLOAT)) return false;
    if (!createImage(specularGIImage_, specularGIMemory_, specularGIView_, VK_FORMAT_R16G16B16A16_SFLOAT)) return false;
    if (!createImage(aoImage_, aoMemory_, aoView_, VK_FORMAT_R8_UNORM)) return false;
    if (!createImage(bentNormalsImage_, bentNormalsMemory_, bentNormalsView_, VK_FORMAT_R16G16B16A16_SFLOAT)) return false;
    
    // History for temporal
    for (int i = 0; i < 2; i++) {
        if (!createImage(historyDiffuse_[i], historyDiffuseMemory_[i], historyDiffuseView_[i], VK_FORMAT_R16G16B16A16_SFLOAT)) return false;
        if (!createImage(historySpecular_[i], historySpecularMemory_[i], historySpecularView_[i], VK_FORMAT_R16G16B16A16_SFLOAT)) return false;
    }
    
    // Update output struct
    output_.diffuseGI = diffuseGIView_;
    output_.specularGI = specularGIView_;
    output_.ao = aoView_;
    output_.bentNormals = bentNormalsView_;
    
    return true;
}

bool GlobalIllumination::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Descriptor set layout for GI
    VkDescriptorSetLayoutBinding bindings[16] = {};
    for (int i = 0; i < 8; i++) {
        bindings[i] = {(uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    for (int i = 8; i < 12; i++) {
        bindings[i] = {(uint32_t)i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    for (int i = 12; i < 16; i++) {
        bindings[i] = {(uint32_t)i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 16;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descLayout_) != VK_SUCCESS) return false;
    
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = 128;
    
    VkPipelineLayoutCreateInfo pipeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &descLayout_;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pushRange;
    
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &finalGatherLayout_) != VK_SUCCESS) return false;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &temporalFilterLayout_) != VK_SUCCESS) return false;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &compositeLayout_) != VK_SUCCESS) return false;
    
    // Create compute pipelines
    VkShaderModule finalGatherModule = VK_NULL_HANDLE;
    VkShaderModule temporalModule = VK_NULL_HANDLE;
    VkShaderModule compositeModule = VK_NULL_HANDLE;
    
    if (!loadShader("build/shaders/final_gather.comp.spv", finalGatherModule)) return false;
    if (!loadShader("build/shaders/gi_temporal.comp.spv", temporalModule)) return false;
    if (!loadShader("build/shaders/gi_composite.comp.spv", compositeModule)) return false;
    
    VkComputePipelineCreateInfo computeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    computeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    computeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeInfo.stage.pName = "main";
    
    computeInfo.stage.module = finalGatherModule;
    computeInfo.layout = finalGatherLayout_;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &finalGatherPipeline_) != VK_SUCCESS) return false;
    
    computeInfo.stage.module = temporalModule;
    computeInfo.layout = temporalFilterLayout_;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &temporalFilterPipeline_) != VK_SUCCESS) return false;
    
    computeInfo.stage.module = compositeModule;
    computeInfo.layout = compositeLayout_;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &compositePipeline_) != VK_SUCCESS) return false;
    
    vkDestroyShaderModule(device, finalGatherModule, nullptr);
    vkDestroyShaderModule(device, temporalModule, nullptr);
    vkDestroyShaderModule(device, compositeModule, nullptr);
    
    return true;
}

bool GlobalIllumination::loadShader(const std::string& path, VkShaderModule& outModule) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    size_t size = file.tellg();
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), size);
    
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = size;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    return vkCreateShaderModule(context_->getDevice(), &createInfo, nullptr, &outModule) == VK_SUCCESS;
}

void GlobalIllumination::update(VkCommandBuffer cmd,
                                 const glm::mat4& view,
                                 const glm::mat4& proj,
                                 const glm::vec3& cameraPos,
                                 float deltaTime) {
    // Store for temporal reprojection
    prevViewProj_ = prevView_ * prevProj_;
    prevView_ = view;
    prevProj_ = proj;
    
    // Update radiance cache with camera position
    if (radianceCache_) {
        radianceCache_->update(cmd, cameraPos, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 0);
    }
    
    frameIndex_++;
}

void GlobalIllumination::computeGI(VkCommandBuffer cmd,
                                    VkImageView gbufferAlbedo,
                                    VkImageView gbufferNormal,
                                    VkImageView gbufferDepth,
                                    VkImageView gbufferRoughness,
                                    VkBuffer lightBuffer,
                                    uint32_t lightCount) {
    traceScreenProbes(cmd);
    updateRadianceCache(cmd);
    finalGather(cmd);
    temporalFilter(cmd);
}

void GlobalIllumination::applyGI(VkCommandBuffer cmd,
                                  VkImageView directLighting,
                                  VkImageView outputHDR) {
    compositeGI(cmd);
}

void GlobalIllumination::injectEmissives(VkCommandBuffer cmd,
                                          VkImageView emissiveBuffer) {
    // Inject emissive surfaces into radiance cache
}

void GlobalIllumination::updateSky(VkCommandBuffer cmd,
                                    VkImageView skybox,
                                    const glm::vec3& sunDirection,
                                    const glm::vec3& sunColor) {
    // Update sky lighting contribution
}

void GlobalIllumination::setConfig(const GIConfig& config) {
    config_ = config;
}

void GlobalIllumination::traceScreenProbes(VkCommandBuffer cmd) {
    if (!screenProbes_) return;
    
    // Dispatch probe tracing
    screenProbes_->placeProbes(cmd, VK_NULL_HANDLE, VK_NULL_HANDLE, glm::mat4(1.0f), glm::mat4(1.0f));
    screenProbes_->traceProbes(cmd, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 0);
    screenProbes_->filterProbes(cmd);
}

void GlobalIllumination::updateRadianceCache(VkCommandBuffer cmd) {
    if (!radianceCache_ || !screenProbes_) return;
    
    radianceCache_->injectProbes(cmd, screenProbes_->getProbeBuffer(), screenProbes_->getProbeCount());
}

void GlobalIllumination::finalGather(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, finalGatherPipeline_);
    
    struct PushConstants {
        uint32_t width;
        uint32_t height;
        uint32_t frameIndex;
        float gatherRadius;
        uint32_t gatherSamples;
        float aoStrength;
        float pad0;
        float pad1;
    } push;
    
    push.width = screenWidth_;
    push.height = screenHeight_;
    push.frameIndex = frameIndex_;
    push.gatherRadius = config_.gatherRadius;
    push.gatherSamples = config_.gatherSamples;
    push.aoStrength = config_.aoStrength;
    
    vkCmdPushConstants(cmd, finalGatherLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void GlobalIllumination::temporalFilter(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporalFilterPipeline_);
    
    struct PushConstants {
        uint32_t width;
        uint32_t height;
        uint32_t historyIndex;
        float temporalWeight;
    } push;
    
    push.width = screenWidth_;
    push.height = screenHeight_;
    push.historyIndex = frameIndex_ % 2;
    push.temporalWeight = config_.temporalWeight;
    
    vkCmdPushConstants(cmd, temporalFilterLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void GlobalIllumination::compositeGI(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compositePipeline_);
    
    struct PushConstants {
        uint32_t width;
        uint32_t height;
        uint32_t debugMode;
        float skyIntensity;
        float emissiveMultiplier;
        float pad0;
        float pad1;
        float pad2;
    } push;
    
    push.width = screenWidth_;
    push.height = screenHeight_;
    push.debugMode = config_.debugMode;
    push.skyIntensity = config_.skyIntensity;
    push.emissiveMultiplier = config_.emissiveMultiplier;
    
    vkCmdPushConstants(cmd, compositeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    
    uint32_t groupsX = (screenWidth_ + 7) / 8;
    uint32_t groupsY = (screenHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}
