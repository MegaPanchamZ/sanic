/**
 * TemporalSuperResolution.h
 * 
 * Temporal Super Resolution (TSR) - Advanced upscaling with temporal stability.
 * Based on Unreal Engine 5's TSR implementation.
 * 
 * Upgrades TAA to full temporal upscaling with:
 * - Motion-aware reprojection
 * - Subpixel detail reconstruction
 * - Anti-ghosting with history validation
 * - Lumen integration for stable GI
 * - Dynamic resolution support
 * 
 * Reference: Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalSuperResolution/
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <vector>

class VulkanContext;

namespace Sanic {

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr uint32_t TSR_HISTORY_COUNT = 2;
constexpr uint32_t TSR_MAX_MIPS = 8;

// ============================================================================
// CONFIGURATION
// ============================================================================

/**
 * TSR quality preset
 */
enum class TSRQuality : uint32_t {
    Performance = 0,    // 1.5x upscale, fewer samples
    Balanced = 1,       // 1.5x-2x upscale
    Quality = 2,        // 2x upscale, full features  
    UltraQuality = 3,   // Native resolution, maximum temporal
    Native = 4          // No upscaling, pure temporal
};

/**
 * Motion vector precision
 */
enum class MotionVectorPrecision : uint32_t {
    Half = 0,       // 16-bit
    Full = 1        // 32-bit
};

/**
 * TSR configuration
 */
struct TSRConfig {
    TSRQuality quality = TSRQuality::Quality;
    
    // Resolution
    float upscaleRatio = 2.0f;          // Output / Input ratio
    bool dynamicResolution = false;
    float minUpscaleRatio = 1.5f;
    float maxUpscaleRatio = 3.0f;
    
    // Jitter
    uint32_t jitterPhases = 8;          // Halton sequence length
    float jitterSpread = 1.0f;
    
    // History
    float historyBlend = 0.9f;          // Base temporal blend
    float maxHistoryBlend = 0.98f;
    float minHistoryBlend = 0.7f;
    
    // Anti-ghosting
    bool enableAntiGhosting = true;
    float ghostingThreshold = 0.1f;
    float velocityWeighting = 1.0f;
    
    // Subpixel detail
    bool enableSubpixelReconstruction = true;
    float sharpening = 0.2f;
    float detailPreservation = 1.0f;
    
    // Lumen integration
    bool lumenStableHistory = true;
    float lumenHistoryClamp = 0.5f;
    
    // Motion vectors
    MotionVectorPrecision mvPrecision = MotionVectorPrecision::Full;
    bool dilateMotionVectors = true;
    
    // Responsiveness
    float reactivity = 0.0f;            // 0 = stable, 1 = responsive to changes
    
    // Debug
    bool debugShowMotionVectors = false;
    bool debugShowHistoryRejection = false;
};

// ============================================================================
// STRUCTURES
// ============================================================================

/**
 * Per-frame TSR data
 */
struct TSRFrameData {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::mat4 invViewProjMatrix;
    glm::mat4 prevViewProjMatrix;
    
    glm::vec4 jitterOffset;         // xy = current, zw = previous
    glm::vec4 screenParams;         // xy = render size, zw = 1/size
    glm::vec4 outputParams;         // xy = output size, zw = 1/size
    
    float upscaleRatio;
    float historyBlend;
    float sharpening;
    uint32_t frameIndex;
    
    uint32_t flags;
    float time;
    float deltaTime;
    float pad;
};

/**
 * TSR pass timing
 */
struct TSRTiming {
    float reprojectMs = 0.0f;
    float reconstructMs = 0.0f;
    float sharpenMs = 0.0f;
    float totalMs = 0.0f;
};

// ============================================================================
// TEMPORAL SUPER RESOLUTION SYSTEM
// ============================================================================

class TemporalSuperResolution {
public:
    TemporalSuperResolution(VulkanContext& context);
    ~TemporalSuperResolution();
    
    bool initialize(uint32_t renderWidth, uint32_t renderHeight,
                   uint32_t outputWidth, uint32_t outputHeight,
                   const TSRConfig& config = {});
    void shutdown();
    
    // Configuration
    void setConfig(const TSRConfig& config);
    const TSRConfig& getConfig() const { return config_; }
    
    void setOutputResolution(uint32_t width, uint32_t height);
    void setRenderResolution(uint32_t width, uint32_t height);
    
    // Per-frame update
    void beginFrame(const glm::mat4& view, const glm::mat4& proj,
                   const glm::mat4& prevViewProj);
    
    /**
     * Get jitter offset for current frame (in pixels)
     */
    glm::vec2 getJitterOffset() const { return currentJitter_; }
    
    /**
     * Get jitter offset in NDC space (-1 to 1)
     */
    glm::vec2 getJitterOffsetNDC() const;
    
