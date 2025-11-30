/**
 * PostProcessAdvanced.h
 * 
 * Advanced post-processing stack including:
 * - FSR 2.0 / XeSS temporal upscaling
 * - Bokeh depth of field
 * - LUT-based color grading
 * - Auto exposure with histogram
 * - Physically-based bloom
 * 
 * Based on Unreal Engine's post-processing pipeline.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>

class VulkanContext;
class RenderGraph;

// ============================================================================
// UPSCALING MODE
// ============================================================================

enum class UpscalingMode {
    None,           // No upscaling
    FSR2,           // AMD FidelityFX Super Resolution 2.0
    XeSS,           // Intel Xe Super Sampling
    DLSS,           // NVIDIA Deep Learning Super Sampling (requires SDK)
    TAA             // Internal TAA-based upscaling
};

enum class UpscalingQuality {
    Performance,    // 50% render resolution
    Balanced,       // 59% render resolution
    Quality,        // 67% render resolution
    UltraQuality,   // 77% render resolution
    Native          // 100% (AA only)
};

// ============================================================================
// BOKEH DOF SETTINGS
// ============================================================================

struct BokehDOFSettings {
    bool enabled = false;
    
    // Focus
    float focusDistance = 10.0f;      // Distance to focus plane (meters)
    float focalLength = 50.0f;        // Lens focal length (mm)
    float fStop = 2.8f;               // Aperture f-number
    float sensorWidth = 36.0f;        // Sensor width (mm, 36mm = full frame)
    
    // Bokeh shape
    uint32_t bladeCount = 6;          // Number of aperture blades
    float bladeRotation = 0.0f;       // Rotation of aperture blades (degrees)
    float bladeCurvature = 0.0f;      // Curvature of blades (0 = straight)
    
    // Quality
    uint32_t maxSamples = 64;         // Maximum CoC samples
    float cocScale = 1.0f;            // Circle of confusion scale
    float nearTransitionSize = 0.5f;  // Size of near blur transition
    float farTransitionSize = 0.5f;   // Size of far blur transition
    
    // Cat's eye effect (for wide aperture lenses)
    float catsEyeAmount = 0.0f;       // Amount of cat's eye vignetting
    float catsEyeAngle = 45.0f;       // Angle of cat's eye effect
    
    // Chromatic aberration in bokeh
    bool chromaticBokeh = false;
    float chromaticBokehAmount = 0.5f;
    
    // Calculate circle of confusion (in pixels) for a given depth
    float calculateCoC(float depth, float screenHeight) const;
};

// ============================================================================
// LUT COLOR GRADING
// ============================================================================

struct LUTColorGradingSettings {
    bool enabled = true;
    
    // LUT settings
    std::string lutPath = "";         // Path to 3D LUT file (cube format)
    float lutIntensity = 1.0f;        // Blend between original and LUT color
    uint32_t lutSize = 32;            // LUT resolution (32x32x32 typical)
    
    // Pre-LUT adjustments
    float whiteBalance = 6500.0f;     // Color temperature (Kelvin)
    float tint = 0.0f;                // Green-magenta tint
    
    // Basic color correction
    float exposure = 0.0f;            // EV adjustment
    float contrast = 1.0f;
    float saturation = 1.0f;
    float vibrance = 0.0f;            // Smart saturation
    glm::vec3 colorFilter = glm::vec3(1.0f);
    
    // Color wheels (lift/gamma/gain)
    glm::vec3 shadowColor = glm::vec3(1.0f);    // Affects dark tones
    glm::vec3 midtoneColor = glm::vec3(1.0f);   // Affects mid tones
    glm::vec3 highlightColor = glm::vec3(1.0f); // Affects bright tones
    
    float shadowOffset = 0.0f;
    float midtoneOffset = 0.0f;
    float highlightOffset = 0.0f;
    
    // HSL adjustments (per-color control)
    glm::vec3 hueShift = glm::vec3(0.0f);       // Per-channel hue shift
    glm::vec3 saturationMult = glm::vec3(1.0f); // Per-channel saturation
    glm::vec3 luminanceMult = glm::vec3(1.0f);  // Per-channel luminance
    
    // Curves (stored as control points for GPU interpolation)
    std::vector<glm::vec2> rgbCurve;      // Master RGB curve
    std::vector<glm::vec2> redCurve;
    std::vector<glm::vec2> greenCurve;
    std::vector<glm::vec2> blueCurve;
    std::vector<glm::vec2> lumaCurve;     // Luminance-only curve
};

// ============================================================================
// AUTO EXPOSURE SETTINGS
// ============================================================================

struct AutoExposureSettings {
    bool enabled = true;
    
    // Metering
    float exposureCompensation = 0.0f;  // EV compensation
    float minExposure = -4.0f;          // Minimum EV
    float maxExposure = 16.0f;          // Maximum EV
    
    // Adaptation speed
    float speedUp = 3.0f;               // Adaptation speed (light to dark)
    float speedDown = 1.0f;             // Adaptation speed (dark to light)
    
    // Histogram settings
    float histogramMin = -8.0f;         // Log2 luminance minimum
    float histogramMax = 4.0f;          // Log2 luminance maximum
    float lowPercent = 80.0f;           // Low end percentage to exclude
    float highPercent = 98.5f;          // High end percentage to exclude
    
    // Target luminance
    float targetExposure = 0.0f;        // Target average luminance (EV)
    float meteringMask = 1.0f;          // Center-weighted metering
    
    // Pre-exposure (manual override)
    bool useManualExposure = false;
    float manualExposure = 0.0f;
};

// ============================================================================
// BLOOM SETTINGS (PHYSICALLY-BASED)
// ============================================================================

struct PhysicalBloomSettings {
    bool enabled = true;
    
    // Physical parameters
    float intensity = 0.5f;
    float threshold = 1.0f;           // Brightness threshold for bloom
    float thresholdSoftness = 0.5f;   // Soft threshold knee
    
    // Scatter simulation
    float scatter = 0.7f;             // Energy conservation in scatter
    glm::vec3 tint = glm::vec3(1.0f);
    
    // Convolution kernel
    uint32_t mipLevels = 6;           // Number of mip levels for blur
    std::array<float, 6> mipWeights = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f, 0.1f};
    
    // Lens flare
    bool lensFlare = false;
    float lensFlareIntensity = 0.1f;
    glm::vec3 lensFlareTint = glm::vec3(1.0f, 0.9f, 0.8f);
    
    // Anamorphic
    float anamorphicRatio = 1.0f;     // 1.0 = circular, 2.0 = horizontal stretch
    float anamorphicBlend = 0.5f;
};

// ============================================================================
// POST-PROCESS CONFIG
// ============================================================================

struct PostProcessAdvancedConfig {
    // Upscaling
    UpscalingMode upscalingMode = UpscalingMode::FSR2;
    UpscalingQuality upscalingQuality = UpscalingQuality::Quality;
    float sharpness = 0.5f;           // Upscaling sharpness
    
    // Depth of Field
    BokehDOFSettings dof;
    
    // Color Grading
    LUTColorGradingSettings colorGrading;
    
    // Auto Exposure
    AutoExposureSettings autoExposure;
    
    // Bloom
    PhysicalBloomSettings bloom;
    
    // Tonemapping
    enum class TonemapOperator {
        ACES,
        ACESFitted,
        Reinhard,
        Uncharted2,
        AgX,
        Neutral
    } tonemapOperator = TonemapOperator::ACES;
    float gamma = 2.2f;
    
    // Motion blur
    bool motionBlur = false;
    float motionBlurIntensity = 1.0f;
    uint32_t motionBlurSamples = 8;
    float motionBlurMaxVelocity = 40.0f;
    
    // Chromatic aberration
    bool chromaticAberration = false;
    float chromaticAberrationIntensity = 0.5f;
    
    // Vignette
    bool vignette = true;
    float vignetteIntensity = 0.3f;
    float vignetteSmoothness = 0.5f;
    
    // Film grain
    bool filmGrain = false;
    float filmGrainIntensity = 0.1f;
    float filmGrainResponse = 0.8f;
    
    // Sharpen
    bool sharpen = true;
    float sharpenIntensity = 0.5f;
};

// ============================================================================
// GPU STRUCTURES
// ============================================================================

struct alignas(16) PostProcessUniforms {
    // Screen
    glm::vec4 screenSize;             // xy = size, zw = 1/size
    glm::vec4 renderSize;             // xy = render res, zw = 1/render res
    
    // Time
    float time;
    float deltaTime;
    uint32_t frameIndex;
    float pad0;
    
    // Camera
    glm::mat4 viewProjMatrix;
    glm::mat4 invViewProjMatrix;
    glm::mat4 prevViewProjMatrix;
    glm::vec4 cameraPosition;
    
    // DOF
    glm::vec4 dofParams;              // x = focusDist, y = focalLength, z = fStop, w = sensorWidth
    glm::vec4 dofParams2;             // x = cocScale, y = nearTrans, z = farTrans, w = bladeCount
    
    // Auto exposure
    glm::vec4 exposureParams;         // x = current, y = target, z = speed, w = compensation
    
    // Bloom
    glm::vec4 bloomParams;            // x = intensity, y = threshold, z = scatter, w = mipCount
    glm::vec4 bloomTint;              // xyz = tint, w = lensFlareIntensity
    
    // Color grading
    glm::vec4 colorGradingParams;     // x = lutIntensity, y = exposure, z = contrast, w = saturation
    glm::vec4 colorFilter;
    glm::vec4 whiteBalanceParams;     // x = temperature, y = tint, zw = unused
    
    // Tonemap
    glm::vec4 tonemapParams;          // x = gamma, y = operator, zw = unused
    
    // Effects
    glm::vec4 vignetteParams;         // x = intensity, y = smoothness, zw = center
    glm::vec4 chromaticParams;        // x = intensity, yzw = unused
    glm::vec4 filmGrainParams;        // x = intensity, y = response, zw = unused
    glm::vec4 sharpenParams;          // x = intensity, yzw = unused
    glm::vec4 motionBlurParams;       // x = intensity, y = samples, z = maxVel, w = unused
};

// ============================================================================
// POST-PROCESS ADVANCED CLASS
// ============================================================================

class PostProcessAdvanced {
public:
    PostProcessAdvanced(VulkanContext& context);
    ~PostProcessAdvanced();
    
    /**
     * Initialize post-processing
     */
    void initialize(uint32_t outputWidth, uint32_t outputHeight,
                   uint32_t renderWidth, uint32_t renderHeight);
    void shutdown();
    
    /**
     * Resize outputs
     */
    void resize(uint32_t outputWidth, uint32_t outputHeight,
               uint32_t renderWidth, uint32_t renderHeight);
    
    /**
     * Set configuration
     */
    void setConfig(const PostProcessAdvancedConfig& config);
    const PostProcessAdvancedConfig& getConfig() const { return config; }
    
    /**
     * Load 3D LUT for color grading
     */
    bool loadLUT(const std::string& path);
    
    // ========================================================================
    // MAIN PROCESSING
    // ========================================================================
    
    /**
     * Process full post-processing stack
     */
    void process(VkCommandBuffer cmd,
                VkImageView hdrColor,
                VkImageView depth,
                VkImageView velocity,
                VkImageView outputLDR,
                const glm::mat4& viewProjMatrix,
                const glm::mat4& prevViewProjMatrix,
                const glm::vec3& cameraPosition,
                float deltaTime);
    
    /**
     * Process individual passes (for render graph integration)
     */
    void computeHistogram(VkCommandBuffer cmd, VkImageView hdrInput);
    void computeExposure(VkCommandBuffer cmd);
    void computeBloom(VkCommandBuffer cmd, VkImageView hdrInput);
    void computeDOF(VkCommandBuffer cmd, VkImageView hdrInput, VkImageView depth);
    void computeUpscaling(VkCommandBuffer cmd, VkImageView input, VkImageView output,
                         VkImageView depth, VkImageView velocity);
    void applyColorGrading(VkCommandBuffer cmd, VkImageView input, VkImageView output);
    void applyTonemap(VkCommandBuffer cmd, VkImageView input, VkImageView output);
    void applyFinalEffects(VkCommandBuffer cmd, VkImageView input, VkImageView output);
    
    // ========================================================================
    // ACCESSORS
    // ========================================================================
    
    float getCurrentExposure() const { return currentExposure; }
    VkImageView getBloomTexture() const { return bloomMipViews.empty() ? VK_NULL_HANDLE : bloomMipViews[0]; }
    
