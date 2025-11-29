/**
 * FinalRenderer.cpp
 * 
 * Implementation of the final rendering pipeline that integrates
 * all Nanite-style and Lumen-style rendering systems.
 * 
 * Turn 40-42: Final Integration
 */

#include "FinalRenderer.h"
#include "VulkanContext.h"
#include "ClusterHierarchy.h"
#include "ClusterCullingPipeline.h"
#include "HZBPipeline.h"
#include "VisBufferRenderer.h"
#include "SoftwareRasterizerPipeline.h"
#include "MaterialSystem.h"
#include "TemporalSystem.h"
#include "SurfaceCache.h"
#include "ScreenSpaceTracing.h"
#include "SDFGenerator.h"
#include "ScreenProbes.h"
#include "RadianceCache.h"
#include "GlobalIllumination.h"
#include "VirtualShadowMaps.h"
#include "RayTracedShadows.h"
#include "PostProcess.h"

#include <cstring>
#include <fstream>
#include <array>

FinalRenderer::FinalRenderer() = default;

FinalRenderer::~FinalRenderer() {
    cleanup();
}

bool FinalRenderer::initialize(VulkanContext* context, const RenderConfig& config) {
    context_ = context;
    config_ = config;
    
    // Get timestamp period for profiling
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(context_->getPhysicalDevice(), &props);
    timestampPeriod_ = props.limits.timestampPeriod;
    
    // Create timestamp query pool
    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 16; // Multiple timestamps per frame
    
    if (vkCreateQueryPool(context_->getDevice(), &queryInfo, nullptr, &timestampPool_) != VK_SUCCESS) {
        return false;
    }
    
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
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.anisotropyEnable = VK_TRUE;
    
    if (vkCreateSampler(context_->getDevice(), &samplerInfo, nullptr, &linearSampler_) != VK_SUCCESS) {
        return false;
    }
    
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.anisotropyEnable = VK_FALSE;
    
    if (vkCreateSampler(context_->getDevice(), &samplerInfo, nullptr, &nearestSampler_) != VK_SUCCESS) {
        return false;
    }
    
    // Create G-Buffers
    if (!createGBuffers()) {
        return false;
    }
    
    // Create uniform buffers
    if (!createUniformBuffers()) {
        return false;
    }
    
    // Create light buffer
    if (!createLightBuffer()) {
        return false;
    }
    
    // Create descriptor sets
    if (!createDescriptorSets()) {
        return false;
    }
    
    // Create pipelines
    if (!createPipelines()) {
        return false;
    }
    
    // Note: Subsystem initialization is deferred - classes use constructor injection
    // For this integration layer, we use lazy initialization patterns since existing
    // classes have their own constructors. In practice, these would be configured
    // through a scene/world setup phase rather than direct construction here.
    //
    // The FinalRenderer coordinates existing subsystems that are constructed elsewhere
    // (e.g., in main.cpp or a Scene class) and passed via setters or injection.
    //
    // For now, subsystems remain null until set externally via setters.
    // This matches how Unreal's renderer coordinates systems created by the engine.
    
    initialized_ = true;
    return true;
}

