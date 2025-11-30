/**
 * PostProcessAdvanced.cpp
 * 
 * Implementation of advanced post-processing system with:
 * - FSR 2.0 / XeSS / DLSS temporal upscaling
 * - Physically-based bokeh depth of field
 * - 3D LUT color grading
 * - Advanced auto-exposure with histogram
 * 
 * Based on Unreal Engine 5's PostProcess architecture
 */

#include "PostProcessAdvanced.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace sanic {

// ============================================================================
// INITIALIZATION
// ============================================================================

AdvancedPostProcess::AdvancedPostProcess(VulkanContext& context, uint32_t renderWidth, uint32_t renderHeight)
    : context_(context)
    , renderWidth_(renderWidth)
    , renderHeight_(renderHeight)
    , displayWidth_(renderWidth)
    , displayHeight_(renderHeight)
    , currentBackend_(UpscalingBackend::TAA)
    , fsr2Context_(nullptr)
    , xessContext_(nullptr)
{
    // Initialize to sane defaults
    upscaleSettings_.backend = UpscalingBackend::TAA;
    upscaleSettings_.qualityMode = 0; // Quality mode
    upscaleSettings_.sharpness = 0.5f;
    upscaleSettings_.mipBias = 0.0f;
    
    dofSettings_.enabled = false;
    dofSettings_.focusDistance = 10.0f;
    dofSettings_.focalLength = 0.05f; // 50mm
    dofSettings_.fStop = 2.8f;
    dofSettings_.sensorWidth = 0.036f; // 36mm (full frame)
    dofSettings_.maxCoC = 32.0f;
    dofSettings_.bladeCount = 6.0f;
    dofSettings_.bladeRotation = 0.0f;
    dofSettings_.bladeCurvature = 0.0f;
    dofSettings_.chromaticAberration = 0.0f;
    dofSettings_.catsEyeAmount = 0.0f;
    
    lutSettings_.enabled = false;
    lutSettings_.lutIntensity = 1.0f;
    lutSettings_.secondaryLUT = VK_NULL_HANDLE;
    lutSettings_.blendFactor = 0.0f;
    lutSettings_.saturation = 1.0f;
    lutSettings_.contrast = 1.0f;
    lutSettings_.gamma = 1.0f;
    lutSettings_.gain = 1.0f;
    lutSettings_.shadows = 1.0f;
    lutSettings_.midtones = 1.0f;
    lutSettings_.highlights = 1.0f;
    
    exposureSettings_.adaptationSpeed = 1.0f;
    exposureSettings_.minExposure = -4.0f;
    exposureSettings_.maxExposure = 4.0f;
    exposureSettings_.targetExposure = 0.0f;
    exposureSettings_.histogramMin = 0.01f;
    exposureSettings_.histogramMax = 100.0f;
    exposureSettings_.lowPercentile = 0.1f;
    exposureSettings_.highPercentile = 0.9f;
}

AdvancedPostProcess::~AdvancedPostProcess() {
    shutdown();
}

void AdvancedPostProcess::initialize() {
    createBuffers();
    createShaders();
    createPipelines();
    createDescriptorSets();
    
    // Initialize upscaling backend if not TAA
    if (upscaleSettings_.backend != UpscalingBackend::TAA) {
        initializeUpscaling(upscaleSettings_.backend, displayWidth_, displayHeight_);
    }
}

void AdvancedPostProcess::shutdown() {
    VkDevice device = context_.getDevice();
    
    // Cleanup upscaling backends
    shutdownFSR2();
    shutdownXeSS();
    
    // Cleanup Vulkan resources
    if (cocPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, cocPipeline_, nullptr);
        cocPipeline_ = VK_NULL_HANDLE;
    }
    if (bokehPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, bokehPipeline_, nullptr);
        bokehPipeline_ = VK_NULL_HANDLE;
    }
    if (lutPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, lutPipeline_, nullptr);
        lutPipeline_ = VK_NULL_HANDLE;
    }
    if (histogramPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, histogramPipeline_, nullptr);
        histogramPipeline_ = VK_NULL_HANDLE;
    }
    if (exposurePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, exposurePipeline_, nullptr);
        exposurePipeline_ = VK_NULL_HANDLE;
    }
    
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
    
    // Cleanup intermediate buffers
    destroyIntermediateBuffers();
}