private:
    VulkanContext& context;
    PostProcessAdvancedConfig config;
    
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    
    float currentExposure = 1.0f;
    uint32_t frameIndex = 0;
    float time = 0.0f;
    
    // ========================================================================
    // RENDER TARGETS
    // ========================================================================
    
    // Bloom chain
    VkImage bloomImage = VK_NULL_HANDLE;
    VkDeviceMemory bloomMemory = VK_NULL_HANDLE;
    std::vector<VkImageView> bloomMipViews;
    
    // DOF
    VkImage dofCoCImage = VK_NULL_HANDLE;
    VkDeviceMemory dofCoCMemory = VK_NULL_HANDLE;
    VkImageView dofCoCView = VK_NULL_HANDLE;
    
    VkImage dofNearImage = VK_NULL_HANDLE;
    VkDeviceMemory dofNearMemory = VK_NULL_HANDLE;
    VkImageView dofNearView = VK_NULL_HANDLE;
    
    VkImage dofFarImage = VK_NULL_HANDLE;
    VkDeviceMemory dofFarMemory = VK_NULL_HANDLE;
    VkImageView dofFarView = VK_NULL_HANDLE;
    
    VkImage bokehImage = VK_NULL_HANDLE;
    VkDeviceMemory bokehMemory = VK_NULL_HANDLE;
    VkImageView bokehView = VK_NULL_HANDLE;
    
    // Upscaling
    VkImage upscaleHistory[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory upscaleHistoryMemory[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView upscaleHistoryView[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    uint32_t historyIndex = 0;
    
    // LUT
    VkImage lutImage = VK_NULL_HANDLE;
    VkDeviceMemory lutMemory = VK_NULL_HANDLE;
    VkImageView lutView = VK_NULL_HANDLE;
    
    // Histogram
    VkBuffer histogramBuffer = VK_NULL_HANDLE;
    VkDeviceMemory histogramMemory = VK_NULL_HANDLE;
    
    VkBuffer exposureBuffer = VK_NULL_HANDLE;
    VkDeviceMemory exposureBufferMemory = VK_NULL_HANDLE;
    void* exposureBufferMapped = nullptr;
    
    // Intermediate
    VkImage intermediateImage = VK_NULL_HANDLE;
    VkDeviceMemory intermediateMemory = VK_NULL_HANDLE;
    VkImageView intermediateView = VK_NULL_HANDLE;
    
    // ========================================================================
    // PIPELINES
    // ========================================================================
    
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    
    VkSampler linearSampler = VK_NULL_HANDLE;
    VkSampler pointSampler = VK_NULL_HANDLE;
    VkSampler lutSampler = VK_NULL_HANDLE;
    
    // Histogram
    VkPipeline histogramPipeline = VK_NULL_HANDLE;
    VkPipelineLayout histogramLayout = VK_NULL_HANDLE;
    
    // Exposure
    VkPipeline exposurePipeline = VK_NULL_HANDLE;
    VkPipelineLayout exposureLayout = VK_NULL_HANDLE;
    
    // Bloom
    VkPipeline bloomDownsamplePipeline = VK_NULL_HANDLE;
    VkPipeline bloomUpsamplePipeline = VK_NULL_HANDLE;
    VkPipelineLayout bloomLayout = VK_NULL_HANDLE;
    
    // DOF
    VkPipeline dofCoCPipeline = VK_NULL_HANDLE;
    VkPipeline dofDownsamplePipeline = VK_NULL_HANDLE;
    VkPipeline dofBokehPipeline = VK_NULL_HANDLE;
    VkPipeline dofCompositePipeline = VK_NULL_HANDLE;
    VkPipelineLayout dofLayout = VK_NULL_HANDLE;
    
    // Upscaling (FSR 2.0 / TAA fallback)
    VkPipeline upscalePipeline = VK_NULL_HANDLE;
    VkPipelineLayout upscaleLayout = VK_NULL_HANDLE;
    
    // Color grading
    VkPipeline colorGradingPipeline = VK_NULL_HANDLE;
    VkPipelineLayout colorGradingLayout = VK_NULL_HANDLE;
    
    // Tonemap
    VkPipeline tonemapPipeline = VK_NULL_HANDLE;
    VkPipelineLayout tonemapLayout = VK_NULL_HANDLE;
    
    // Final effects (vignette, grain, sharpen)
    VkPipeline finalEffectsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout finalEffectsLayout = VK_NULL_HANDLE;
    
    // ========================================================================
    // INTERNAL METHODS
    // ========================================================================
    
    void createRenderTargets();
    void createPipelines();
    void createDescriptors();
    void createSamplers();
    
    void updateUniforms(const glm::mat4& viewProjMatrix,
                       const glm::mat4& prevViewProjMatrix,
                       const glm::vec3& cameraPosition,
                       float deltaTime);
    
    // Bloom passes
    void bloomDownsample(VkCommandBuffer cmd, VkImageView input);
    void bloomUpsample(VkCommandBuffer cmd);
    
    // DOF passes
    void dofComputeCoC(VkCommandBuffer cmd, VkImageView depth);
    void dofDownsample(VkCommandBuffer cmd, VkImageView hdrInput);
    void dofBokeh(VkCommandBuffer cmd);
    void dofComposite(VkCommandBuffer cmd, VkImageView hdrInput, VkImageView output);
    
    // Helpers
    void createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                    VkFormat format, VkImageUsageFlags usage,
                    VkImage& image, VkDeviceMemory& memory);
    VkImageView createImageView(VkImage image, VkFormat format, 
                               VkImageAspectFlags aspect, uint32_t mipLevel = 0);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& memory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
};

// ============================================================================
// BOKEH DOF IMPLEMENTATION
// ============================================================================

inline float BokehDOFSettings::calculateCoC(float depth, float screenHeight) const {
    // Calculate circle of confusion using thin lens equation
    // CoC = abs(A * f * (S - D) / (D * (S - f)))
    // Where: A = aperture diameter, f = focal length, S = focus distance, D = depth
    
    float apertureDiameter = focalLength / fStop;
    float focusDistMM = focusDistance * 1000.0f;  // Convert to mm
    float depthMM = depth * 1000.0f;
    
    // Thin lens equation
    float cocMM = std::abs(apertureDiameter * focalLength * (focusDistMM - depthMM) / 
                          (depthMM * (focusDistMM - focalLength)));
    
    // Convert to pixels based on sensor size and screen height
    float cocPixels = cocMM / sensorWidth * screenHeight * cocScale;
    
    return cocPixels;
}

