/**
 * PostProcess.h
 * 
 * Post-processing pipeline including bloom, tonemapping, DOF, motion blur.
 * 
 * Turn 40-42: Post-processing
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

// Tonemapping operators
enum class TonemapOperator {
    Reinhard,
    ReinhardExtended,
    ACES,
    ACESFitted,
    Uncharted2,
    Uchimura,
    Lottes,
    None
};

// Bloom settings
struct BloomSettings {
    bool enabled = true;
    float threshold = 1.0f;
    float intensity = 0.5f;
    float radius = 0.005f;
    uint32_t mipLevels = 6;
    float scatter = 0.7f;
    glm::vec3 tint = glm::vec3(1.0f);
};

// Depth of Field settings
struct DOFSettings {
    bool enabled = false;
    float focusDistance = 10.0f;
    float focusRange = 5.0f;
    float maxBlur = 1.0f;
    float aperture = 2.8f;      // f-stop
    uint32_t bladeCount = 6;    // Bokeh shape
    float bladeRotation = 0.0f;
};

// Motion blur settings
struct MotionBlurSettings {
    bool enabled = false;
    float intensity = 1.0f;
    uint32_t samples = 8;
    float maxVelocity = 40.0f;  // Pixels
    bool perObjectBlur = true;
};

// Chromatic aberration
struct ChromaticAberrationSettings {
    bool enabled = false;
    float intensity = 0.5f;
    glm::vec2 center = glm::vec2(0.5f);
};

// Vignette
struct VignetteSettings {
    bool enabled = true;
    float intensity = 0.3f;
    float smoothness = 0.5f;
    glm::vec2 center = glm::vec2(0.5f);
    glm::vec3 color = glm::vec3(0.0f);
};

// Film grain
struct FilmGrainSettings {
    bool enabled = false;
    float intensity = 0.1f;
    float response = 0.8f;
};

// Color grading
struct ColorGradingSettings {
    float exposure = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    glm::vec3 colorFilter = glm::vec3(1.0f);
    glm::vec3 shadows = glm::vec3(1.0f);
    glm::vec3 midtones = glm::vec3(1.0f);
    glm::vec3 highlights = glm::vec3(1.0f);
    float shadowsStart = 0.0f;
    float shadowsEnd = 0.3f;
    float highlightsStart = 0.55f;
    float highlightsEnd = 1.0f;
};

// Sharpening
struct SharpenSettings {
    bool enabled = true;
    float intensity = 0.5f;
    float threshold = 0.1f;     // Avoid sharpening noise
};

struct PostProcessConfig {
    TonemapOperator tonemap = TonemapOperator::ACESFitted;
    float gamma = 2.2f;
    
    BloomSettings bloom;
    DOFSettings dof;
    MotionBlurSettings motionBlur;
    ChromaticAberrationSettings chromaticAberration;
    VignetteSettings vignette;
    FilmGrainSettings filmGrain;
    ColorGradingSettings colorGrading;
    SharpenSettings sharpen;
    
    // Auto exposure
    bool autoExposure = true;
    float minExposure = 0.5f;
    float maxExposure = 4.0f;
    float exposureSpeed = 1.0f;
    float keyValue = 0.18f;     // Middle gray
    
    // TAA integration
    bool taaEnabled = true;
    float taaSharpness = 0.5f;
};

// GPU push constants for post-process
struct alignas(16) PostProcessPushConstants {
    glm::vec4 screenSize;       // xy = size, zw = 1/size
    glm::vec4 bloomParams;      // x = threshold, y = intensity, z = scatter, w = mipLevel
    glm::vec4 dofParams;        // x = focusDist, y = focusRange, z = maxBlur, w = aperture
    glm::vec4 tonemapParams;    // x = exposure, y = gamma, z = operator, w = pad
    glm::vec4 vignetteParams;   // x = intensity, y = smoothness, zw = center
    glm::vec4 colorFilter;
    float time;
    uint32_t frameIndex;
    float deltaTime;
    uint32_t flags;             // Bitfield for enabled effects
};

class PostProcess {
public:
    PostProcess() = default;
    ~PostProcess();
    
    bool initialize(VulkanContext* context,
                    uint32_t width,
                    uint32_t height,
                    const PostProcessConfig& config = {});
    void cleanup();
    
    void resize(uint32_t width, uint32_t height);
    
    // Main post-process pass
    void process(VkCommandBuffer cmd,
                 VkImageView hdrInput,
                 VkImageView depthBuffer,
                 VkImageView velocityBuffer,
                 VkImageView outputLDR,
                 float deltaTime);
    
    // Individual passes (if needed separately)
    void computeBloom(VkCommandBuffer cmd, VkImageView hdrInput);
    void computeDOF(VkCommandBuffer cmd, VkImageView input, VkImageView depth);
    void computeMotionBlur(VkCommandBuffer cmd, VkImageView input, VkImageView velocity);
    void computeAutoExposure(VkCommandBuffer cmd, VkImageView hdrInput);
    void applyTonemap(VkCommandBuffer cmd, VkImageView hdrInput, VkImageView output);
    void applyFXAA(VkCommandBuffer cmd, VkImageView input, VkImageView output);
    
    // Configuration
    void setConfig(const PostProcessConfig& config) { config_ = config; }
    const PostProcessConfig& getConfig() const { return config_; }
    
    // Getters
    VkImageView getBloomTexture() const { return bloomMipViews_.empty() ? VK_NULL_HANDLE : bloomMipViews_[0]; }
    float getCurrentExposure() const { return currentExposure_; }
    
private:
    bool createRenderTargets();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    void bloomDownsample(VkCommandBuffer cmd);
    void bloomUpsample(VkCommandBuffer cmd);
    
    VulkanContext* context_ = nullptr;
    bool initialized_ = false;
    
    PostProcessConfig config_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t frameIndex_ = 0;
    float currentExposure_ = 1.0f;
    float time_ = 0.0f;
    
    // Bloom chain
    VkImage bloomImage_ = VK_NULL_HANDLE;
    VkDeviceMemory bloomMemory_ = VK_NULL_HANDLE;
    std::vector<VkImageView> bloomMipViews_;
    
    // DOF buffers
    VkImage dofNear_ = VK_NULL_HANDLE;
    VkDeviceMemory dofNearMemory_ = VK_NULL_HANDLE;
    VkImageView dofNearView_ = VK_NULL_HANDLE;
    
    VkImage dofFar_ = VK_NULL_HANDLE;
    VkDeviceMemory dofFarMemory_ = VK_NULL_HANDLE;
    VkImageView dofFarView_ = VK_NULL_HANDLE;
    
    // Exposure histogram
    VkBuffer histogramBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory histogramMemory_ = VK_NULL_HANDLE;
    
    VkBuffer exposureBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory exposureMemory_ = VK_NULL_HANDLE;
    
    // Intermediate targets
    VkImage intermediateImage_ = VK_NULL_HANDLE;
    VkDeviceMemory intermediateMemory_ = VK_NULL_HANDLE;
    VkImageView intermediateView_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline bloomDownPipeline_ = VK_NULL_HANDLE;
    VkPipeline bloomUpPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout bloomLayout_ = VK_NULL_HANDLE;
    
    VkPipeline dofCocPipeline_ = VK_NULL_HANDLE;
    VkPipeline dofBlurPipeline_ = VK_NULL_HANDLE;
    VkPipeline dofCompositePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout dofLayout_ = VK_NULL_HANDLE;
    
    VkPipeline motionBlurPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout motionBlurLayout_ = VK_NULL_HANDLE;
    
    VkPipeline histogramPipeline_ = VK_NULL_HANDLE;
    VkPipeline exposurePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout exposureLayout_ = VK_NULL_HANDLE;
    
    VkPipeline tonemapPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout tonemapLayout_ = VK_NULL_HANDLE;
    
    VkPipeline fxaaPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout fxaaLayout_ = VK_NULL_HANDLE;
    
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout compositeLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler pointSampler_ = VK_NULL_HANDLE;
};