// ============================================================================
// UPSCALING BACKENDS
// ============================================================================

void AdvancedPostProcess::initializeUpscaling(UpscalingBackend backend, 
                                               uint32_t displayWidth, 
                                               uint32_t displayHeight) {
    displayWidth_ = displayWidth;
    displayHeight_ = displayHeight;
    
    switch (backend) {
        case UpscalingBackend::FSR2:
            initializeFSR2();
            break;
        case UpscalingBackend::XeSS:
            initializeXeSS();
            break;
        case UpscalingBackend::DLSS:
            initializeDLSS();
            break;
        case UpscalingBackend::TAA:
        default:
            // TAA uses existing implementation
            break;
    }
    
    currentBackend_ = backend;
    
    // Calculate optimal render resolution based on quality mode
    calculateRenderResolution();
}

void AdvancedPostProcess::calculateRenderResolution() {
    float scale = 1.0f;
    
    switch (upscaleSettings_.qualityMode) {
        case 0: // Quality
            scale = 0.667f;
            break;
        case 1: // Balanced
            scale = 0.59f;
            break;
        case 2: // Performance
            scale = 0.5f;
            break;
        case 3: // Ultra Performance
            scale = 0.333f;
            break;
    }
    
    renderWidth_ = static_cast<uint32_t>(displayWidth_ * scale);
    renderHeight_ = static_cast<uint32_t>(displayHeight_ * scale);
    
    // Ensure even dimensions
    renderWidth_ = (renderWidth_ + 1) & ~1;
    renderHeight_ = (renderHeight_ + 1) & ~1;
    
    // Calculate mip bias for texture sampling at reduced resolution
    upscaleSettings_.mipBias = std::log2(scale);
}

void AdvancedPostProcess::initializeFSR2() {
#ifdef HAS_FSR2
    // FSR 2.0 initialization using AMD FidelityFX SDK
    FfxFsr2ContextDescription contextDesc = {};
    contextDesc.flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE | 
                        FFX_FSR2_ENABLE_AUTO_EXPOSURE |
                        FFX_FSR2_ENABLE_DEPTH_INVERTED;
    contextDesc.maxRenderSize.width = renderWidth_;
    contextDesc.maxRenderSize.height = renderHeight_;
    contextDesc.displaySize.width = displayWidth_;
    contextDesc.displaySize.height = displayHeight_;
    
    // Get Vulkan backend interface
    FfxFsr2Interface backendInterface = {};
    size_t scratchBufferSize = ffxFsr2GetScratchMemorySizeVK(context_.getPhysicalDevice());
    void* scratchBuffer = malloc(scratchBufferSize);
    
    FfxErrorCode error = ffxFsr2GetInterfaceVK(&backendInterface, 
                                                scratchBuffer, 
                                                scratchBufferSize,
                                                context_.getPhysicalDevice(),
                                                vkGetDeviceProcAddr);
    
    if (error != FFX_OK) {
        free(scratchBuffer);
        throw std::runtime_error("Failed to get FSR 2.0 Vulkan interface");
    }
    
    contextDesc.backendInterface = backendInterface;
    contextDesc.device = ffxGetDeviceVK(context_.getDevice());
    
    fsr2Context_ = new FfxFsr2Context();
    error = ffxFsr2ContextCreate(fsr2Context_, &contextDesc);
    
    if (error != FFX_OK) {
        delete fsr2Context_;
        fsr2Context_ = nullptr;
        free(scratchBuffer);
        throw std::runtime_error("Failed to create FSR 2.0 context");
    }
    
    fsr2ScratchBuffer_ = scratchBuffer;
#else
    throw std::runtime_error("FSR 2.0 support not compiled in");
#endif
}

