/**
 * ScreenSpaceTracing.h
 * 
 * Screen-space ray tracing system for reflections and AO.
 * Implements hierarchical ray marching with Hi-Z acceleration.
 * 
 * Turn 19-21: Enhanced screen-space tracing
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

struct SSRConfig {
    float maxDistance = 100.0f;
    float thickness = 0.5f;
    float stride = 1.0f;
    float roughnessThreshold = 0.5f;
    float fadeStart = 0.1f;
    uint32_t maxSteps = 64;
    bool useHierarchical = true;
    bool temporalFilter = true;
};

struct ConeTraceConfig {
    float coneAngle = 0.1f;
    float maxDistance = 50.0f;
    float aoIntensity = 1.0f;
    uint32_t maxSteps = 32;
    bool useSDF = true;
    bool computeAO = true;
};

class ScreenSpaceTracing {
public:
    ScreenSpaceTracing() = default;
    ~ScreenSpaceTracing();
    
    bool initialize(VulkanContext* context, uint32_t width, uint32_t height);
    void cleanup();
    bool resize(uint32_t width, uint32_t height);
    
    /**
     * Perform hierarchical SSR
     */
    void traceReflections(VkCommandBuffer cmd,
                          VkImageView colorBuffer,
                          VkImageView depthBuffer,
                          VkImageView normalBuffer,
                          VkImageView materialBuffer,
                          VkImageView hizBuffer,
                          const glm::mat4& viewProj,
                          const glm::mat4& invViewProj,
                          const glm::mat4& view,
                          const SSRConfig& config = SSRConfig{});
    
    /**
     * Cone tracing for rough reflections and AO
     */
    void coneTrace(VkCommandBuffer cmd,
                   VkImageView colorBuffer,
                   VkImageView depthBuffer,
                   VkImageView normalBuffer,
                   VkImageView materialBuffer,
                   VkImageView hizBuffer,
                   VkImageView globalSDF,
                   const glm::vec3& sdfOrigin,
                   const glm::vec3& sdfExtent,
                   float sdfVoxelSize,
                   const glm::mat4& viewProj,
                   const glm::mat4& invViewProj,
                   const ConeTraceConfig& config = ConeTraceConfig{});
    
    /**
     * Temporal filtering for SSR
     */
    void temporalFilter(VkCommandBuffer cmd,
                        VkImageView currentSSR,
                        VkImageView historySSR,
                        VkImageView motionVectors,
                        VkImageView outputSSR);
    
    /**
     * Get result image views
     */
    VkImageView getReflectionView() const { return reflectionView_; }
    VkImageView getHitBufferView() const { return hitBufferView_; }
    VkImageView getConeTraceView() const { return coneTraceView_; }
    
private:
    bool createImages();
    bool createDescriptorSets();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    VulkanContext* context_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    
    // SSR output
    VkImage reflectionImage_ = VK_NULL_HANDLE;
    VkImageView reflectionView_ = VK_NULL_HANDLE;
    VkDeviceMemory reflectionMemory_ = VK_NULL_HANDLE;
    
    VkImage hitBufferImage_ = VK_NULL_HANDLE;
    VkImageView hitBufferView_ = VK_NULL_HANDLE;
    VkDeviceMemory hitBufferMemory_ = VK_NULL_HANDLE;
    
    // Cone trace output
    VkImage coneTraceImage_ = VK_NULL_HANDLE;
    VkImageView coneTraceView_ = VK_NULL_HANDLE;
    VkDeviceMemory coneTraceMemory_ = VK_NULL_HANDLE;
    
    // History for temporal
    VkImage historyImage_ = VK_NULL_HANDLE;
    VkImageView historyView_ = VK_NULL_HANDLE;
    VkDeviceMemory historyMemory_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline ssrPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ssrLayout_ = VK_NULL_HANDLE;
    VkPipeline coneTracePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout coneTraceLayout_ = VK_NULL_HANDLE;
    VkPipeline temporalPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout temporalLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorSetLayout ssrDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout coneTraceDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet ssrDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet coneTraceDescSet_ = VK_NULL_HANDLE;
    
    // Samplers
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler pointSampler_ = VK_NULL_HANDLE;
    
    bool initialized_ = false;
};