void FinalRenderer::cleanup() {
    if (!context_) return;
    
    VkDevice device = context_->getDevice();
    vkDeviceWaitIdle(device);
    
    // Note: Subsystems are not owned by FinalRenderer, don't reset them
    // They are owned externally and cleanup is handled by their owners
    
    // Cleanup Vulkan resources
    auto destroyImage = [device](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (img) vkDestroyImage(device, img, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        view = VK_NULL_HANDLE;
        img = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    };
    
    destroyImage(depthImage_, depthMemory_, depthView_);
    destroyImage(visBufferImage_, visBufferMemory_, visBufferView_);
    destroyImage(normalImage_, normalMemory_, normalView_);
    destroyImage(albedoImage_, albedoMemory_, albedoView_);
    destroyImage(materialImage_, materialMemory_, materialView_);
    destroyImage(velocityImage_, velocityMemory_, velocityView_);
    destroyImage(hdrImage_, hdrMemory_, hdrView_);
    destroyImage(finalOutputImage_, finalOutputMemory_, finalOutputView_);
    
    if (frameUniformBuffer_) {
        vkDestroyBuffer(device, frameUniformBuffer_, nullptr);
        vkFreeMemory(device, frameUniformMemory_, nullptr);
    }
    
    if (lightBuffer_) {
        vkDestroyBuffer(device, lightBuffer_, nullptr);
        vkFreeMemory(device, lightMemory_, nullptr);
    }
    
    if (lightingPipeline_) vkDestroyPipeline(device, lightingPipeline_, nullptr);
    if (lightingLayout_) vkDestroyPipelineLayout(device, lightingLayout_, nullptr);
    
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    if (frameDescLayout_) vkDestroyDescriptorSetLayout(device, frameDescLayout_, nullptr);
    if (gbufferDescLayout_) vkDestroyDescriptorSetLayout(device, gbufferDescLayout_, nullptr);
    
    if (linearSampler_) vkDestroySampler(device, linearSampler_, nullptr);
    if (nearestSampler_) vkDestroySampler(device, nearestSampler_, nullptr);
    
    if (timestampPool_) vkDestroyQueryPool(device, timestampPool_, nullptr);
    
    context_ = nullptr;
    initialized_ = false;
}

void FinalRenderer::resize(uint32_t width, uint32_t height) {
    if (!initialized_) return;
    
    config_.width = width;
    config_.height = height;
    
    // Wait for GPU
    vkDeviceWaitIdle(context_->getDevice());
    
    // Recreate size-dependent resources
    VkDevice device = context_->getDevice();
    
    auto destroyImage = [device](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (img) vkDestroyImage(device, img, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        view = VK_NULL_HANDLE;
        img = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    };
    
    destroyImage(depthImage_, depthMemory_, depthView_);
    destroyImage(visBufferImage_, visBufferMemory_, visBufferView_);
    destroyImage(normalImage_, normalMemory_, normalView_);
    destroyImage(albedoImage_, albedoMemory_, albedoView_);
    destroyImage(materialImage_, materialMemory_, materialView_);
    destroyImage(velocityImage_, velocityMemory_, velocityView_);
    destroyImage(hdrImage_, hdrMemory_, hdrView_);
    destroyImage(finalOutputImage_, finalOutputMemory_, finalOutputView_);
    
    createGBuffers();
    
    // Resize subsystems - note: some systems don't have resize methods
    // and need to be recreated. For now, skip resize calls for systems 
    // that don't support it.
    
    if (temporalSystem_) {
        temporalSystem_->resize(width, height);
    }
    
    if (globalIllumination_) {
        globalIllumination_->resize(width, height);
    }
    
    if (postProcess_) {
        postProcess_->resize(width, height);
    }
}

void FinalRenderer::setConfig(const RenderConfig& config) {
    config_ = config;
    // Dynamically toggle features as needed
}

void FinalRenderer::render(const FrameContext& frame) {
    if (!initialized_) return;
    
    VkCommandBuffer cmd = frame.commandBuffer;
    
    // Reset stats
    stats_ = {};
    
    // Reset timestamp queries
    vkCmdResetQueryPool(cmd, timestampPool_, 0, 16);
    
    // Update frame uniforms
    updateFrameUniforms(frame);
    uploadLights(frame);
    
    // Timestamp: Start
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampPool_, 0);
    
    // 1. Geometry Pass (Cluster culling, VisBuffer rendering)
    executeGeometryPass(cmd, frame);
    
    // Timestamp: After geometry
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, 1);
    
    // 2. Shadow Pass
    executeShadowPass(cmd, frame);
    
    // Timestamp: After shadows
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, 2);
    
    // 3. GI Pass
    executeGIPass(cmd, frame);
    
    // Timestamp: After GI
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, 3);
    
    // 4. Lighting Pass
    executeLightingPass(cmd, frame);
    
    // Timestamp: After lighting
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, 4);
    
    // 5. Post-processing Pass
    executePostProcessPass(cmd, frame);
    
    // Timestamp: End
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, 5);
}