void AdvancedPostProcess::shutdownFSR2() {
#ifdef HAS_FSR2
    if (fsr2Context_) {
        ffxFsr2ContextDestroy(fsr2Context_);
        delete fsr2Context_;
        fsr2Context_ = nullptr;
    }
    if (fsr2ScratchBuffer_) {
        free(fsr2ScratchBuffer_);
        fsr2ScratchBuffer_ = nullptr;
    }
#endif
}

void AdvancedPostProcess::initializeXeSS() {
#ifdef HAS_XESS
    xess_version_t version;
    xess_result_t result = xessGetVersion(&version);
    
    xess_d3d12_init_params_t initParams = {};
    initParams.outputResolution.x = displayWidth_;
    initParams.outputResolution.y = displayHeight_;
    initParams.qualitySetting = static_cast<xess_quality_settings_t>(upscaleSettings_.qualityMode);
    initParams.initFlags = XESS_INIT_FLAG_INVERTED_DEPTH | 
                           XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
    
    // XeSS requires Vulkan interop - use VK_KHR_external_memory
    // This is a simplified placeholder - real implementation needs careful Vulkan/D3D12 interop
    
    xessContext_ = nullptr; // Placeholder
#else
    throw std::runtime_error("XeSS support not compiled in");
#endif
}

void AdvancedPostProcess::shutdownXeSS() {
#ifdef HAS_XESS
    if (xessContext_) {
        xessDestroyContext(xessContext_);
        xessContext_ = nullptr;
    }
#endif
}

void AdvancedPostProcess::initializeDLSS() {
#ifdef HAS_DLSS
    // DLSS requires NVIDIA GPU and NGX SDK
    // Similar structure to FSR2/XeSS initialization
    throw std::runtime_error("DLSS implementation pending");
#else
    throw std::runtime_error("DLSS support not compiled in");
#endif
}

void AdvancedPostProcess::shutdownDLSS() {
#ifdef HAS_DLSS
    // Cleanup DLSS resources
#endif
}

// ============================================================================
// MAIN PROCESSING
// ============================================================================

void AdvancedPostProcess::process(VkCommandBuffer cmd,
                                  VkImageView colorInput,
                                  VkImageView depthInput,
                                  VkImageView motionVectors,
                                  VkImageView output,
                                  float deltaTime,
                                  const glm::mat4& jitterMatrix) {
    // 1. Auto-exposure (compute histogram and adapt)
    processAutoExposure(cmd, colorInput, deltaTime);
    
    // 2. Depth of field (if enabled)
    VkImageView dofOutput = colorInput;
    if (dofSettings_.enabled) {
        processDOF(cmd, colorInput, depthInput);
        dofOutput = cocImageView_; // Use DOF output
    }
    
    // 3. Temporal upscaling
    VkImageView upscaleOutput = dofOutput;
    switch (currentBackend_) {
        case UpscalingBackend::FSR2:
            processFSR2(cmd, dofOutput, depthInput, motionVectors, deltaTime, jitterMatrix);
            upscaleOutput = upscaledImageView_;
            break;
        case UpscalingBackend::XeSS:
            processXeSS(cmd, dofOutput, depthInput, motionVectors, deltaTime, jitterMatrix);
            upscaleOutput = upscaledImageView_;
            break;
        case UpscalingBackend::DLSS:
            processDLSS(cmd, dofOutput, depthInput, motionVectors, deltaTime, jitterMatrix);
            upscaleOutput = upscaledImageView_;
            break;
        case UpscalingBackend::TAA:
        default:
            // TAA handled elsewhere
            break;
    }
    
    // 4. Color grading with LUT (if enabled)
    if (lutSettings_.enabled && lutSettings_.primaryLUT != VK_NULL_HANDLE) {
        processColorGrading(cmd, upscaleOutput, output);
    } else {
        // Copy to output
        copyToOutput(cmd, upscaleOutput, output);
    }
}

