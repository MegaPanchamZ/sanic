/**
 * RayTracedShadows.h
 * 
 * Hardware ray-traced soft shadows with denoising.
 * Provides high-quality area light shadows.
 * 
 * Turn 37-39: Ray traced shadows
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

// Shadow ray configuration
struct ShadowRayConfig {
    uint32_t raysPerPixel = 1;          // SPP for shadows
    uint32_t maxBounces = 1;            // Usually 1 for shadows
    float maxDistance = 1000.0f;
    float normalBias = 0.01f;
    float lightRadius = 0.0f;           // 0 = point/directional, >0 = area
    bool softShadows = true;
    float softShadowAngle = 0.5f;       // Degrees for sun
};

// Per-light shadow settings
struct LightShadowSettings {
    glm::vec4 position;         // xyz = pos, w = type
    glm::vec4 direction;        // xyz = dir, w = range
    glm::vec4 color;            // xyz = color, w = intensity
    glm::vec4 shadowParams;     // x = radius, y = angle, z = bias, w = enabled
};

// Denoiser settings
struct ShadowDenoiserSettings {
    bool enabled = true;
    uint32_t spatialPasses = 2;
    float spatialSigma = 1.0f;
    float temporalAlpha = 0.05f;        // Lower = more temporal
    float depthThreshold = 0.1f;
    float normalThreshold = 0.9f;
    bool useVarianceGuided = true;
};

struct RTShadowConfig {
    ShadowRayConfig rayConfig;
    ShadowDenoiserSettings denoiser;
    
    uint32_t maxLights = 4;
    bool useHardwareBVH = true;
    bool useMiniTraversal = false;      // For simple scenes
    
    VkFormat shadowFormat = VK_FORMAT_R8_UNORM;
    VkFormat momentFormat = VK_FORMAT_R16G16_SFLOAT;
};

class RayTracedShadows {
public:
    RayTracedShadows() = default;
    ~RayTracedShadows();
    
    bool initialize(VulkanContext* context,
                    uint32_t width,
                    uint32_t height,
                    const RTShadowConfig& config = {});
    void cleanup();
    
    void resize(uint32_t width, uint32_t height);
    
    // Set acceleration structure for tracing
    void setAccelerationStructure(VkAccelerationStructureKHR tlas);
    
    // Trace shadows for all lights
    void trace(VkCommandBuffer cmd,
               VkImageView depthBuffer,
               VkImageView normalBuffer,
               VkImageView motionVectors,
               const glm::mat4& viewProj,
               const glm::mat4& invViewProj,
               const glm::mat4& prevViewProj,
               const std::vector<LightShadowSettings>& lights);
    
    // Denoise shadow masks
    void denoise(VkCommandBuffer cmd);
    
    // Get shadow mask for light
    VkImageView getShadowMask(uint32_t lightIndex = 0) const;
    
    // Combined shadow mask (all lights)
    VkImageView getCombinedShadowMask() const { return combinedShadowView_; }
    
    const RTShadowConfig& getConfig() const { return config_; }
    void setConfig(const RTShadowConfig& config) { config_ = config; }
    
private:
    bool createShadowTextures();
    bool createPipelines();
    bool createShaderBindingTable();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    void spatialDenoise(VkCommandBuffer cmd);
    void temporalDenoise(VkCommandBuffer cmd);
    
    VulkanContext* context_ = nullptr;
    bool initialized_ = false;
    
    RTShadowConfig config_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t frameIndex_ = 0;
    
    VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
    
    // Raw shadow masks (noisy)
    std::vector<VkImage> rawShadowImages_;
    std::vector<VkDeviceMemory> rawShadowMemory_;
    std::vector<VkImageView> rawShadowViews_;
    
    // Denoised shadow masks
    std::vector<VkImage> denoisedShadowImages_;
    std::vector<VkDeviceMemory> denoisedShadowMemory_;
    std::vector<VkImageView> denoisedShadowViews_;
    
    // History for temporal
    VkImage shadowHistory_[2] = {};
    VkDeviceMemory shadowHistoryMemory_[2] = {};
    VkImageView shadowHistoryView_[2] = {};
    
    // Moments for variance estimation
    VkImage momentsImage_ = VK_NULL_HANDLE;
    VkDeviceMemory momentsMemory_ = VK_NULL_HANDLE;
    VkImageView momentsView_ = VK_NULL_HANDLE;
    
    // Combined shadow mask
    VkImage combinedShadow_ = VK_NULL_HANDLE;
    VkDeviceMemory combinedShadowMemory_ = VK_NULL_HANDLE;
    VkImageView combinedShadowView_ = VK_NULL_HANDLE;
    
    // Ray tracing pipeline
    VkPipeline rtPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout rtLayout_ = VK_NULL_HANDLE;
    
    // Shader binding table
    VkBuffer sbtBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory sbtMemory_ = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR raygenRegion_{};
    VkStridedDeviceAddressRegionKHR missRegion_{};
    VkStridedDeviceAddressRegionKHR hitRegion_{};
    VkStridedDeviceAddressRegionKHR callableRegion_{};
    
    // Denoiser pipelines
    VkPipeline spatialPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout spatialLayout_ = VK_NULL_HANDLE;
    
    VkPipeline temporalPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout temporalLayout_ = VK_NULL_HANDLE;
    
    VkPipeline combinePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout combineLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
};
