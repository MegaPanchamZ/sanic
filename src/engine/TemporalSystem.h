/**
 * TemporalSystem.h
 * 
 * Temporal Anti-Aliasing and motion vector system.
 * Implements Unreal-style TAA with:
 * - Motion vector generation from visibility buffer
 * - Catmull-Rom history sampling
 * - Variance clipping for ghost rejection
 * - Velocity-based feedback adjustment
 * 
 * Turn 11-12: Complete temporal stability system
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <string>

// Forward declarations
class VulkanContext;

// Halton sequence for jitter generation
class HaltonSequence {
public:
    static float halton(int index, int base) {
        float f = 1.0f;
        float r = 0.0f;
        int i = index;
        while (i > 0) {
            f /= static_cast<float>(base);
            r += f * (i % base);
            i /= base;
        }
        return r;
    }
    
    static glm::vec2 sample(int index) {
        // Use bases 2 and 3 for good 2D distribution
        return glm::vec2(
            halton(index + 1, 2) - 0.5f,
            halton(index + 1, 3) - 0.5f
        );
    }
};

// TAA configuration
struct TAAConfig {
    float feedbackMin = 0.88f;      // Minimum history blend (fast motion)
    float feedbackMax = 0.97f;      // Maximum history blend (static)
    float sharpness = 0.25f;        // Sharpening strength
    float motionScale = 1.0f;       // Motion vector scaling
    
    bool varianceClipping = true;   // Use variance-based clipping
    bool catmullRomSampling = true; // High-quality history sampling
    int jitterSequenceLength = 8;   // Halton sequence length
    
    // Motion blur settings (optional integration)
    bool motionBlurEnabled = false;
    float motionBlurIntensity = 1.0f;
    int motionBlurSamples = 8;
};

// Temporal frame data
struct TemporalFrameData {
    VkImage colorImage = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    
    glm::mat4 viewProj;
    glm::mat4 jitteredViewProj;
    glm::vec2 jitter;
    uint32_t frameIndex;
};

class TemporalSystem {
public:
    TemporalSystem() = default;
    ~TemporalSystem();
    
    // Non-copyable
    TemporalSystem(const TemporalSystem&) = delete;
    TemporalSystem& operator=(const TemporalSystem&) = delete;
    
    /**
     * Initialize the temporal system
     * @param context Vulkan context
     * @param width Screen width
     * @param height Screen height
     * @param config TAA configuration
     */
    bool initialize(VulkanContext* context,
                   uint32_t width, uint32_t height,
                   const TAAConfig& config = TAAConfig{});
    
    /**
     * Resize internal resources
     */
    bool resize(uint32_t width, uint32_t height);
    
    /**
     * Update configuration
     */
    void setConfig(const TAAConfig& config);
    
    /**
     * Begin a new frame - compute jitter and prepare history
     * @param viewProj Non-jittered view-projection matrix
     * @return Jittered view-projection matrix to use for rendering
     */
    glm::mat4 beginFrame(const glm::mat4& viewProj);
    
    /**
     * Get current frame's jitter offset in pixels
     */
    glm::vec2 getJitterOffset() const;
    
    /**
     * Get current frame's jitter in UV space (-0.5 to 0.5)
     */
    glm::vec2 getJitterUV() const;
    
    /**
     * Generate motion vectors from visibility buffer
     */
    void generateMotionVectors(VkCommandBuffer cmd,
                               VkBuffer visibilityBuffer,
                               VkBuffer vertexBuffer,
                               VkBuffer indexBuffer,
                               VkBuffer clusterBuffer,
                               VkBuffer instanceBuffer,
                               VkImageView positionBuffer,
                               const glm::mat4& invViewProj);
    
    /**
     * Apply TAA to current frame
     * @param cmd Command buffer
     * @param currentFrame Current frame color (input)
     * @param depthBuffer Current frame depth
     * @param outputFrame TAA result (output)
     */
    void applyTAA(VkCommandBuffer cmd,
                  VkImageView currentFrame,
                  VkImageView depthBuffer,
                  VkImageView outputFrame);
    
    /**
     * End frame - copy result to history buffer
     */
    void endFrame(VkCommandBuffer cmd, VkImageView currentResult);
    
    /**
     * Get the previous frame's view-projection matrix
     */
    const glm::mat4& getPreviousViewProj() const { return prevViewProj_; }
    
    /**
     * Get motion vectors image view
     */
    VkImageView getMotionVectorsView() const { return motionVectorsView_; }
    
    /**
     * Get history texture sampler
     */
    VkSampler getHistorySampler() const { return historySampler_; }
    
    /**
     * Get current frame index
     */
    uint32_t getFrameIndex() const { return frameIndex_; }
    
    void cleanup();
    
private:
    bool createImages();
    bool createDescriptorSets();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    VulkanContext* context_ = nullptr;
    TAAConfig config_;
    
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t frameIndex_ = 0;
    
    // Jitter tracking
    glm::vec2 currentJitter_;
    glm::mat4 currentViewProj_;
    glm::mat4 currentJitteredViewProj_;
    glm::mat4 prevViewProj_;
    
    // History double-buffering
    std::array<TemporalFrameData, 2> historyFrames_;
    uint32_t currentHistoryIndex_ = 0;
    
    // Motion vectors
    VkImage motionVectorsImage_ = VK_NULL_HANDLE;
    VkImageView motionVectorsView_ = VK_NULL_HANDLE;
    VkDeviceMemory motionVectorsMemory_ = VK_NULL_HANDLE;
    
    // Samplers
    VkSampler historySampler_ = VK_NULL_HANDLE;
    VkSampler pointSampler_ = VK_NULL_HANDLE;
    
    // Motion vector generation
    VkPipeline motionVectorPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout motionVectorLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout motionVectorDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool motionVectorDescPool_ = VK_NULL_HANDLE;
    VkDescriptorSet motionVectorDescSet_ = VK_NULL_HANDLE;
    
    // TAA pipeline
    VkPipeline taaPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout taaLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout taaDescLayout0_ = VK_NULL_HANDLE;  // Storage images
    VkDescriptorSetLayout taaDescLayout1_ = VK_NULL_HANDLE;  // History sampler
    VkDescriptorPool taaDescPool_ = VK_NULL_HANDLE;
    VkDescriptorSet taaDescSet0_ = VK_NULL_HANDLE;
    VkDescriptorSet taaDescSet1_ = VK_NULL_HANDLE;
    
    bool initialized_ = false;
};

/**
 * Optional: Motion blur post-process
 * Can be used in conjunction with TAA motion vectors
 */
class MotionBlurSystem {
public:
    struct Config {
        int samples = 8;
        float intensity = 1.0f;
        float maxVelocity = 32.0f;  // Max pixels of blur
        bool tileBasedOptimization = true;
    };
    
    bool initialize(VulkanContext* context, uint32_t width, uint32_t height);
    
    void apply(VkCommandBuffer cmd,
               VkImageView colorInput,
               VkImageView motionVectors,
               VkImageView depthBuffer,
               VkImageView output,
               const Config& config);
    
    void cleanup();
    
private:
    VulkanContext* context_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    
    // Tile-based optimization
    VkBuffer tileBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory tileMemory_ = VK_NULL_HANDLE;
};