// ============================================================================
// DEPTH OF FIELD
// ============================================================================

void AdvancedPostProcess::processDOF(VkCommandBuffer cmd, 
                                     VkImageView colorInput, 
                                     VkImageView depthInput) {
    // Pass 1: Calculate Circle of Confusion
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cocPipeline_);
        
        struct CoCPushConstants {
            glm::vec4 screenSize;
            glm::vec4 dofParams;
            glm::vec4 cameraParams;
        } pushData;
        
        pushData.screenSize = glm::vec4(renderWidth_, renderHeight_, 
                                        1.0f / renderWidth_, 1.0f / renderHeight_);
        pushData.dofParams = glm::vec4(dofSettings_.focusDistance,
                                       dofSettings_.focalLength,
                                       dofSettings_.fStop,
                                       dofSettings_.sensorWidth);
        pushData.cameraParams = glm::vec4(0.1f, 1000.0f, // near/far
                                          1.0f, dofSettings_.maxCoC);
        
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                          0, sizeof(pushData), &pushData);
        
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                               0, 1, &cocDescriptorSet_, 0, nullptr);
        
        uint32_t groupsX = (renderWidth_ + 7) / 8;
        uint32_t groupsY = (renderHeight_ + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }
    
    // Barrier: CoC write -> bokeh read
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.image = cocImage_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        vkCmdPipelineBarrier(cmd, 
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    
    // Pass 2: Bokeh blur
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bokehPipeline_);
        
        struct BokehPushConstants {
            glm::vec4 screenSize;
            glm::vec4 dofParams;
            glm::vec4 dofParams2;
            glm::vec4 bokehParams;
            glm::vec4 effectParams;
        } pushData;
        
        pushData.screenSize = glm::vec4(renderWidth_, renderHeight_,
                                        1.0f / renderWidth_, 1.0f / renderHeight_);
        pushData.dofParams = glm::vec4(dofSettings_.focusDistance,
                                       dofSettings_.focalLength,
                                       dofSettings_.fStop,
                                       dofSettings_.sensorWidth);
        pushData.dofParams2 = glm::vec4(1.0f, 0.0f, 0.0f, dofSettings_.maxCoC);
        pushData.bokehParams = glm::vec4(dofSettings_.bladeCount,
                                         dofSettings_.bladeRotation,
                                         dofSettings_.bladeCurvature,
                                         64.0f); // maxSamples
        pushData.effectParams = glm::vec4(dofSettings_.catsEyeAmount,
                                          0.0f,
                                          dofSettings_.chromaticAberration > 0.0f ? 1.0f : 0.0f,
                                          dofSettings_.chromaticAberration);
        
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                          0, sizeof(pushData), &pushData);
        
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                               0, 1, &bokehDescriptorSet_, 0, nullptr);
        
        uint32_t groupsX = (renderWidth_ + 7) / 8;
        uint32_t groupsY = (renderHeight_ + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }
}

// ============================================================================
// COLOR GRADING
// ============================================================================