void FinalRenderer::updateFrameUniforms(const FrameContext& frame) {
    FrameUniforms uniforms{};
    
    uniforms.viewMatrix = frame.camera.view;
    uniforms.projMatrix = frame.camera.proj;
    uniforms.viewProjMatrix = frame.camera.viewProj;
    uniforms.invViewMatrix = frame.camera.invView;
    uniforms.invProjMatrix = frame.camera.invProj;
    uniforms.invViewProjMatrix = frame.camera.invViewProj;
    uniforms.prevViewProjMatrix = frame.camera.prevViewProj;
    
    uniforms.cameraPosition = glm::vec4(frame.camera.position, frame.totalTime);
    uniforms.cameraParams = glm::vec4(frame.camera.nearPlane, frame.camera.farPlane, 
                                       frame.camera.fov, frame.camera.aspectRatio);
    uniforms.screenSize = glm::vec4(config_.width, config_.height, 
                                     1.0f / config_.width, 1.0f / config_.height);
    
    // Jitter for TAA
    if (config_.enableTemporalAA && temporalSystem_) {
        glm::vec2 jitter = temporalSystem_->getJitterUV();
        uniforms.jitterOffset = glm::vec4(jitter.x, jitter.y, 0.0f, 0.0f);
    }
    
    uniforms.sunDirection = glm::vec4(frame.scene.sunDirection, frame.scene.sunIntensity);
    uniforms.sunColor = glm::vec4(frame.scene.sunColor, 1.0f);
    uniforms.ambientColor = glm::vec4(frame.scene.ambientColor, frame.scene.ambientIntensity);
    
    uniforms.frameIndex = frame.frameIndex;
    uniforms.deltaTime = frame.deltaTime;
    uniforms.totalTime = frame.totalTime;
    
    // Pack feature flags
    uniforms.flags = 0;
    if (config_.enableNanite) uniforms.flags |= 0x1;
    if (config_.enableSoftwareRasterizer) uniforms.flags |= 0x2;
    if (config_.enableGI) uniforms.flags |= 0x4;
    if (config_.enableVSM) uniforms.flags |= 0x8;
    if (config_.enableRayTracedShadows) uniforms.flags |= 0x10;
    
    uniforms.lightCount = static_cast<uint32_t>(frame.scene.lights.size());
    uniforms.clusterCount = stats_.totalClusters;
    
    // Upload
    memcpy(frameUniformMapped_, &uniforms, sizeof(FrameUniforms));
}

void FinalRenderer::uploadLights(const FrameContext& frame) {
    if (frame.scene.lights.empty()) return;
    
    size_t size = frame.scene.lights.size() * sizeof(LightData);
    memcpy(lightBufferMapped_, frame.scene.lights.data(), size);
}

void FinalRenderer::executeGeometryPass(VkCommandBuffer cmd, const FrameContext& frame) {
    // Build HZB from previous frame's depth
    if (config_.enableHZBCulling) {
        buildHZB(cmd);
    }
    
    // Cull clusters using hierarchical culling
    cullClusters(cmd, frame.camera);
    
    // Render visibility buffer with mesh shaders
    renderVisBuffer(cmd);
    
    // Resolve materials from visibility buffer
    resolveMaterials(cmd);
}