    /**
     * Execute TSR upscaling
     * 
     * @param cmd Command buffer
     * @param colorInput Low-res rendered color
     * @param depthInput Low-res depth
     * @param motionVectors Per-pixel motion vectors
     * @param reactivityMask Optional mask for responsive areas (UI, particles)
     */
    void execute(VkCommandBuffer cmd,
                VkImageView colorInput,
                VkImageView depthInput,
                VkImageView motionVectors,
                VkImageView reactivityMask = VK_NULL_HANDLE);
    
    /**
     * Get upscaled output
     */
    VkImageView getOutput() const { return outputView_; }
    
    /**
     * Get timing info
     */
    const TSRTiming& getTiming() const { return timing_; }
    
    // Debug
    void debugVisualize(VkCommandBuffer cmd, VkImageView output, int mode = 0);
    
    // Lumen integration
    /**
     * Get history texture for Lumen to sample
     */
    VkImageView getHistoryForLumen() const;
    
    /**
     * Signal that camera cut occurred - reset history
     */
    void onCameraCut();
    
private:
    void createResources();
    void createPipelines();
    void updateJitter();
    void updateFrameData();
    
    // Passes
    void passReproject(VkCommandBuffer cmd, VkImageView color, VkImageView depth,
                      VkImageView motion);
    void passReconstruct(VkCommandBuffer cmd);
    void passSharpen(VkCommandBuffer cmd);
    
    // Helpers
    void createImage(VkImage& image, VkDeviceMemory& memory, VkImageView& view,
                    uint32_t width, uint32_t height, VkFormat format, 
                    VkImageUsageFlags usage, uint32_t mipLevels = 1);
    void destroyImage(VkImage& image, VkDeviceMemory& memory, VkImageView& view);
    VkShaderModule loadShader(const std::string& path);
    
    VulkanContext& context_;
    TSRConfig config_;
    
    // Resolution
    uint32_t renderWidth_ = 1920;
    uint32_t renderHeight_ = 1080;
    uint32_t outputWidth_ = 3840;
    uint32_t outputHeight_ = 2160;
    
    // Frame state
    uint32_t frameIndex_ = 0;
    uint32_t historyIndex_ = 0;
    glm::vec2 currentJitter_;
    glm::vec2 prevJitter_;
    bool cameraReset_ = true;
    
    TSRFrameData frameData_;
    
    // History textures (ping-pong)
    std::array<VkImage, TSR_HISTORY_COUNT> historyImages_{};
    std::array<VkDeviceMemory, TSR_HISTORY_COUNT> historyMemory_{};
    std::array<VkImageView, TSR_HISTORY_COUNT> historyViews_{};
    
    // Intermediate textures
    VkImage reprojectedImage_ = VK_NULL_HANDLE;
    VkDeviceMemory reprojectedMemory_ = VK_NULL_HANDLE;
    VkImageView reprojectedView_ = VK_NULL_HANDLE;
    
    VkImage reconstructedImage_ = VK_NULL_HANDLE;
    VkDeviceMemory reconstructedMemory_ = VK_NULL_HANDLE;
    VkImageView reconstructedView_ = VK_NULL_HANDLE;
    
    // Output texture
    VkImage outputImage_ = VK_NULL_HANDLE;
    VkDeviceMemory outputMemory_ = VK_NULL_HANDLE;
    VkImageView outputView_ = VK_NULL_HANDLE;
    
    // Motion vector processing
    VkImage dilatedMotionImage_ = VK_NULL_HANDLE;
    VkDeviceMemory dilatedMotionMemory_ = VK_NULL_HANDLE;
    VkImageView dilatedMotionView_ = VK_NULL_HANDLE;
    
    // Buffers
    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    void* uniformMapped_ = nullptr;
    
    // Sampler
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler pointSampler_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline reprojectPipeline_ = VK_NULL_HANDLE;
    VkPipeline reconstructPipeline_ = VK_NULL_HANDLE;
    VkPipeline sharpenPipeline_ = VK_NULL_HANDLE;
    VkPipeline dilateMotionPipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    
    // Timing
    TSRTiming timing_;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    
    // Halton sequence for jitter
    std::vector<glm::vec2> haltonSequence_;
};

// ============================================================================
// HALTON SEQUENCE UTILITIES
// ============================================================================

namespace Halton {
    /**
     * Generate a Halton sequence value
     */
    inline float halton(int index, int base) {
        float result = 0.0f;
        float f = 1.0f / base;
        int i = index;
        while (i > 0) {
            result += f * (i % base);
            i /= base;
            f /= base;
        }
        return result;
    }
    
    /**
     * Generate 2D Halton point
     */
    inline glm::vec2 halton2D(int index) {
        return glm::vec2(halton(index, 2), halton(index, 3));
    }
    
    /**
     * Generate jittered Halton sequence
     */
    inline std::vector<glm::vec2> generateSequence(int count) {
        std::vector<glm::vec2> result(count);
        for (int i = 0; i < count; ++i) {
            result[i] = halton2D(i + 1) - 0.5f;  // Center around 0
        }
        return result;
    }
}

} // namespace Sanic