void AdvancedPostProcess::processColorGrading(VkCommandBuffer cmd,
                                              VkImageView input,
                                              VkImageView output) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lutPipeline_);
    
    struct LUTPushConstants {
        glm::vec4 screenSize;
        glm::vec4 lutParams;
        glm::vec4 colorAdjust;
        glm::vec4 colorOffset;
        glm::vec4 shadowsMidtonesHighlights;
    } pushData;
    
    pushData.screenSize = glm::vec4(displayWidth_, displayHeight_,
                                    1.0f / displayWidth_, 1.0f / displayHeight_);
    pushData.lutParams = glm::vec4(lutSettings_.lutSize,
                                   lutSettings_.lutIntensity,
                                   lutSettings_.blendFactor,
                                   lutSettings_.secondaryLUT != VK_NULL_HANDLE ? 1.0f : 0.0f);
    pushData.colorAdjust = glm::vec4(lutSettings_.saturation,
                                     lutSettings_.contrast,
                                     lutSettings_.gamma,
                                     lutSettings_.gain);
    pushData.colorOffset = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    pushData.shadowsMidtonesHighlights = glm::vec4(lutSettings_.shadows,
                                                   lutSettings_.midtones,
                                                   lutSettings_.highlights,
                                                   0.33f); // shadows width
    
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(pushData), &pushData);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                           0, 1, &lutDescriptorSet_, 0, nullptr);
    
    uint32_t groupsX = (displayWidth_ + 7) / 8;
    uint32_t groupsY = (displayHeight_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

// ============================================================================
// AUTO EXPOSURE
// ============================================================================

void AdvancedPostProcess::processAutoExposure(VkCommandBuffer cmd,
                                              VkImageView colorInput,
                                              float deltaTime) {
    // Pass 1: Build histogram
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, histogramPipeline_);
        
        // Clear histogram buffer
        vkCmdFillBuffer(cmd, histogramBuffer_, 0, 256 * sizeof(uint32_t), 0);
        
        VkBufferMemoryBarrier clearBarrier = {};
        clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        clearBarrier.buffer = histogramBuffer_;
        clearBarrier.size = VK_WHOLE_SIZE;
        
        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 0, nullptr, 1, &clearBarrier, 0, nullptr);
        
        struct HistogramPush {
            glm::vec4 screenSize;
            glm::vec4 params;
        } pushData;
        
        pushData.screenSize = glm::vec4(renderWidth_, renderHeight_,
                                        1.0f / renderWidth_, 1.0f / renderHeight_);
        pushData.params = glm::vec4(std::log2(exposureSettings_.histogramMin),
                                   std::log2(exposureSettings_.histogramMax),
                                   0.0f, 0.0f);
        
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                          0, sizeof(pushData), &pushData);
        
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                               0, 1, &histogramDescriptorSet_, 0, nullptr);
        
        uint32_t groupsX = (renderWidth_ + 15) / 16;
        uint32_t groupsY = (renderHeight_ + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }
    
    // Barrier
    {
        VkBufferMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.buffer = histogramBuffer_;
        barrier.size = VK_WHOLE_SIZE;
        
        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 0, nullptr, 1, &barrier, 0, nullptr);
    }
    
    // Pass 2: Calculate exposure from histogram
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, exposurePipeline_);
        
        struct ExposurePush {
            glm::vec4 params;
            glm::vec4 params2;
        } pushData;
        
        pushData.params = glm::vec4(std::log2(exposureSettings_.histogramMin),
                                   std::log2(exposureSettings_.histogramMax),
                                   exposureSettings_.lowPercentile,
                                   exposureSettings_.highPercentile);
        pushData.params2 = glm::vec4(exposureSettings_.adaptationSpeed * deltaTime,
                                    exposureSettings_.minExposure,
                                    exposureSettings_.maxExposure,
                                    exposureSettings_.targetExposure);
        
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                          0, sizeof(pushData), &pushData);
        
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                               0, 1, &exposureDescriptorSet_, 0, nullptr);
        
        vkCmdDispatch(cmd, 1, 1, 1);
    }
}

// ============================================================================
// UPSCALING DISPATCH
// ============================================================================