void FinalRenderer::executeShadowPass(VkCommandBuffer cmd, const FrameContext& frame) {
    // Virtual shadow map page marking and rendering
    if (config_.enableVSM && virtualShadowMaps_) {
        // Mark visible pages based on screen-space analysis
        glm::mat4 invViewProj = frame.camera.invViewProj;
        virtualShadowMaps_->markVisiblePages(cmd, depthView_, normalView_, invViewProj);
        
        // Render dirty shadow pages - requires geometry buffers from scene
        // The full call requires: vertexBuffer, indexBuffer, drawCommands, drawCount
        // These are managed by the scene and passed through coordination
    }
    
    // Ray-traced shadows for primary light
    if (config_.enableRayTracedShadows && rayTracedShadows_) {
        // Build light shadow settings from scene
        std::vector<LightShadowSettings> lightSettings;
        LightShadowSettings sunSettings{};
        sunSettings.position = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // Directional light
        sunSettings.direction = glm::vec4(glm::normalize(frame.scene.sunDirection), 1000.0f);
        sunSettings.color = glm::vec4(frame.scene.sunColor, frame.scene.sunIntensity);
        sunSettings.shadowParams = glm::vec4(0.5f, 0.5f, 0.001f, 1.0f); // radius, angle, bias, enabled
        lightSettings.push_back(sunSettings);
        
        // Trace shadows using ray tracing
        rayTracedShadows_->trace(cmd, depthView_, normalView_, velocityView_,
                                 frame.camera.viewProj, frame.camera.invViewProj,
                                 frame.camera.prevViewProj, lightSettings);
        rayTracedShadows_->denoise(cmd);
    }
}

void FinalRenderer::executeGIPass(VkCommandBuffer cmd, const FrameContext& frame) {
    if (!config_.enableGI || !globalIllumination_) return;
    
    // Update the main GI system
    globalIllumination_->update(cmd, 
                                frame.camera.view, 
                                frame.camera.proj,
                                frame.camera.position,
                                frame.deltaTime);
    
    // Compute GI contribution - requires G-Buffer access
    // Note: light buffer access is coordinated through the scene
    globalIllumination_->computeGI(cmd,
                                   albedoView_,
                                   normalView_,
                                   depthView_,
                                   materialView_,  // roughness in G-Buffer
                                   lightBuffer_,
                                   static_cast<uint32_t>(frame.scene.lights.size()));
}

void FinalRenderer::executeLightingPass(VkCommandBuffer cmd, const FrameContext& frame) {
    // Memory barrier before lighting
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // Bind lighting pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lightingPipeline_);
    
    // Bind descriptor sets
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lightingLayout_,
                            0, 1, &gbufferDescSet_, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lightingLayout_,
                            3, 1, &frameDescSet_, 0, nullptr);
    
    // Dispatch lighting compute
    uint32_t groupsX = (config_.width + 7) / 8;
    uint32_t groupsY = (config_.height + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Barrier for HDR output
    VkImageMemoryBarrier imgBarrier{};
    imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imgBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imgBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgBarrier.image = hdrImage_;
    imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imgBarrier.subresourceRange.levelCount = 1;
    imgBarrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &imgBarrier);
}

void FinalRenderer::executePostProcessPass(VkCommandBuffer cmd, const FrameContext& frame) {
    if (!postProcess_) return;
    
    // Run full post-processing pipeline
    postProcess_->process(cmd, hdrView_, depthView_, velocityView_, 
                          finalOutputView_, frame.deltaTime);
}

void FinalRenderer::buildHZB(VkCommandBuffer cmd) {
    if (!hzbPipeline_) return;
    
    // HZBPipeline::generateHZB builds hierarchical-Z from depth buffer
    hzbPipeline_->generateHZB(cmd, depthImage_, depthView_, config_.width, config_.height);
}

