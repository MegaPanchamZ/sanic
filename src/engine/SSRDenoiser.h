#pragma once
#include "VulkanContext.h"
#include <glm/glm.hpp>

// SSR Denoiser - Temporal accumulation + Spatial filtering
// Implements À-Trous wavelet filter with temporal reprojection

struct DenoiserConfig {
    float temporalBlend = 0.9f;      // How much history to keep
    float sigmaLuminance = 4.0f;     // Edge-stopping for luminance
    float sigmaNormal = 32.0f;       // Edge-stopping for normals
    float sigmaDepth = 0.1f;         // Edge-stopping for depth
    int spatialPasses = 3;           // Number of À-Trous passes (1, 2, 4 spacing)
    bool enableTemporal = true;
    bool enableSpatial = true;
};

class SSRDenoiser {
public:
    SSRDenoiser(VulkanContext& context, uint32_t width, uint32_t height, VkDescriptorPool descriptorPool);
    ~SSRDenoiser();
    
    // Run the denoising pipeline
    // Input: raw SSR reflection
    // Output: denoised reflection
    void denoise(VkCommandBuffer cmd,
                 VkImageView inputReflection,
                 VkImageView velocityView,
                 VkImageView normalView,
                 VkImageView depthView,
                 VkSampler sampler);
    
    void resize(uint32_t width, uint32_t height);
    
    VkImageView getOutputView() const { return pingPongViews[0]; }
    VkImage getOutputImage() const { return pingPongImages[0]; }
    
    void setConfig(const DenoiserConfig& config) { this->config = config; }
    DenoiserConfig& getConfig() { return config; }
    
    // Set history from previous frame
    void swapHistory();
    VkImageView getHistoryView() const { return historyView; }
    
private:
    VulkanContext& context;
    uint32_t width, height;
    VkDescriptorPool descriptorPool;
    DenoiserConfig config;
    
    // Ping-pong buffers for spatial filter passes
    VkImage pingPongImages[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory pingPongMemory[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView pingPongViews[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    
    // History buffer for temporal accumulation
    VkImage historyImage = VK_NULL_HANDLE;
    VkDeviceMemory historyMemory = VK_NULL_HANDLE;
    VkImageView historyView = VK_NULL_HANDLE;
    
    // Temporal pass
    VkDescriptorSetLayout temporalSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet temporalDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout temporalPipelineLayout = VK_NULL_HANDLE;
    VkPipeline temporalPipeline = VK_NULL_HANDLE;
    
    // Spatial pass
    VkDescriptorSetLayout spatialSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet spatialDescriptorSets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkPipelineLayout spatialPipelineLayout = VK_NULL_HANDLE;
    VkPipeline spatialPipeline = VK_NULL_HANDLE;
    
    struct TemporalPushConstants {
        int width, height;
        float blendFactor;
        float velocityScale;
    };
    
    struct SpatialPushConstants {
        int passIndex;
        int width, height;
        float sigmaLuminance;
        float sigmaNormal;
        float sigmaDepth;
        float _padding[2];
    };
    
    bool firstFrame = true;
    
    void createImages();
    void createTemporalPipeline();
    void createSpatialPipeline();
    void allocateDescriptorSets();
    
    void destroyResources();
    
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
};