void AdvancedPostProcess::processFSR2(VkCommandBuffer cmd,
                                      VkImageView colorInput,
                                      VkImageView depthInput,
                                      VkImageView motionVectors,
                                      float deltaTime,
                                      const glm::mat4& jitterMatrix) {
#ifdef HAS_FSR2
    if (!fsr2Context_) return;
    
    FfxFsr2DispatchDescription dispatchDesc = {};
    
    // Set up input resources
    dispatchDesc.commandList = ffxGetCommandListVK(cmd);
    dispatchDesc.color = ffxGetTextureResourceVK(fsr2Context_, colorInputImage_, colorInput,
                                                  renderWidth_, renderHeight_,
                                                  VK_FORMAT_R16G16B16A16_SFLOAT);
    dispatchDesc.depth = ffxGetTextureResourceVK(fsr2Context_, depthImage_, depthInput,
                                                 renderWidth_, renderHeight_,
                                                 VK_FORMAT_D32_SFLOAT);
    dispatchDesc.motionVectors = ffxGetTextureResourceVK(fsr2Context_, motionImage_, motionVectors,
                                                         renderWidth_, renderHeight_,
                                                         VK_FORMAT_R16G16_SFLOAT);
    dispatchDesc.output = ffxGetTextureResourceVK(fsr2Context_, upscaledImage_, upscaledImageView_,
                                                  displayWidth_, displayHeight_,
                                                  VK_FORMAT_R16G16B16A16_SFLOAT);
    
    // Set parameters
    dispatchDesc.jitterOffset.x = jitterMatrix[3][0] * 0.5f * renderWidth_;
    dispatchDesc.jitterOffset.y = jitterMatrix[3][1] * 0.5f * renderHeight_;
    dispatchDesc.motionVectorScale.x = -renderWidth_;
    dispatchDesc.motionVectorScale.y = -renderHeight_;
    dispatchDesc.renderSize.width = renderWidth_;
    dispatchDesc.renderSize.height = renderHeight_;
    dispatchDesc.frameTimeDelta = deltaTime * 1000.0f; // ms
    dispatchDesc.preExposure = 1.0f;
    dispatchDesc.reset = false;
    dispatchDesc.cameraNear = 0.1f;
    dispatchDesc.cameraFar = 1000.0f;
    dispatchDesc.cameraFovAngleVertical = glm::radians(60.0f);
    dispatchDesc.sharpness = upscaleSettings_.sharpness;
    
    FfxErrorCode error = ffxFsr2ContextDispatch(fsr2Context_, &dispatchDesc);
    if (error != FFX_OK) {
        // Handle error
    }
#endif
}

void AdvancedPostProcess::processXeSS(VkCommandBuffer cmd,
                                      VkImageView colorInput,
                                      VkImageView depthInput,
                                      VkImageView motionVectors,
                                      float deltaTime,
                                      const glm::mat4& jitterMatrix) {
#ifdef HAS_XESS
    // XeSS dispatch implementation
    // Similar structure to FSR2
#endif
}

void AdvancedPostProcess::processDLSS(VkCommandBuffer cmd,
                                      VkImageView colorInput,
                                      VkImageView depthInput,
                                      VkImageView motionVectors,
                                      float deltaTime,
                                      const glm::mat4& jitterMatrix) {
#ifdef HAS_DLSS
    // DLSS dispatch implementation
#endif
}

// ============================================================================
// LUT LOADING
// ============================================================================

void AdvancedPostProcess::loadLUT(const std::string& path) {
    // Load .cube LUT file format
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open LUT file: " + path);
    }
    
    std::string line;
    uint32_t lutSize = 0;
    std::vector<glm::vec3> lutData;
    
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        // Parse LUT size
        if (line.find("LUT_3D_SIZE") != std::string::npos) {
            sscanf(line.c_str(), "LUT_3D_SIZE %u", &lutSize);
            lutData.reserve(lutSize * lutSize * lutSize);
            continue;
        }
        
        // Skip other metadata
        if (line.find("DOMAIN_") != std::string::npos ||
            line.find("TITLE") != std::string::npos) {
            continue;
        }
        
        // Parse color values
        float r, g, b;
        if (sscanf(line.c_str(), "%f %f %f", &r, &g, &b) == 3) {
            lutData.push_back(glm::vec3(r, g, b));
        }
    }
    
    if (lutSize == 0 || lutData.size() != lutSize * lutSize * lutSize) {
        throw std::runtime_error("Invalid LUT file format");
    }
    
    // Create 3D texture
    createLUTTexture(lutData.data(), lutSize);
    lutSettings_.lutSize = static_cast<float>(lutSize);
}