void FinalRenderer::cullClusters(VkCommandBuffer cmd, const CameraData& camera) {
    if (!clusterCulling_) return;
    
    // Construct culling parameters from camera data
    ClusterCullingPipeline::CullingParams params{};
    params.viewMatrix = camera.view;
    params.projMatrix = camera.proj;
    params.viewProjMatrix = camera.viewProj;
    params.cameraPosition = camera.position;
    params.frustumPlanes[0] = camera.frustumPlanes[0];
    params.frustumPlanes[1] = camera.frustumPlanes[1];
    params.frustumPlanes[2] = camera.frustumPlanes[2];
    params.frustumPlanes[3] = camera.frustumPlanes[3];
    params.frustumPlanes[4] = camera.frustumPlanes[4];
    params.frustumPlanes[5] = camera.frustumPlanes[5];
    params.screenSize = glm::vec2(config_.width, config_.height);
    params.nearPlane = camera.nearPlane;
    params.lodScale = 1.0f;
    params.errorThreshold = 1.0f;
    params.frameIndex = 0; // Set from frame context if needed
    params.flags = config_.enableHZBCulling ? 0x1 : 0x0;
    
    // Perform GPU-driven culling
    clusterCulling_->performCulling(cmd, params);
    
    // Get stats from culling pipeline
    auto cullingStats = clusterCulling_->getStats();
    stats_.totalClusters = cullingStats.clustersTested;
    stats_.visibleClusters = cullingStats.clustersVisible;
    stats_.culledClusters = cullingStats.clustersTested - cullingStats.clustersVisible;
}

void FinalRenderer::renderVisBuffer(VkCommandBuffer cmd) {
    // Note: VisBufferRenderer uses mesh shaders for hardware rasterization
    // The actual render call requires GameObject list which is passed from scene
    // For integration, the FinalRenderer coordinates with existing renderer pattern
    // This is a coordination stub - actual rendering is done through scene management
    
    if (config_.enableSoftwareRasterizer && softwareRasterizer_) {
        // Software rasterizer handles small triangles via compute
        softwareRasterizer_->resetCounters(cmd);
        // Actual rasterization is done after binning in the full pipeline
    }
}

void FinalRenderer::resolveMaterials(VkCommandBuffer cmd) {
    if (!materialSystem_) return;
    
    // Upload any pending material/light changes
    materialSystem_->uploadData(cmd);
    
    // Note: Material binning and evaluation is coordinated with the visibility buffer
    // The actual binMaterials/evaluateMaterials calls require cluster buffer access
    // which is provided through the scene/renderer coordination
}