void AdvancedPostProcess::createLUTTexture(const glm::vec3* data, uint32_t size) {
    VkDevice device = context_.getDevice();
    
    // Create 3D image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {size, size, size};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    VkImage lutImage;
    vkCreateImage(device, &imageInfo, nullptr, &lutImage);
    
    // Allocate memory and upload data...
    // (Full implementation would include staging buffer and memory management)
    
    lutSettings_.primaryLUT = lutImage;
}

// ============================================================================
// RESOURCE CREATION STUBS
// ============================================================================

void AdvancedPostProcess::createBuffers() {
    VkDevice device = context_.getDevice();
    
    // Create CoC buffer
    VkImageCreateInfo cocImageInfo = {};
    cocImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    cocImageInfo.imageType = VK_IMAGE_TYPE_2D;
    cocImageInfo.format = VK_FORMAT_R16_SFLOAT;
    cocImageInfo.extent = {renderWidth_, renderHeight_, 1};
    cocImageInfo.mipLevels = 1;
    cocImageInfo.arrayLayers = 1;
    cocImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    cocImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    cocImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    vkCreateImage(device, &cocImageInfo, nullptr, &cocImage_);
    
    // Create histogram buffer
    VkBufferCreateInfo histogramBufferInfo = {};
    histogramBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    histogramBufferInfo.size = 256 * sizeof(uint32_t);
    histogramBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    vkCreateBuffer(device, &histogramBufferInfo, nullptr, &histogramBuffer_);
    
    // Create exposure buffer (single float for current exposure)
    VkBufferCreateInfo exposureBufferInfo = {};
    exposureBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    exposureBufferInfo.size = sizeof(float);
    exposureBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    
    vkCreateBuffer(device, &exposureBufferInfo, nullptr, &exposureBuffer_);
    
    // Create upscaled output image
    VkImageCreateInfo upscaledImageInfo = {};
    upscaledImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    upscaledImageInfo.imageType = VK_IMAGE_TYPE_2D;
    upscaledImageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    upscaledImageInfo.extent = {displayWidth_, displayHeight_, 1};
    upscaledImageInfo.mipLevels = 1;
    upscaledImageInfo.arrayLayers = 1;
    upscaledImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    upscaledImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    upscaledImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    vkCreateImage(device, &upscaledImageInfo, nullptr, &upscaledImage_);
    
    // Memory allocation would go here...
}

void AdvancedPostProcess::destroyIntermediateBuffers() {
    VkDevice device = context_.getDevice();
    
    if (cocImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, cocImage_, nullptr);
        cocImage_ = VK_NULL_HANDLE;
    }
    if (histogramBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, histogramBuffer_, nullptr);
        histogramBuffer_ = VK_NULL_HANDLE;
    }
    if (exposureBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, exposureBuffer_, nullptr);
        exposureBuffer_ = VK_NULL_HANDLE;
    }
    if (upscaledImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, upscaledImage_, nullptr);
        upscaledImage_ = VK_NULL_HANDLE;
    }
}

void AdvancedPostProcess::createShaders() {
    // Load shader modules from SPIR-V
    // cocShader_ = loadShaderModule("shaders/dof_coc.comp.spv");
    // bokehShader_ = loadShaderModule("shaders/bokeh_dof.comp.spv");
    // lutShader_ = loadShaderModule("shaders/lut_color_grading.comp.spv");
    // histogramShader_ = loadShaderModule("shaders/histogram.comp.spv");
    // exposureShader_ = loadShaderModule("shaders/exposure.comp.spv");
}

void AdvancedPostProcess::createPipelines() {
    // Create compute pipelines for each shader
}

void AdvancedPostProcess::createDescriptorSets() {
    // Create descriptor sets for each pass
}

void AdvancedPostProcess::copyToOutput(VkCommandBuffer cmd, VkImageView src, VkImageView dst) {
    // Copy or blit src to dst
}

} // namespace sanic