bool FinalRenderer::createGBuffers() {
    VkDevice device = context_->getDevice();
    
    auto createImage = [this, device](VkFormat format, VkImageUsageFlags usage,
                                       VkImage& image, VkDeviceMemory& memory, 
                                       VkImageView& view, VkImageAspectFlags aspect) -> bool {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {config_.width, config_.height, 1};
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
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits, 
                                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            return false;
        }
        
        vkBindImageMemory(device, image, memory, 0);
        
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        
        return vkCreateImageView(device, &viewInfo, nullptr, &view) == VK_SUCCESS;
    };
    
    // Depth buffer
    if (!createImage(VK_FORMAT_D32_SFLOAT, 
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     depthImage_, depthMemory_, depthView_, VK_IMAGE_ASPECT_DEPTH_BIT)) {
        return false;
    }
    
    // Visibility buffer (R32G32_UINT - cluster ID + triangle ID)
    if (!createImage(VK_FORMAT_R32G32_UINT,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                     visBufferImage_, visBufferMemory_, visBufferView_, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    
    // Normal buffer (RGB10A2 for quality normals)
    if (!createImage(VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                     normalImage_, normalMemory_, normalView_, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    
    // Albedo buffer (RGBA8)
    if (!createImage(VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                     albedoImage_, albedoMemory_, albedoView_, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    
    // Material buffer (RGBA8 - metallic, roughness, AO, emissive)
    if (!createImage(VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                     materialImage_, materialMemory_, materialView_, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    
    // Velocity buffer (RG16F)
    if (!createImage(VK_FORMAT_R16G16_SFLOAT,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                     velocityImage_, velocityMemory_, velocityView_, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    
    // HDR lighting buffer (RGBA16F)
    if (!createImage(VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     hdrImage_, hdrMemory_, hdrView_, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    
    // Final output (RGBA8 for swapchain copy)
    if (!createImage(VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     finalOutputImage_, finalOutputMemory_, finalOutputView_, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    
    return true;
}

bool FinalRenderer::createUniformBuffers() {
    VkDevice device = context_->getDevice();
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(FrameUniforms);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &frameUniformBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, frameUniformBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &frameUniformMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, frameUniformBuffer_, frameUniformMemory_, 0);
    vkMapMemory(device, frameUniformMemory_, 0, sizeof(FrameUniforms), 0, &frameUniformMapped_);
    
    return true;
}

bool FinalRenderer::createLightBuffer() {
    VkDevice device = context_->getDevice();
    const VkDeviceSize maxLights = 1024;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(LightData) * maxLights;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &lightBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, lightBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &lightMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, lightBuffer_, lightMemory_, 0);
    vkMapMemory(device, lightMemory_, 0, bufferInfo.size, 0, &lightBufferMapped_);
    
    return true;
}

bool FinalRenderer::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // Load lighting compute shader
    std::ifstream file("shaders/deferred_lighting.comp.spv", std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> code(fileSize);
    file.seekg(0);
    file.read(code.data(), fileSize);
    file.close();
    
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return false;
    }
    
    // Create pipeline layout
    std::array<VkDescriptorSetLayout, 4> layouts = {
        gbufferDescLayout_, // Set 0: G-Buffer
        gbufferDescLayout_, // Set 1: GI/Shadow inputs (reuse layout for now)
        gbufferDescLayout_, // Set 2: HDR output
        frameDescLayout_    // Set 3: Frame uniforms
    };
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    layoutInfo.pSetLayouts = layouts.data();
    
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &lightingLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }
    
    // Create compute pipeline
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = lightingLayout_;
    
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, 
                                                nullptr, &lightingPipeline_);
    
    vkDestroyShaderModule(device, shaderModule, nullptr);
    
    return result == VK_SUCCESS;
}

bool FinalRenderer::createDescriptorSets() {
    VkDevice device = context_->getDevice();
    
    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 32;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 16;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 8;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 16;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        return false;
    }
    
    // Frame uniform layout
    std::array<VkDescriptorSetLayoutBinding, 2> frameBindings{};
    frameBindings[0].binding = 0;
    frameBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBindings[0].descriptorCount = 1;
    frameBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_ALL_GRAPHICS;
    
    frameBindings[1].binding = 1;
    frameBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    frameBindings[1].descriptorCount = 1;
    frameBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(frameBindings.size());
    layoutInfo.pBindings = frameBindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &frameDescLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // G-Buffer layout
    std::array<VkDescriptorSetLayoutBinding, 6> gbufferBindings{};
    for (int i = 0; i < 6; i++) {
        gbufferBindings[i].binding = i;
        gbufferBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbufferBindings[i].descriptorCount = 1;
        gbufferBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    
    layoutInfo.bindingCount = static_cast<uint32_t>(gbufferBindings.size());
    layoutInfo.pBindings = gbufferBindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &gbufferDescLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Allocate descriptor sets
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &frameDescLayout_;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &frameDescSet_) != VK_SUCCESS) {
        return false;
    }
    
    allocInfo.pSetLayouts = &gbufferDescLayout_;
    if (vkAllocateDescriptorSets(device, &allocInfo, &gbufferDescSet_) != VK_SUCCESS) {
        return false;
    }
    
    // Update frame descriptor set
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = frameUniformBuffer_;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(FrameUniforms);
    
    VkDescriptorBufferInfo lightBufferInfo{};
    lightBufferInfo.buffer = lightBuffer_;
    lightBufferInfo.offset = 0;
    lightBufferInfo.range = VK_WHOLE_SIZE;
    
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = frameDescSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &bufferInfo;
    
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = frameDescSet_;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &lightBufferInfo;
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    
    return true;
}
